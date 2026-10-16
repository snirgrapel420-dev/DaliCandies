#include "CandyEngine.h"
#include <algorithm>

namespace dali
{
namespace
{
constexpr int kPhysVoices = kMaxVoices + 2;      // two extra slots host voices that are fading out after being stolen
std::atomic<uint32_t> gBankVersion { 1 };
}

Patch Patch::from(const CandyDNA& d)
{
    Patch p;
    p.numLayers = d.numLayers; p.layers = d.layers; p.routes = d.routes; p.mods = d.mods; p.fx = d.fx;
    p.durationMs = d.durationMs; p.seed = d.seed;
    for (auto& r : p.routes) ModEngine::validate(r);
    return p;
}

std::shared_ptr<SoundBankData> buildSoundBank(const std::array<const CandyDNA*, kNumSoundSlots>& dna, double sampleRate)
{
    auto b = std::make_shared<SoundBankData>();
    for (size_t i = 0; i < kNumSoundSlots; ++i)
    {
        if (dna[i] == nullptr) continue;
        auto& s = b->slots[i];
        s.valid = true; s.patch = Patch::from(*dna[i]);
        for (int l = 0; l < kMaxLayers; ++l)
            if (s.patch.layers[(size_t) l].active) s.internalSrc[(size_t) l] = makeInternalSource(s.patch.layers[(size_t) l], sampleRate);
    }
    if (b->slots[0].valid) b->fx = b->slots[0].patch.fx;
    b->version = gBankVersion++;
    return b;
}

//==============================================================================
CandyEngine::CandyEngine() {}
CandyEngine::~CandyEngine() = default;

void CandyEngine::prepare(double sampleRate, int maxBlockSize, QualityMode q)
{
    sr_ = sampleRate; maxBlock_ = std::max(16, maxBlockSize); quality_ = q;
    voices_.clear();
    for (int i = 0; i < kPhysVoices; ++i)
    {
        auto v = std::make_unique<Voice>();
        v->mod.prepare(sr_);
        for (auto& l : v->layer)
        {
            for (int e = 0; e < kNumEngines; ++e) { l.eng[e] = createEngine((EngineType) e); l.eng[e]->prepare(sr_, q); }
            l.env.setSampleRate(sr_); l.insert.prepare(sr_); l.gain.setup(sr_, 1.5f); l.pan.setup(sr_, 4.0f);
        }
        voices_.push_back(std::move(v));
    }
    for (auto* b : { &dryL_, &dryR_, &sdL_, &sdR_, &srL_, &srR_, &smL_, &smR_, &fxL_, &fxR_, &tmpL_, &tmpR_ }) b->assign((size_t) maxBlock_, 0.0f);
    delay_.prepare(sr_); reverb_.prepare(sr_); modfx_.prepare(sr_); master_.prepare(sr_);
    reset();
}

void CandyEngine::reset()
{
    for (auto& v : voices_) v->active = false;
    queued_ = 0; voiceCounter_ = 0; totalEvents_ = 0; idleDelay_ = idleReverb_ = idleMod_ = 0;
    delay_.reset(); reverb_.reset(); modfx_.reset(); master_.reset();
    for (auto& g : gDest_) g = 0;
    driftPitch_ = driftLen_ = 0.0f; driftP_.fill(0.0f); driftRng_.reseed(0xD21F7u);
}

void CandyEngine::publishBank(std::shared_ptr<SoundBankData> bank)
{
    std::lock_guard<std::mutex> lk(bankMutex_);
    bankGraveyard_.push_back(bank);
    if (bankGraveyard_.size() > 24) bankGraveyard_.erase(bankGraveyard_.begin());
    bank_.store(bank.get(), std::memory_order_release);
}

void CandyEngine::setPerformance(float mw, float at, float fol, float tr, const float* m4)
{
    modWheel_ = mw; aftertouch_ = at; follower_ = fol; transient_ = tr;
    if (m4 != nullptr) for (int i = 0; i < kNumMacros; ++i) macros_[(size_t) i] = m4[i];
}

void CandyEngine::noteOffAll()
{
    for (auto& v : voices_) if (v->active) for (auto& l : v->layer) if (l.state == LayerRt::Playing) l.env.noteOff();
}

bool CandyEngine::queueEvent(const EventRequest& e)
{
    if (queued_ >= kQueue) return false;
    queue_[(size_t) queued_++] = e; return true;
}

int CandyEngine::activeVoices() const { int n = 0; for (auto& v : voices_) n += v->active ? 1 : 0; return n; }

int CandyEngine::allocateVoice()
{
    int logical = 0;
    for (auto& v : voices_) if (v->active && ! v->fading) ++logical;
    if (logical >= kMaxVoices)
    {
        // intelligent stealing: prefer the quietest, then the oldest, voice
        int victim = -1; float best = 1.0e9f;
        for (int i = 0; i < (int) voices_.size(); ++i)
        {
            auto& v = *voices_[(size_t) i];
            if (! v.active || v.fading) continue;
            const float age = (float) (voiceCounter_ - v.startId);
            const float score = v.peakEnv * (0.4f + v.velocity) - 0.0005f * age;
            if (score < best) { best = score; victim = i; }
        }
        if (victim >= 0) { voices_[(size_t) victim]->fading = true; voices_[(size_t) victim]->fadeOut = 96; }
    }
    for (int i = 0; i < (int) voices_.size(); ++i) if (! voices_[(size_t) i]->active) return i;
    // pathological: everything busy -> hard replace the oldest
    int oldest = 0; uint64_t id = ~0ull;
    for (int i = 0; i < (int) voices_.size(); ++i) if (voices_[(size_t) i]->startId < id) { id = voices_[(size_t) i]->startId; oldest = i; }
    return oldest;
}

void CandyEngine::startVoice(const EventRequest& e)
{
    const SoundBankData* bank = curBank_;
    if (bank == nullptr) return;
    int slot = std::clamp(e.slot, 0, kNumSoundSlots - 1);
    if (! bank->slots[(size_t) slot].valid)
    {
        slot = -1; for (int i = 0; i < kNumSoundSlots; ++i) if (bank->slots[(size_t) i].valid) { slot = i; break; }
        if (slot < 0) return;
    }
    const auto& sl = bank->slots[(size_t) slot];
    Voice& v = *voices_[(size_t) allocateVoice()];
    v.patch = sl.patch; v.active = true; v.fading = false; v.fadeOut = 0; v.startId = ++voiceCounter_; v.timeSec = 0;
    v.velocity = std::clamp(e.velocity, 0.0f, 1.0f); v.pitchOffset = e.pitchOffset; v.midiNote = e.midiNote; v.eventSeed = e.eventSeed;
    v.lengthScale = std::clamp(e.lengthScale, 0.05f, 16.0f); v.peakEnv = 1.0f;
    if (drift_ > 0.0f)
    {   // mean-reverting random walks: the sound wanders away from its origin over the repeats but never runs off
        driftPitch_ = driftPitch_ * 0.88f + driftRng_.bipolar() * drift_ * 1.6f;
        driftLen_   = driftLen_   * 0.85f + driftRng_.bipolar() * drift_ * 0.35f;
        for (auto& x : driftP_) x = x * 0.9f + driftRng_.bipolar() * drift_ * 0.12f;
        v.pitchOffset += std::clamp(driftPitch_, -6.0f * drift_, 6.0f * drift_);
        v.lengthScale = std::clamp(v.lengthScale * std::exp(driftLen_), 0.05f, 16.0f);
        for (auto& L : v.patch.layers) if (L.active) for (int i = 0; i < kParamsPerLayer; ++i) L.p[(size_t) i] = clamp01(L.p[(size_t) i] + driftP_[(size_t) i]);
    }
    v.bufPos = kControlRate; v.finished = false;
    for (auto& g : v.gDest) g = 0.0f;
    ++totalEvents_;

    const uint32_t seedBase = e.eventSeed ? hashCombine(v.patch.seed, e.eventSeed) : v.patch.seed;
    v.mod.start(v.patch.mods, v.patch.routes, seedBase, v.velocity, v.patch.layers[0].pitch + e.pitchOffset);

    bool anySolo = false;
    for (int l = 0; l < kMaxLayers; ++l) anySolo = anySolo || (v.patch.layers[(size_t) l].active && v.patch.layers[(size_t) l].solo);
    const SourceBuffer* ext = external_.get();
    float life = 0.0f;
    for (int l = 0; l < kMaxLayers; ++l)
    {
        auto& L = v.layer[(size_t) l]; const auto& cfg = v.patch.layers[(size_t) l];
        L.state = LayerRt::Idle;
        if (! cfg.active || l >= std::max(1, v.patch.numLayers)) continue;
        L.audible = ! cfg.mute && (! anySolo || cfg.solo);
        const uint32_t lseed = e.eventSeed ? hashCombine(cfg.seed, e.eventSeed) : cfg.seed;
        L.active = L.eng[(int) cfg.engine].get();
        const SourceBuffer* src = (wantsExternalSource(cfg) && ext != nullptr) ? ext : sl.internalSrc[(size_t) l].get();
        L.active->trigger(cfg, v.velocity, lseed, src);
        L.insert.start(cfg.insert, lseed);
        L.waitSamples = (int) (cfg.startOffsetMs * 0.001 * sr_);
        L.state = LayerRt::Waiting; L.elapsed = 0;
        L.gain.reset(1.0f); L.pan.reset(cfg.pan);
        life = std::max(life, cfg.startOffsetMs + cfg.durationMs * v.lengthScale + cfg.releaseMs + cfg.attackMs);
    }
    v.maxLifeSec = life * 0.001f + 1.5f;
}

void CandyEngine::renderChunk(Voice& v, int hostOffset)
{
    const float bpm = (float) host_.bpm;
    constexpr int c = kControlRate;
    const double ppq = host_.ppq + (double) hostOffset * (double) bpm / (60.0 * sr_);
    v.mod.setHost(bpm, ppq); v.mod.setLive(modWheel_, aftertouch_, follower_, transient_); v.mod.setMacros(macros_.data());
    v.mod.update(c, v.timeSec);
    const float* dest = v.mod.destinations();
    for (int d = 96; d < kNumDestinations; ++d) v.gDest[d] = dest[d];

    for (auto& b : v.buf) std::fill(b, b + c, 0.0f);
    float (&aL)[c] = v.buf[0]; float (&aR)[c] = v.buf[1]; float (&dL)[c] = v.buf[2]; float (&dR)[c] = v.buf[3];
    float (&rL)[c] = v.buf[4]; float (&rR)[c] = v.buf[5]; float (&mL)[c] = v.buf[6]; float (&mR)[c] = v.buf[7];
    float tL[c], tR[c];
    bool anyLive = false; float peak = 0.0f;

    for (int l = 0; l < kMaxLayers; ++l)
    {
        auto& L = v.layer[(size_t) l]; const auto& cfg = v.patch.layers[(size_t) l];
        if (L.state == LayerRt::Idle) continue;
        anyLive = true;
        int s0 = 0;
        if (L.state == LayerRt::Waiting)
        {
            if (L.waitSamples >= c) { L.waitSamples -= c; continue; }
            s0 = L.waitSamples; L.state = LayerRt::Playing;
            L.env.trigger(cfg.attackMs, cfg.decayMs, cfg.sustain, cfg.releaseMs, cfg.durationMs * v.lengthScale);
            L.gain.reset(L.audible ? dbToGain(std::min(cfg.gainDb + 24.0f * dest[kLayerDestBase + l * 8 + kLdGain], 12.0f)) : 0.0f);
        }
        const int len = c - s0;
        ControlFrame cf;
        for (int i = 0; i < kParamsPerLayer; ++i) cf.p[(size_t) i] = clamp01(cfg.p[(size_t) i] + dest[layerParamDest(l, i)]);
        const int base = kLayerDestBase + l * 8;
        cf.hz = midiToHz(cfg.pitch + v.pitchOffset + 24.0f * dest[base + kLdPitch]);
        cf.velocity = v.velocity; cf.timeSec = L.elapsed;
        cf.progress = std::clamp(L.elapsed / std::max(0.02f, 0.001f * cfg.durationMs * v.lengthScale), 0.0f, 1.0f);
        cf.bpm = bpm;
        L.active->process(cf, tL, tR, len);

        const float gdb = cfg.gainDb + 24.0f * dest[base + kLdGain];
        L.gain.set(L.audible ? dbToGain(std::min(gdb, 12.0f)) : 0.0f);
        L.pan.set(std::clamp(cfg.pan + dest[base + kLdPan], -1.0f, 1.0f));
        for (int i = 0; i < len; ++i) { const float e = L.env.tick() * L.gain.tick(); tL[i] *= e; tR[i] *= e; peak = std::max(peak, L.env.level); }
        L.insert.process(tL, tR, len, cfg.insertA + dest[base + kLdInsertA], cfg.insertB + dest[base + kLdInsertB], cf.hz, bpm);

        const float sD = clamp01(cfg.sendDelay + dest[base + kLdSendDelay]);
        const float sR = clamp01(cfg.sendReverb + dest[base + kLdSendReverb]);
        const float sM = clamp01(cfg.sendMod + dest[base + kLdSendMod]);
        for (int i = 0; i < len; ++i)
        {
            const float pan = L.pan.tick();
            const float xl = tL[i] * panGainL(pan) * 1.41421356f, xr = tR[i] * panGainR(pan) * 1.41421356f;
            aL[s0 + i] += xl; aR[s0 + i] += xr;
            dL[s0 + i] += xl * sD; dR[s0 + i] += xr * sD;
            rL[s0 + i] += xl * sR; rR[s0 + i] += xr * sR;
            mL[s0 + i] += xl * sM; mR[s0 + i] += xr * sM;
        }
        L.elapsed += (float) len / (float) sr_;
        if (! L.env.active()) L.state = LayerRt::Idle;
    }
    v.peakEnv = peak; v.timeSec += (float) c / (float) sr_;
    if (! anyLive || v.timeSec > v.maxLifeSec) v.finished = true;
    v.bufPos = 0;
}

void CandyEngine::renderVoice(Voice& v, int offset, int n)
{
    int done = 0;
    while (done < n && v.active)
    {
        if (v.bufPos >= kControlRate)
        {
            if (v.finished) { v.active = false; break; }
            renderChunk(v, offset + done);
        }
        const int take = std::min(kControlRate - v.bufPos, n - done);
        for (int i = 0; i < take; ++i)
        {
            float g = 1.0f;
            if (v.fading) { g = std::max(0.0f, (float) v.fadeOut / 96.0f); if (v.fadeOut > 0) --v.fadeOut; }
            const int o = offset + done + i, k = v.bufPos + i;
            dryL_[(size_t) o] += v.buf[0][k] * g; dryR_[(size_t) o] += v.buf[1][k] * g;
            sdL_[(size_t) o]  += v.buf[2][k] * g; sdR_[(size_t) o]  += v.buf[3][k] * g;
            srL_[(size_t) o]  += v.buf[4][k] * g; srR_[(size_t) o]  += v.buf[5][k] * g;
            smL_[(size_t) o]  += v.buf[6][k] * g; smR_[(size_t) o]  += v.buf[7][k] * g;
        }
        v.bufPos += take; done += take;
        if (v.fading && v.fadeOut <= 0) { v.active = false; break; }
        if (v.bufPos >= kControlRate && v.finished) { v.active = false; break; }
    }
}

void CandyEngine::renderSegment(int offset, int n)
{
    if (n <= 0) return;
    for (auto& v : voices_) if (v->active) renderVoice(*v, offset, n);
}

void CandyEngine::process(float* outL, float* outR, int n)
{
    std::fill(outL, outL + n, 0.0f); std::fill(outR, outR + n, 0.0f);
    processWithDry(outL, outR, n);
}

void CandyEngine::processWithDry(float* outL, float* outR, int n)
{
    n = std::min(n, maxBlock_);
    curBank_ = bank_.load(std::memory_order_acquire);
    for (auto* b : { &dryL_, &dryR_, &sdL_, &sdR_, &srL_, &srR_, &smL_, &smR_ }) std::fill(b->begin(), b->begin() + n, 0.0f);
    // sort queued events by sample offset (insertion sort; queue is small)
    for (int i = 1; i < queued_; ++i)
    {
        auto key = queue_[(size_t) i]; int j = i - 1;
        while (j >= 0 && queue_[(size_t) j].sampleOffset > key.sampleOffset) { queue_[(size_t) (j + 1)] = queue_[(size_t) j]; --j; }
        queue_[(size_t) (j + 1)] = key;
    }
    int pos = 0;
    for (int i = 0; i < queued_; ++i)
    {
        const int off = std::clamp(queue_[(size_t) i].sampleOffset, 0, n - 1);
        if (off > pos) { renderSegment(pos, off - pos); pos = off; }
        startVoice(queue_[(size_t) i]);
    }
    queued_ = 0;
    renderSegment(pos, n - pos);

    // ---- global modulation destinations: sum over the voices that are alive
    for (auto& g : gDest_) g = 0;
    for (auto& v : voices_) if (v->active) for (int d = 96; d < kNumDestinations; ++d) gDest_[d] += v->gDest[d];
    for (int d = 96; d < kNumDestinations; ++d) gDest_[d] = std::clamp(gDest_[d], -2.0f, 2.0f);

    // ---- global effects
    const FxConfig fx = curBank_ ? curBank_->fx : FxConfig();
    const float bpm = (float) std::max(20.0, host_.bpm);
    const float nv = (float) std::max(1, activeVoices());
    const float gDelayFb = gDest_[kGdDelayFeedback], gRevSize = gDest_[kGdReverbSize], gModDepth = gDest_[kGdModFxDepth];
    const float gWidth = gDest_[kGdWidth], gGain = gDest_[kGdMasterGain], gDelayTime = gDest_[kGdDelayTime];
    (void) nv;
    const float beatSamples = (float) (60.0 / bpm * sr_);
    const float delaySamples = (float) divisionBeats(fx.delayDiv, 0) * beatSamples * std::exp2(std::clamp(gDelayTime, -1.0f, 1.0f));
    delay_.setParams(delaySamples, fx.delayFeedback + 0.4f * gDelayFb, fx.delayTone, fx.pingPong);
    reverb_.setParams(fx.reverbSize + 0.5f * gRevSize, fx.reverbDamp);
    modfx_.setParams(fx.modType, expMap(fx.modRate, 0.05f, 8.0f), fx.modDepth + 0.5f * gModDepth, (fx.modFeedback - 0.5f) * 1.6f);

    auto quiet = [n](const std::vector<float>& a, const std::vector<float>& b) {
        float m = 0; for (int i = 0; i < n; ++i) m = std::max(m, std::max(std::abs(a[(size_t) i]), std::abs(b[(size_t) i]))); return m < 1.0e-7f; };
    auto runFx = [&](auto& fxu, const std::vector<float>& inL, const std::vector<float>& inR, float level, int& idle)
    {
        const bool inQuiet = quiet(inL, inR);
        if (inQuiet && idle > (int) (sr_ * 14.0)) return;               // fully decayed: skip processing
        fxu.process(inL.data(), inR.data(), fxL_.data(), fxR_.data(), n);
        float m = 0; for (int i = 0; i < n; ++i) m = std::max(m, std::max(std::abs(fxL_[(size_t) i]), std::abs(fxR_[(size_t) i])));
        idle = (inQuiet && m < 1.0e-6f) ? idle + n : 0;
        for (int i = 0; i < n; ++i) { dryL_[(size_t) i] += fxL_[(size_t) i] * level; dryR_[(size_t) i] += fxR_[(size_t) i] * level; }
    };
    if (! bypassSends_)
    {
        runFx(delay_, sdL_, sdR_, fx.delayLevel, idleDelay_);
        runFx(reverb_, srL_, srR_, fx.reverbLevel, idleReverb_);
        runFx(modfx_, smL_, smR_, fx.modLevel, idleMod_);
    }
    // conservative adaptive EQ (psychoacoustic awareness) - only when it actually cuts
    if (adEqOn_ && adEqDb_ < -0.05f)
    {
        if (std::abs(adEqHz_ - adEqLastHz_) > 0.5f || std::abs(adEqDb_ - adEqLastDb_) > 0.01f)
        {
            for (auto& b : adEq_) { const auto z1 = b.z1, z2 = b.z2; b.setPeak(sr_, adEqHz_, adEqQ_, adEqDb_); b.z1 = z1; b.z2 = z2; }
            adEqLastHz_ = adEqHz_; adEqLastDb_ = adEqDb_;
        }
        for (int i = 0; i < n; ++i) { dryL_[(size_t) i] = adEq_[0].process(dryL_[(size_t) i]); dryR_[(size_t) i] = adEq_[1].process(dryR_[(size_t) i]); }
    }

    const float width = ovr_ ? ovrWidth_ : fx.width + 0.5f * gWidth;
    const float gainDb = ovr_ ? ovrGain_ : fx.masterGainDb + 12.0f * gGain;
    master_.setParams(width * adWidth_, gainDb + adTrim_, std::max(fx.lowCutHz, adLowCut_));
    master_.process(dryL_.data(), dryR_.data(), n);
    for (int i = 0; i < n; ++i) { outL[i] += dryL_[(size_t) i]; outR[i] += dryR_[(size_t) i]; }
}

} // namespace dali
