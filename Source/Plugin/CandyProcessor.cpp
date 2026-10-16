#include "CandyProcessor.h"
#include "Sequencer/ContextBuilder.h"
#if DALI_WITH_EDITOR
 #include "UI/CandyEditor.h"
#endif
#include <chrono>

namespace dali
{
namespace
{
juce::File dataDir()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable("DALICANDY_DATA_DIR", {}); env.isNotEmpty()) return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Dali Audio").getChildFile("DaliCandy");
}
std::vector<float> waveThumb(const juce::AudioBuffer<float>& b, int points = 96)
{
    std::vector<float> w((size_t) points * 2, 0.0f); const int n = b.getNumSamples(); if (n <= 0) return w;
    for (int p = 0; p < points; ++p)
    {
        const int a = (int) ((int64_t) n * p / points), e = std::max(a + 1, (int) ((int64_t) n * (p + 1) / points));
        float lo = 0, hi = 0; for (int c = 0; c < b.getNumChannels(); ++c) { const float* d = b.getReadPointer(c); for (int i = a; i < e && i < n; ++i) { lo = std::min(lo, d[i]); hi = std::max(hi, d[i]); } }
        w[(size_t) p * 2] = lo; w[(size_t) p * 2 + 1] = hi;
    }
    return w;
}
}

//==============================================================================================================
CandyProcessor::CandyProcessor(bool isFx)
    : AudioProcessor(isFx ? BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
                          : BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DaliCandyParams", createParameterLayout(isFx)),
      isFx_(isFx), presets_(dataDir().getChildFile("Presets"))
{
    presets_.setFactoryProvider(embeddedFactoryPresets); presets_.rescan();
    taste_.loadFromFile(dataDir().getChildFile("taste.json"));
    Preset p;
    if (presets_.entries().empty() || ! presets_.load(0, p))
    {   // no factory bank (should not happen): generate one sound so that the plugin is never silent
        CandyGenerator g; CandyDNA d; GenSettings gs;
        for (uint32_t s = 1; s < 200; ++s) if (g.generateOne(gs, s, 44100.0, d)) break;
        p.setMain(d); p.name = "Init";
    }
    preset_ = p; history_.push(preset_.main());
    for (auto* id : { pid::seqOn, pid::seqMode, pid::seqBars, pid::seqDiv, pid::seqFeel, pid::seqSwing, pid::seqProb, pid::seqRatchetProb, pid::seqRatchetMax, pid::seqMicro,
                      pid::seqVel, pid::seqVelRange, pid::seqPitchVar, pid::seqLength, pid::euSteps, pid::euHits, pid::euRotate, pid::seqFreeze, pid::quality, pid::category })
        apvts.addParameterListener(id, this);
    preset_.seq.enabled = false;                                  // nothing plays on Play until a pattern has been built for this track
    applyParamsFromPreset(preset_);
    rebuildPatternNow(false);
    startTimerHz(30);
}

CandyProcessor::~CandyProcessor()
{
    alive_ = false; cancel_ = true; stopTimer(); cancelPendingUpdate();
    pool_.removeAllJobs(true, 8000);
    for (auto* id : { pid::seqOn, pid::seqMode, pid::seqBars, pid::seqDiv, pid::seqFeel, pid::seqSwing, pid::seqProb, pid::seqRatchetProb, pid::seqRatchetMax, pid::seqMicro,
                      pid::seqVel, pid::seqVelRange, pid::seqPitchVar, pid::seqLength, pid::euSteps, pid::euHits, pid::euRotate, pid::seqFreeze, pid::quality, pid::category })
        apvts.removeParameterListener(id, this);
}

juce::AudioProcessorEditor* CandyProcessor::createEditor()
{
#if DALI_WITH_EDITOR
    return new ui::CandyEditor(*this);
#else
    return nullptr;
#endif
}

bool CandyProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (! isFx_) return l.inputBuses.isEmpty() || l.getMainInputChannelSet().isDisabled();
    // unified plug-in: the main input is optional (instrument use has no audio input), stereo when present
    if (! l.getMainInputChannelSet().isDisabled() && l.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 1) { const auto sc = l.inputBuses[1]; if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false; }
    return true;
}

//==============================================================================================================
// audio thread
//==============================================================================================================
void CandyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate; maxBlock_ = std::max(samplesPerBlock, 512);
    const auto q = (QualityMode) std::clamp((int) apvts.getRawParameterValue(pid::quality)->load(), 0, 2);
    engine_.prepare(sr_, maxBlock_, q);
    live_.prepare(sr_); live_.reset();
    candyL_.assign((size_t) maxBlock_, 0.0f); candyR_.assign((size_t) maxBlock_, 0.0f);
    scL_.assign((size_t) maxBlock_, 0.0f); scR_.assign((size_t) maxBlock_, 0.0f);
    freePpq_ = 0.0; wasPlaying_ = false; seq_.resetPosition();
    lastWet_ = apvts.getRawParameterValue(pid::wet)->load() * 0.01f; lastGain_ = dbToGain(apvts.getRawParameterValue(pid::outGain)->load());
    lastDry_ = isFx_ ? apvts.getRawParameterValue(pid::dry)->load() * 0.01f : 0.0f;
    rebuildBankNow();
    prepared_ = true;
}

void CandyProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = std::chrono::steady_clock::now();
    const int n = buffer.getNumSamples();
    auto outBus = getBusBuffer(buffer, false, 0);
    if (! prepared_ || n <= 0 || outBus.getNumChannels() < 2)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
        return;
    }
    float* outL = outBus.getWritePointer(0); float* outR = outBus.getWritePointer(1);

    // The unified plug-in works as an effect while its main audio input is connected and as an instrument otherwise.
    const bool inputOn = isFx_ && getBusCount(true) > 0 && getBus(true, 0) != nullptr && getBus(true, 0)->isEnabled() && getBus(true, 0)->getNumberOfChannels() >= 2;

    // ---- analysis source (effect mode) ----------------------------------------------------------------------
    const float* aL = nullptr; const float* aR = nullptr;
    if (inputOn)
    {
        bool useSc = apvts.getRawParameterValue(pid::anaSource)->load() > 0.5f && getBusCount(true) > 1 && getBus(true, 1) != nullptr && getBus(true, 1)->isEnabled();
        if (useSc) { auto sc = getBusBuffer(buffer, true, 1); aL = sc.getReadPointer(0); aR = sc.getNumChannels() > 1 ? sc.getReadPointer(1) : aL; }
        else { aL = outL; aR = outR; }
        live_.process(aL, aR, n);
    }

    // ---- host / transport -----------------------------------------------------------------------------------------
    HostInfo host; double bpm = apvts.getRawParameterValue(pid::anaBpm)->load(); bool hostPpq = false, hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit(20.0, 400.0, *b);
            if (auto p = pos->getPpqPosition()) { host.ppq = *p; hostPpq = true; }
            hostPlaying = pos->getIsPlaying();
        }
    const bool freeRun = apvts.getRawParameterValue(pid::seqFreeRun)->load() > 0.5f;
    host.bpm = bpm; hostBpm_.store(bpm, std::memory_order_relaxed);
    engine_.setDrift(apvts.getRawParameterValue(pid::drift)->load() * 0.01f);
    const bool useHost = hostPpq && hostPlaying;
    if (useHost) { host.playing = true; freePpq_ = host.ppq; }
    else if (freeRun) { host.playing = true; host.ppq = freePpq_; }
    else { host.playing = false; host.ppq = freePpq_; }
    if (host.playing && ! wasPlaying_) seq_.resetPosition();
    wasPlaying_ = host.playing;
    const double beatsPerSample = bpm / 60.0 / sr_;
    uiPlayhead_.store(host.ppq, std::memory_order_relaxed); uiPlaying_.store(host.playing, std::memory_order_relaxed);

    // ---- capture (FX: records the analysis source, aligned to the next bar line) ------------------------------------------
    if (auto* cap = captureRaw_.load(std::memory_order_acquire); cap != nullptr && aL != nullptr)
    {
        int st = cap->state.load(std::memory_order_relaxed); int startAt = 0;
        if (st == 1)
        {
            if (hostPpq && ! hostPlaying) { /* armed: wait until the host transport runs, otherwise we would record silence */ }
            else if (! useHost) { st = 2; cap->state.store(2); cap->written.store(0); }
            else
            {
                const double bpb = (double) cap->beatsPerBar, inBar = std::fmod(host.ppq, bpb);
                if (inBar < 1.0e-9) { st = 2; startAt = 0; }
                else { const double toBoundary = bpb - inBar; if (toBoundary < (double) n * beatsPerSample) { st = 2; startAt = (int) std::ceil(toBoundary / beatsPerSample); } }
                if (st == 2) { cap->state.store(2); cap->written.store(0); cap->bpm = bpm; }
            }
        }
        if (st == 2)
        {
            int w = cap->written.load(std::memory_order_relaxed);
            for (int i = startAt; i < n && w < cap->capacity; ++i) cap->mono[(size_t) w++] = 0.5f * (aL[i] + aR[i]);
            cap->written.store(w, std::memory_order_release);
            if (w >= cap->capacity) cap->state.store(3, std::memory_order_release);
        }
    }

    // ---- live negative space ------------------------------------------------------------------------------------------
    {
        float omit = 0.0f;
        if (inputOn && apvts.getRawParameterValue(pid::liveNeg)->load() > 0.5f && live_.rms() > 0.002f)
        {
            NegSpaceContext c; c.eventDensity = clamp01(live_.onsetDensity() / 8.0f); c.overlap = clamp01(live_.follower() * 1.4f); c.transient = live_.transient();
            c.competition = clamp01(live_.mid() * 0.7f + live_.high() * 0.5f); c.maskingRisk = clamp01(0.5f * c.competition + 0.5f * c.overlap);
            omit = NegativeSpace::omitProbability(NegativeSpace::clutter(c), apvts.getRawParameterValue(pid::negSpace)->load() * 0.01f, apvts.getRawParameterValue(pid::density)->load() * 0.01f);
        }
        seq_.setLiveOmit(omit);
    }

    // ---- engine control -----------------------------------------------------------------------------------------------------
    float macros[kNumMacros] = { apvts.getRawParameterValue(pid::macro1)->load() * 0.01f, apvts.getRawParameterValue(pid::macro2)->load() * 0.01f,
                                 apvts.getRawParameterValue(pid::macro3)->load() * 0.01f, apvts.getRawParameterValue(pid::macro4)->load() * 0.01f };
    engine_.setPerformance(modWheel_, aftertouch_, inputOn ? live_.follower() : 0.0f, inputOn ? live_.transient() : 0.0f, macros);
    {
        const float adaptMix = inputOn ? apvts.getRawParameterValue(pid::adapt)->load() * 0.01f : 0.0f;
        engine_.setAdaptive(adaptive_.lowCut.load(), adaptive_.eqOn.load() && adaptMix > 0.01f, adaptive_.eqHz.load(), adaptive_.eqDb.load() * adaptMix, adaptive_.eqQ.load(),
                            lerp(1.0f, adaptive_.width.load(), adaptMix), adaptive_.trim.load() * adaptMix);
    }
    const bool seqOn = apvts.getRawParameterValue(pid::seqOn)->load() > 0.5f;
    const int audit = auditionSlot_.exchange(-1);

    keyboardState.processNextMidiBuffer(midi, 0, n, true);        // on-screen keyboard (no-op without a GUI)
    // ---- render in chunks ----------------------------------------------------------------------------------------------------
    for (int start = 0; start < n; start += maxBlock_)
    {
        const int cn = std::min(maxBlock_, n - start);
        HostInfo hc = host; hc.ppq = host.ppq + (double) start * beatsPerSample; engine_.setHost(hc);
        if (start == 0 && audit >= 0) { EventRequest e; e.sampleOffset = 0; e.slot = audit; e.velocity = auditionVel_.load(); e.midiNote = 60.0f; e.eventSeed = hashCombine(0xA0D17u, ++noteCounter_); engine_.queueEvent(e); }
        {
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage(); const int pos = meta.samplePosition;
                if (pos < start || pos >= start + cn) continue;
                if (m.isNoteOn())
                {
                    EventRequest e; e.sampleOffset = pos - start; e.slot = juce::jlimit(0, kNumSoundSlots - 1, m.getChannel() - 1);
                    e.velocity = m.getFloatVelocity(); e.midiNote = (float) m.getNoteNumber(); e.pitchOffset = (float) (m.getNoteNumber() - 60);
                    e.eventSeed = hashCombine((uint32_t) m.getNoteNumber(), ++noteCounter_); engine_.queueEvent(e);
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff()) engine_.noteOffAll();
                else if (m.isControllerOfType(1)) modWheel_ = (float) m.getControllerValue() / 127.0f;
                else if (m.isChannelPressure()) aftertouch_ = (float) m.getChannelPressureValue() / 127.0f;
                else if (m.isAftertouch()) aftertouch_ = (float) m.getAfterTouchValue() / 127.0f;
            }
        }
        if (seqOn && hc.playing) seq_.render(hc, sr_, cn, engine_);
        engine_.process(candyL_.data() + 0, candyR_.data() + 0, cn);
        // mix
        const float wet = apvts.getRawParameterValue(pid::wet)->load() * 0.01f;
        const float gain = dbToGain(apvts.getRawParameterValue(pid::outGain)->load());
        const float dry = inputOn ? apvts.getRawParameterValue(pid::dry)->load() * 0.01f : 0.0f;
        const float w0 = lastWet_ * lastGain_, w1 = wet * gain;
        for (int i = 0; i < cn; ++i)
        {
            const float t = (float) (i + 1) / (float) cn;
            const float wg = w0 + (w1 - w0) * t, dg = lastDry_ + (dry - lastDry_) * t;
            const float cl = sanitize(candyL_[(size_t) i]), cr = sanitize(candyR_[(size_t) i]);
            outL[start + i] = (inputOn ? outL[start + i] * dg : 0.0f) + cl * wg;
            outR[start + i] = (inputOn ? outR[start + i] * dg : 0.0f) + cr * wg;
        }
        lastWet_ = wet; lastGain_ = gain; lastDry_ = dry;
    }
    for (int c = 2; c < outBus.getNumChannels(); ++c) outBus.clear(c, 0, n);
    if (! inputOn) live_.process(outL, outR, n);

    float pk = 0; for (int i = 0; i < n; ++i) pk = std::max(pk, std::max(std::abs(outL[i]), std::abs(outR[i])));
    outPeak_.store(std::max(pk, outPeak_.load() * 0.92f), std::memory_order_relaxed);
    voices_.store(engine_.activeVoices(), std::memory_order_relaxed);
    protect_.store(engine_.master().protectCount(), std::memory_order_relaxed);
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    cpu_.store(cpu_.load() * 0.9f + 0.1f * (float) (el / ((double) n / sr_)), std::memory_order_relaxed);
    if (! useHost && freeRun) freePpq_ += (double) n * beatsPerSample;
}

//==============================================================================================================
// state
//==============================================================================================================
void CandyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    syncParamsToPreset();
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml == nullptr) return;
    xml->setAttribute("daliVersion", 1); xml->setAttribute("product", isFx_ ? "fx" : "instrument");
    xml->setAttribute("genCounter", (int) genCounter_);
    { const juce::ScopedLock sl(lock_); xml->createNewChildElement("Preset")->addTextElement(preset_.toJson()); }
    xml->createNewChildElement("UI")->addTextElement(juce::JSON::toString(uiState, true));
    copyXmlToBinary(*xml, dest);
}

void CandyProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType())) return;          // unknown / corrupt data: keep current state
    Preset p; bool havePreset = false;
    if (auto* pe = xml->getChildByName("Preset")) havePreset = Preset::fromJson(pe->getAllSubText(), p);
    genCounter_ = (uint32_t) std::max(1, xml->getIntAttribute("genCounter", 1));
    if (auto* ue = xml->getChildByName("UI")) uiState = juce::JSON::parse(ue->getAllSubText());
    xml->deleteAllChildElementsWithTagName("Preset"); xml->deleteAllChildElementsWithTagName("UI");
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    if (havePreset)
    {
        { const juce::ScopedLock sl(lock_); preset_ = p; history_.push(preset_.main()); }
        rebuildBankNow(); rebuildPatternNow(false);
    }
    postToMessageThread([this] { flagChange(kChSound | kChPattern | kChPreset | kChParamsLoaded); });
}

void CandyProcessor::syncParamsToPreset()
{
    const juce::ScopedLock sl(lock_);
    preset_.gen = genSettingsFrom(apvts);
    preset_.seq = seqSettingsFrom(apvts, preset_.seq);
    preset_.macros = { apvts.getRawParameterValue(pid::macro1)->load() * 0.01f, apvts.getRawParameterValue(pid::macro2)->load() * 0.01f,
                       apvts.getRawParameterValue(pid::macro3)->load() * 0.01f, apvts.getRawParameterValue(pid::macro4)->load() * 0.01f };
}

static void setParam(juce::AudioProcessorValueTreeState& a, const char* id, float plain)
{
    if (auto* p = a.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(plain));
}

void CandyProcessor::applyParamsFromPreset(const Preset& p)
{
    using namespace pid;
    const auto& g = p.gen;
    setParam(apvts, complexity, g.complexity * 100.0f); setParam(apvts, motion, g.motion * 100.0f); setParam(apvts, space, g.space * 100.0f);
    setParam(apvts, density, g.density * 100.0f); setParam(apvts, chaos, g.chaos * 100.0f); setParam(apvts, brightness, g.brightness * 100.0f);
    setParam(apvts, musicality, g.musicality * 100.0f); setParam(apvts, lengthMode, (float) g.lengthMode); setParam(apvts, lengthMs, g.customLengthMs);
    setParam(apvts, category, (float) (int) g.category); setParam(apvts, negSpace, g.negSpaceSensitivity * 100.0f);
    setParam(apvts, rootNote, (float) g.rootNote); setParam(apvts, scale, (float) g.scale);
    if (g.familyA >= 0) { setParam(apvts, famA, (float) (g.familyA + 1)); setParam(apvts, famB, (float) (g.familyB + 1)); setParam(apvts, spice, g.spice * 100.0f); }   // presets from before the Kitchen keep the current selection
    const auto& s = p.seq;
    setParam(apvts, seqMode, (float) (int) s.mode); setParam(apvts, seqBars, (float) s.bars);
    setParam(apvts, seqDiv, (float) s.division); setParam(apvts, seqFeel, (float) s.feel); setParam(apvts, seqSwing, s.swing * 100.0f);
    setParam(apvts, seqProb, s.probability * 100.0f); setParam(apvts, seqRatchetProb, s.ratchetProb * 100.0f); setParam(apvts, seqRatchetMax, (float) s.ratchetMax);
    setParam(apvts, seqMicro, s.microtimingMs); setParam(apvts, seqVel, s.velBase * 100.0f); setParam(apvts, seqVelRange, s.velRange * 100.0f);
    setParam(apvts, seqPitchVar, s.pitchVariation); setParam(apvts, seqLength, s.lengthBeats); setParam(apvts, euSteps, (float) s.euclidSteps);
    setParam(apvts, euHits, (float) s.euclidHits); setParam(apvts, euRotate, (float) s.euclidRotate); setParam(apvts, seqEvolve, s.evolveAmount * 100.0f);
    setParam(apvts, seqFreeze, s.freezeAll ? 1.0f : 0.0f);
    for (int i = 0; i < kNumMacros; ++i) setParam(apvts, (std::string("macro") + std::to_string(i + 1)).c_str(), p.macros[(size_t) i] * 100.0f);
    seqDirty_ = false;       // loading a preset restores its stored events instead of re-planning
}

//==============================================================================================================
// model access
//==============================================================================================================
Preset CandyProcessor::presetSnapshot() const { const juce::ScopedLock sl(lock_); return preset_; }
CandyDNA CandyProcessor::mainSound() const { const juce::ScopedLock sl(lock_); return preset_.main(); }
SeqConfig CandyProcessor::seqConfig() const { const juce::ScopedLock sl(lock_); return preset_.seq; }
int CandyProcessor::mainSlot() const { const juce::ScopedLock sl(lock_); return preset_.mainSlot; }
std::shared_ptr<const SeqPattern> CandyProcessor::pattern() const { const juce::ScopedLock sl(lock_); return pattern_; }
std::shared_ptr<const TrackAnalysis> CandyProcessor::analysis() const { const juce::ScopedLock sl(lock_); return analysis_; }
MaskingReport CandyProcessor::masking() const { const juce::ScopedLock sl(lock_); return masking_; }
std::vector<CandidateView> CandyProcessor::candidates() const { const juce::ScopedLock sl(lock_); return candidates_; }
juce::String CandyProcessor::statusText() const { const juce::ScopedLock sl(lock_); return status_; }
juce::String CandyProcessor::lastExportMessage() const { const juce::ScopedLock sl(lock_); return lastExport_; }

Preset CandyProcessor::currentAsPreset() { syncParamsToPreset(); return presetSnapshot(); }

void CandyProcessor::pushHistory(const CandyDNA& d) { history_.push(d); }

void CandyProcessor::commitMain(const CandyDNA& d, bool toHistory)
{
    { const juce::ScopedLock sl(lock_);
      auto& slotDna = preset_.slots[(size_t) preset_.mainSlot]; slotDna = d; preset_.slotUsed[(size_t) preset_.mainSlot] = true;
      if (toHistory) pushHistory(d); }
    scheduleBankRebuild(); flagChange(kChSound);
}

void CandyProcessor::setMainSound(const CandyDNA& d, bool toHistory) { commitMain(d, toHistory); rebuildBankNow(); }

void CandyProcessor::editMain(const std::function<void(CandyDNA&)>& fn, bool rebuildNow)
{
    { const juce::ScopedLock sl(lock_); fn(preset_.slots[(size_t) preset_.mainSlot]); preset_.slotUsed[(size_t) preset_.mainSlot] = true;
      for (auto& l : preset_.main().layers) { l.durationMs = std::max(1.0f, l.durationMs); }
      preset_.main().numLayers = 0; for (int i = 0; i < kMaxLayers; ++i) if (preset_.main().layers[(size_t) i].active) preset_.main().numLayers = i + 1;
      preset_.main().numLayers = std::max(1, preset_.main().numLayers); }
    if (rebuildNow) rebuildBankNow(); else scheduleBankRebuild();
    flagChange(kChSound);
}

void CandyProcessor::setSlotSound(int slot, const CandyDNA* d)
{
    slot = std::clamp(slot, 0, kNumSoundSlots - 1);
    { const juce::ScopedLock sl(lock_);
      if (d != nullptr) { preset_.slots[(size_t) slot] = *d; preset_.slotUsed[(size_t) slot] = true; }
      else if (slot != preset_.mainSlot) preset_.slotUsed[(size_t) slot] = false; }
    rebuildBankNow(); flagChange(kChSound | kChPattern);
}

void CandyProcessor::setMainSlot(int slot)
{
    { const juce::ScopedLock sl(lock_); slot = std::clamp(slot, 0, kNumSoundSlots - 1); if (! preset_.slotUsed[(size_t) slot]) { preset_.slots[(size_t) slot] = preset_.main(); preset_.slotUsed[(size_t) slot] = true; }
      preset_.mainSlot = slot; }
    flagChange(kChSound);
}

void CandyProcessor::loadPreset(const Preset& p)
{
    { const juce::ScopedLock sl(lock_); preset_ = p; history_.push(preset_.main()); candidates_.clear(); }
    applyParamsFromPreset(p);
    rebuildBankNow(); rebuildPatternNow(false);
    flagChange(kChSound | kChPattern | kChPreset | kChCandidates | kChParamsLoaded);
    audition();
}

void CandyProcessor::setPresetInfo(const juce::String& name, const juce::StringArray& tags, const juce::String& author)
{
    const juce::ScopedLock sl(lock_); preset_.name = name; preset_.tags = tags; if (author.isNotEmpty()) preset_.author = author;
}

void CandyProcessor::audition(int slot, float velocity) { auditionVel_ = velocity; auditionSlot_ = std::clamp(slot, 0, kNumSoundSlots - 1); }

void CandyProcessor::rate(int signal)
{
    CandyDNA d = mainSound();
    taste_.rate(d, signal);
    { const juce::ScopedLock sl(lock_);
      auto& m = preset_.main().meta; if (signal > 0) { m.rating = std::min(5, m.rating + 1); if (signal >= 2) m.favorite = true; } else { m.rating = std::max(-5, m.rating - 1); m.rejected = m.rating <= -2; } }
    taste_.saveToFile(dataDir().getChildFile("taste.json"));
    flagChange(kChTaste | kChSound);
}

void CandyProcessor::resetStats() { engine_.master().resetStats(); protect_ = 0; }

//==============================================================================================================
// bank / pattern publication
//==============================================================================================================
void CandyProcessor::rebuildBankNow()
{
    Preset p; { const juce::ScopedLock sl(lock_); p = preset_; }
    auto bank = buildSoundBank(p.slotPointers(), sr_);
    engine_.publishBank(bank);
    bankDirty_ = false;
}

void CandyProcessor::scheduleBankRebuild() { bankDirty_ = true; bankDirtyAt_ = juce::Time::getMillisecondCounterHiRes(); }

SeqContext CandyProcessor::seqContext() const
{
    SeqContext c; { const juce::ScopedLock sl(lock_);
      int used = 0; for (bool u : preset_.slotUsed) used += u ? 1 : 0; c.numSlots = std::max(1, used);
      c.analysis = analysis_.get(); if (analysis_ != nullptr) c.bpm = analysis_->bpm;
      auto cands = preset_.main().spectral.bands; for (size_t i = 0; i < cands.size(); ++i) c.candyBands[i] = cands[i]; }
    const auto g = genSettingsFrom(const_cast<juce::AudioProcessorValueTreeState&>(apvts));
    c.density = g.density; c.chaos = g.chaos; c.negSpaceSensitivity = g.negSpaceSensitivity;
    return c;
}

void CandyProcessor::rebuildPatternNow(bool replan)
{
    SeqConfig cfg;
    { const juce::ScopedLock sl(lock_); preset_.seq = seqSettingsFrom(apvts, preset_.seq); cfg = preset_.seq; }
    auto ctx = seqContext();
    auto pat = makePattern(cfg, ctx, replan);
    { const juce::ScopedLock sl(lock_); preset_.seq.events = pat->events; pattern_ = pat; }
    seq_.publish(pat);
    flagChange(kChPattern);
}

void CandyProcessor::parameterChanged(const juce::String& id, float)
{
    // may be called from any thread (host automation): only set flags
    if (id == pid::quality) { qualityDirty_ = true; return; }
    if (id == pid::seqOn) return;
    seqDirty_ = true; seqDirtyAt_ = juce::Time::getMillisecondCounterHiRes();
}

void CandyProcessor::editSeq(const std::function<void(SeqConfig&)>& fn)
{
    { const juce::ScopedLock sl(lock_); fn(preset_.seq); }
    rebuildPatternNow(false);
}

void CandyProcessor::regeneratePattern(bool newSeed)
{
    { const juce::ScopedLock sl(lock_); if (newSeed) preset_.seq.seed = hashCombine(preset_.seq.seed, ++patternSeedCounter_); }
    rebuildPatternNow(true);
}

void CandyProcessor::evolvePattern()
{
    SeqConfig cfg; { const juce::ScopedLock sl(lock_); preset_.seq = seqSettingsFrom(apvts, preset_.seq); cfg = preset_.seq; }
    auto ev = evolveEvents(cfg, seqContext(), ++patternSeedCounter_);
    { const juce::ScopedLock sl(lock_); preset_.seq.events = ev; }
    rebuildPatternNow(false);
}

void CandyProcessor::toggleEventFrozen(size_t index)
{
    { const juce::ScopedLock sl(lock_); if (index < preset_.seq.events.size()) { auto& e = preset_.seq.events[index]; e.frozen = ! e.frozen; } }
    rebuildPatternNow(false);
}

//==============================================================================================================
// message-thread plumbing
//==============================================================================================================
void CandyProcessor::postToMessageThread(std::function<void()> fn)
{
    { const juce::ScopedLock sl(mailLock_); mail_.push_back({ std::move(fn) }); }
    if (alive_) triggerAsyncUpdate();
}

void CandyProcessor::handleAsyncUpdate()
{
    std::deque<MailItem> items;
    { const juce::ScopedLock sl(mailLock_); items.swap(mail_); }
    for (auto& it : items) if (alive_ && it.apply) it.apply();
    if (auto cap = capture_; cap != nullptr && ! captureHandled_)
    {
        const int st = cap->state.load();
        if (st == 1 || st == 2)
        {
            const int pct = st == 1 ? -1 : (int) (100.0f * (float) cap->written.load() / (float) std::max(1, cap->capacity)) / 5 * 5;
            if (pct != lastListenPct_)
            {
                lastListenPct_ = pct;
                { const juce::ScopedLock sl(lock_); status_ = st == 1 ? juce::String("LISTEN armed: press Play in the host (starts on the next bar line)...") : "Listening to the track... " + juce::String(pct) + "% (" + juce::String(cap->bars) + " bars)"; }
                flagChange(kChStatus);
            }
        }
    }
    if (pendingFlags_ != 0) { uiFlags_ |= pendingFlags_; sendChangeMessage(); }
}

void CandyProcessor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (bankDirty_ && now - bankDirtyAt_ > 120.0) rebuildBankNow();
    if (seqDirty_ && now - seqDirtyAt_ > 150.0) { seqDirty_ = false; rebuildPatternNow(true); }
    if (qualityDirty_.exchange(false) && prepared_)
    {
        suspendProcessing(true); prepareToPlay(sr_, maxBlock_); suspendProcessing(false);
    }
    if (auto cap = capture_; cap != nullptr && ! captureHandled_ && cap->state.load() == 3)
    {
        captureHandled_ = true; captureRaw_ = nullptr;
        std::vector<float> mono(cap->mono.begin(), cap->mono.begin() + cap->capacity);
        float peak = 0.0f; for (float v : mono) peak = std::max(peak, std::abs(v));
        if (peak < 0.003f)
        {   // nothing came in: do not "analyse" silence (that would call every step a gap and fill all of it)
            listenPending_ = false;
            { const juce::ScopedLock sl(lock_); status_ = "LISTEN heard silence. Put DaliCandy as an effect on the track (audio input), press Play, or use Import WAV in Track analysis."; }
            flagChange(kChStatus);
        }
        else
        beginAnalysis(std::move(mono), cap->sampleRate, cap->bpm, cap->beatsPerBar);
    }
    if (pendingFlags_ != 0) { uiFlags_ |= pendingFlags_; sendChangeMessage(); pendingFlags_ = 0; }
    updateAdaptive();
}

void CandyProcessor::runJob(const juce::String& label, std::function<void()> job)
{
    { const juce::ScopedLock sl(lock_); status_ = label; }
    flagChange(kChStatus);
    ++busyJobs_; cancel_ = false; progress_ = 0;
    pool_.addJob([this, job = std::move(job)]
    {
        if (alive_) job();
        --busyJobs_;
        postToMessageThread([this] { const juce::ScopedLock sl(lock_); if (busyJobs_.load() == 0 && status_ != "Cancelled") {} flagChange(kChStatus); });
    });
}

void CandyProcessor::cancelWork() { cancel_ = true; { const juce::ScopedLock sl(lock_); status_ = "Cancelled"; } flagChange(kChStatus); }

//==============================================================================================================
// generation & DNA operations
//==============================================================================================================
double CandyProcessor::syncWindowMs() const
{
    const double beats = lenSyncBeats((int) apvts.getRawParameterValue(pid::lenSync)->load());
    return beats > 0.0 ? beats * 60000.0 / std::max(20.0, hostBpm_.load()) : 0.0;
}

GenSettings CandyProcessor::kitchenSettings(int* gapBand)
{
    auto gs = genSettingsFrom(apvts, hostBpm_.load());
    if (gapBand) *gapBand = -1;
    if (apvts.getRawParameterValue(pid::listenOn)->load() > 0.5f)
        if (auto a = analysis(); a != nullptr && a->valid)
        {   // aim the sound at the quietest band (400 Hz - 12 kHz) of the analysed track
            int best = -1; float lowest = 1.0e9f;
            for (int b = 3; b <= 6; ++b) if (a->bandDb[(size_t) b] < lowest) { lowest = a->bandDb[(size_t) b]; best = b; }
            if (best >= 0)
            {
                gs.brightness = std::clamp(std::log2(bandCentreHz(best) / 500.0f) / 5.0f, 0.05f, 0.95f);
                if (gapBand) *gapBand = best;
            }
        }
    return gs;
}

void CandyProcessor::generate()
{
    int gap = -1;
    const auto gs = kitchenSettings(&gap); const uint32_t seed = hashCombine(0xDA11u, genCounter_++); const double sr = std::max(8000.0, sr_);
    const bool keep = apvts.getRawParameterValue(pid::keepSkel)->load() > 0.5f;
    CandyDNA base = mainSound(); const bool haveBase = keep && base.activeLayerCount() > 0;
    TasteModel tasteCopy = taste_;
    const juce::String how = haveBase ? "variations" : "dishes";
    const juce::String aim = gap >= 0 ? juce::String(" aimed at ") + juce::String((int) bandCentreHz(gap)) + " Hz" : juce::String();
    runJob(haveBase ? "Cooking variations..." : "Generating candy...", [this, gs, seed, sr, tasteCopy, haveBase, base, how, aim]() mutable
    {
        CandyGenerator gen(&tasteCopy); GenerateOptions o; o.count = 8; o.oversample = 3; o.sampleRate = std::min(sr, 48000.0);
        o.cancelled = [this] { return cancel_.load() || ! alive_; }; o.progress = [this](float p) { progress_ = p; };
        GenerateStats st; std::vector<CandyDNA> batch;
        if (haveBase) { batch = gen.variations(base, seed, 8, 0.28f, gs, o.sampleRate); st.tried = 8; st.accepted = (int) batch.size(); st.rejected = 8 - st.accepted; }
        else batch = gen.generateBatch(gs, seed, o, &st);
        std::vector<CandidateView> views; OfflineRenderer r;
        for (auto& d : batch)
        {
            if (cancel_ || ! alive_) return;
            RenderOptions ro; ro.sampleRate = 44100.0; ro.quality = QualityMode::Eco; ro.trimSilence = true;
            CandidateView v; v.dna = d; v.wave = waveThumb(r.renderOneShot(d, ro)); views.push_back(std::move(v));
        }
        postToMessageThread([this, views = std::move(views), st, how, aim]() mutable
        {
            int n = 0;
            { const juce::ScopedLock sl(lock_); candidates_ = std::move(views); n = (int) candidates_.size();
              status_ = juce::String(n) + " " + how + aim + " (" + juce::String(st.rejected) + " rejected of " + juce::String(st.tried) + " tried)"; }
            flagChange(kChCandidates | kChStatus);
            if (n > 0) platesToPalette();
            if (rollPending_.exchange(false) && n > 0) selectCandidate((int) (rollPick_ % (uint32_t) n));
            if (n > 0 && (buildAfterCook_.exchange(false) || (analysis() != nullptr && apvts.getRawParameterValue(pid::seqOn)->load() > 0.5f))) applySuggestionsToSequence();
        });
    });
}

void CandyProcessor::roll() { rollWith(hashCombine(juce::Time::getMillisecondCounter(), genCounter_ * 2654435761u)); }

void CandyProcessor::rollWith(uint32_t seed)
{
    Rng r(seed);
    auto set = [&](const char* id, float plain) { setParam(apvts, id, plain); };
    const int a = r.rangeInt(0, kNumFamilies - 1); int b = -1;
    if (r.chance(0.6f)) { b = r.rangeInt(0, kNumFamilies - 2); if (b >= a) ++b; }
    set(pid::famA, (float) (a + 1)); set(pid::famB, (float) (b + 1));
    set(pid::keepSkel, 0.0f); set(pid::listenOn, 0.0f);
    set(pid::spice, r.range(5.0f, 100.0f)); set(pid::complexity, r.range(25.0f, 95.0f)); set(pid::motion, r.range(20.0f, 100.0f));
    set(pid::brightness, r.range(15.0f, 90.0f)); set(pid::chaos, r.range(10.0f, 90.0f)); set(pid::density, r.range(15.0f, 70.0f));
    rollPick_ = r.next(); rollPending_ = true;
    generate();
}

void CandyProcessor::listen()
{
    if (getTotalNumInputChannels() == 0)
    {
        { const juce::ScopedLock sl(lock_); status_ = "LISTEN needs an audio input. Use DaliCandy as an effect on the track, or Import WAV in Track analysis."; }
        flagChange(kChStatus); return;
    }
    setParam(apvts, pid::listenOn, 1.0f);
    listenPending_ = true; lastListenPct_ = -2;
    startCapture();
}

void CandyProcessor::platesToPalette()
{
    {   const juce::ScopedLock sl(lock_);
        const int n = std::min((int) candidates_.size(), kNumSoundSlots);
        for (int i = 0; i < kNumSoundSlots; ++i)
        {
            if (i < n) { preset_.slots[(size_t) i] = candidates_[(size_t) i].dna; preset_.slotUsed[(size_t) i] = true; }
            else preset_.slotUsed[(size_t) i] = false;
        }
        preset_.mainSlot = 0; if (n > 0) history_.push(preset_.slots[0]);
    }
    rebuildBankNow(); flagChange(kChSound | kChPattern);
}

void CandyProcessor::selectCandidate(int index)
{
    CandyDNA d; bool inPalette = false;
    {   const juce::ScopedLock sl(lock_); if (index < 0 || index >= (int) candidates_.size()) return; d = candidates_[(size_t) index].dna;
        inPalette = index < kNumSoundSlots && preset_.slotUsed[(size_t) index] && preset_.slots[(size_t) index].id == d.id;
        if (inPalette) { preset_.mainSlot = index; history_.push(d); } }
    if (inPalette) { flagChange(kChSound); audition(index); return; }       // the plate already lives in its slot: just make it the current one
    setMainSound(d, true); audition(mainSlot());
}

void CandyProcessor::candidateToSlot(int index, int slot)
{
    CandyDNA d; { const juce::ScopedLock sl(lock_); if (index < 0 || index >= (int) candidates_.size()) return; d = candidates_[(size_t) index].dna; }
    setSlotSound(slot, &d); rebuildPatternNow(false);
}

void CandyProcessor::mutateMain(float amount)
{
    const auto d = mainSound(); const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(d.seed, genCounter_++); const double sr = std::min(48000.0, std::max(8000.0, sr_));
    runJob("Mutating...", [this, d, gs, seed, amount, sr]
    {
        CandyGenerator gen; for (int tries = 0; tries < 6 && ! cancel_ && alive_; ++tries)
        {
            auto m = gen.mutate(d, hashCombine(seed, (uint32_t) tries), amount, gs);
            if (gen.analyseAndFinish(m, gs, sr)) { postToMessageThread([this, m] { setMainSound(m, true); audition(); { const juce::ScopedLock sl(lock_); status_ = "Mutated"; } flagChange(kChStatus); }); return; }
        }
        postToMessageThread([this] { { const juce::ScopedLock sl(lock_); status_ = "Mutation produced no valid sound - try a smaller amount"; } flagChange(kChStatus); });
    });
}

void CandyProcessor::evolveMain(float amount)
{
    const auto d = mainSound(); const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(d.seed, genCounter_++); const double sr = std::min(48000.0, std::max(8000.0, sr_));
    runJob("Evolving...", [this, d, gs, seed, amount, sr]
    {
        CandyGenerator gen; for (int tries = 0; tries < 6 && ! cancel_ && alive_; ++tries)
        {
            auto m = gen.evolve(d, hashCombine(seed, (uint32_t) tries), amount, gs);
            if (gen.analyseAndFinish(m, gs, sr)) { postToMessageThread([this, m] { setMainSound(m, true); audition(); { const juce::ScopedLock sl(lock_); status_ = "Evolved"; } flagChange(kChStatus); }); return; }
        }
        postToMessageThread([this] { { const juce::ScopedLock sl(lock_); status_ = "Evolution produced no valid sound"; } flagChange(kChStatus); });
    });
}

void CandyProcessor::breedMainWith(const CandyDNA& other)
{
    const auto d = mainSound(); const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(d.seed ^ (uint32_t) other.seed, genCounter_++); const double sr = std::min(48000.0, std::max(8000.0, sr_));
    runJob("Breeding...", [this, d, other, gs, seed, sr]
    {
        CandyGenerator gen; for (int tries = 0; tries < 8 && ! cancel_ && alive_; ++tries)
        {
            auto m = gen.breed(d, other, hashCombine(seed, (uint32_t) tries), gs);
            if (gen.analyseAndFinish(m, gs, sr)) { postToMessageThread([this, m] { setMainSound(m, true); audition(); { const juce::ScopedLock sl(lock_); status_ = "Bred a new candy"; } flagChange(kChStatus); }); return; }
        }
        postToMessageThread([this] { { const juce::ScopedLock sl(lock_); status_ = "Breeding produced no valid sound"; } flagChange(kChStatus); });
    });
}

void CandyProcessor::regenerateLayer(int layer)
{
    const auto d = mainSound(); const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(d.seed, genCounter_++); const double sr = std::min(48000.0, std::max(8000.0, sr_));
    runJob("Regenerating layer...", [this, d, gs, seed, layer, sr]
    {
        CandyGenerator gen; for (int tries = 0; tries < 8 && ! cancel_ && alive_; ++tries)
        {
            auto m = d; gen.regenerateLayer(m, layer, hashCombine(seed, (uint32_t) tries), gs);
            if (gen.analyseAndFinish(m, gs, sr, false)) { postToMessageThread([this, m] { setMainSound(m, true); audition(); }); return; }
        }
    });
}

void CandyProcessor::recall(size_t stepsBack)
{
    const CandyDNA* d = history_.back(stepsBack); if (d == nullptr) return;
    commitMain(*d, false); rebuildBankNow(); audition();
}

//==============================================================================================================
// analysis
//==============================================================================================================
void CandyProcessor::startCapture()
{
    if (! prepared_) return;
    const int barsChoice[] = { 4, 8, 16, 32 };
    const int bars = barsChoice[std::clamp((int) apvts.getRawParameterValue(pid::capBars)->load(), 0, 3)];
    auto cap = std::make_shared<CaptureBuffer>();
    cap->bars = bars; cap->sampleRate = sr_; cap->bpm = apvts.getRawParameterValue(pid::anaBpm)->load();
    cap->capacity = (int) std::ceil((double) bars * cap->beatsPerBar * 60.0 / cap->bpm * sr_);
    cap->mono.assign((size_t) cap->capacity, 0.0f);
    cap->state = 1;
    { const juce::ScopedLock sl(lock_); captureGrave_.push_back(cap); if (captureGrave_.size() > 4) captureGrave_.erase(captureGrave_.begin()); capture_ = cap; status_ = "Armed: waiting for the next bar line..."; }
    captureHandled_ = false;
    captureRaw_.store(cap.get(), std::memory_order_release);
    flagChange(kChStatus);
}

void CandyProcessor::cancelCapture() { captureRaw_.store(nullptr); captureHandled_ = true; { const juce::ScopedLock sl(lock_); capture_.reset(); status_ = "Capture cancelled"; } flagChange(kChStatus); }

int CandyProcessor::captureState() const { const juce::ScopedLock sl(lock_); return capture_ ? capture_->state.load() : 0; }
float CandyProcessor::captureProgress() const { const juce::ScopedLock sl(lock_); return capture_ && capture_->capacity > 0 ? (float) capture_->written.load() / (float) capture_->capacity : 0.0f; }

void CandyProcessor::importAudioFile(const juce::File& f)
{
    const double bpm = apvts.getRawParameterValue(pid::anaBpm)->load();
    runJob("Analysing " + f.getFileName() + "...", [this, f, bpm]
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
        if (r == nullptr || r->lengthInSamples < 4410) { postToMessageThread([this] { { const juce::ScopedLock sl(lock_); status_ = "Could not read that audio file"; } flagChange(kChStatus); }); return; }
        const int maxSamples = (int) std::min<int64_t>(r->lengthInSamples, (int64_t) (r->sampleRate * 60.0 * 8.0));     // up to 8 minutes
        juce::AudioBuffer<float> b((int) std::min<unsigned>(2, r->numChannels), maxSamples);
        r->read(&b, 0, maxSamples, 0, true, true);
        std::vector<float> mono((size_t) maxSamples);
        for (int i = 0; i < maxSamples; ++i) { float s = 0; for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample(c, i); mono[(size_t) i] = s / (float) b.getNumChannels(); }
        // limit analysis to the first 32 bars
        const int maxBars = (int) (32.0 * 4.0 * 60.0 / bpm * r->sampleRate); if ((int) mono.size() > maxBars) mono.resize((size_t) maxBars);
        const double srFile = r->sampleRate;
        postToMessageThread([this, m = std::move(mono), srFile, bpm]() mutable { beginAnalysis(std::move(m), srFile, bpm, 4); });
    });
}

void CandyProcessor::beginAnalysis(std::vector<float> mono, double sr, double bpm, int beats)
{
    const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(0xA5A5u, genCounter_++);
    runJob("Analysing track...", [this, mono = std::move(mono), sr, bpm, beats, gs, seed]
    {
        std::atomic<bool> cancelFlag { false };
        auto a = analyzeTrack(mono.data(), (int) mono.size(), sr, bpm, beats, &cancel_, [this](float p) { progress_ = p; });
        if (! a.valid || cancel_ || ! alive_) { postToMessageThread([this] { { const juce::ScopedLock sl(lock_); status_ = "Analysis cancelled or failed"; } flagChange(kChStatus); }); return; }
        generateSuggestions(a, gs.density, gs.negSpaceSensitivity, seed);
        auto shared = std::make_shared<const TrackAnalysis>(std::move(a));
        postToMessageThread([this, shared]
        {
            { const juce::ScopedLock sl(lock_); analysis_ = shared; status_ = "Analysis ready: " + juce::String(shared->bars) + " bars, " + juce::String((int) shared->suggestions.size()) + " suggestions"; }
            updateAdaptive(); flagChange(kChAnalysis | kChStatus);
            if (listenPending_.exchange(false)) { buildAfterCook_ = true; generate(); }
        });
    });
}

void CandyProcessor::editAnalysis(const std::function<void(TrackAnalysis&)>& fn)
{
    std::shared_ptr<const TrackAnalysis> cur = analysis(); if (cur == nullptr) return;
    auto copy = std::make_shared<TrackAnalysis>(*cur); fn(*copy); ++copy->revision;
    { const juce::ScopedLock sl(lock_); analysis_ = copy; }
    flagChange(kChAnalysis);
}

void CandyProcessor::regenerateSuggestions()
{
    const auto gs = genSettingsFrom(apvts); const uint32_t seed = hashCombine(0xA5A5u, genCounter_++);
    editAnalysis([&](TrackAnalysis& a) { computeOpportunity(a); generateSuggestions(a, gs.density, gs.negSpaceSensitivity, seed); });
}

void CandyProcessor::applySuggestionsToSequence()
{
    auto a = analysis(); if (a == nullptr) return;
    std::vector<PaletteEntry> pal;
    {   const juce::ScopedLock sl(lock_);
        for (int i = 0; i < kNumSoundSlots; ++i) if (preset_.slotUsed[(size_t) i])
        {   const auto& d = preset_.slots[(size_t) i]; PaletteEntry e; e.slot = i; e.bands = d.spectral.bands; e.durationMs = std::max(20.0f, d.durationMs); e.role = d.role; pal.push_back(e); } }
    const auto gs = genSettingsFrom(apvts); const SeqConfig cfg = seqConfig();
    ContextOptions o; o.variety = apvts.getRawParameterValue(pid::variety)->load() * 0.01f; o.velBase = cfg.velBase; o.velRange = cfg.velRange; o.pitchVariation = cfg.pitchVariation;
    o.rootNote = gs.rootNote; o.scale = gs.scale; o.musicality = gs.musicality; o.seed = hashCombine(0xBEEF, genCounter_++);
    ContextReport rep; auto ev = buildContextEvents(*a, pal, o, &rep);
    if (ev.empty())
    {   // the track has no real gap for candy: add nothing rather than a fill that is not needed
        { const juce::ScopedLock sl(lock_); status_ = "Your track is already full: no gap is big enough for candy, so nothing was added (raise AMOUNT or analyse a sparser section)"; }
        flagChange(kChStatus); return;
    }
    setParam(apvts, pid::seqBars, (float) std::clamp(a->bars, 1, 32));
    { const juce::ScopedLock sl(lock_); preset_.seq.events = ev; preset_.seq.bars = std::clamp(a->bars, 1, 32); preset_.seq.freezeAll = true; preset_.seq.enabled = true; }
    setParam(apvts, pid::seqFreeze, 1.0f); setParam(apvts, pid::seqOn, 1.0f);
    seqDirty_ = false; rebuildPatternNow(false);
    int used = 0; for (int n : rep.perSlot) used += n > 0 ? 1 : 0;
    const juce::ScopedLock sl(lock_); status_ = "Placed " + juce::String((int) ev.size()) + " events in the gaps of the track using " + juce::String(used) + " different sound(s)"
                                                + (rep.transitions > 0 ? ", " + juce::String(rep.transitions) + " transition(s) into section changes" : juce::String());
    flagChange(kChStatus | kChPattern);
}

void CandyProcessor::buildPattern(bool newSeed)
{
    if (analysis() != nullptr) { if (newSeed) regenerateSuggestions(); applySuggestionsToSequence(); return; }
    setParam(apvts, pid::seqOn, 1.0f); setParam(apvts, pid::seqFreeze, 0.0f);
    { const juce::ScopedLock sl(lock_); preset_.seq.enabled = true; preset_.seq.freezeAll = false; }
    regeneratePattern(newSeed);
    const juce::ScopedLock sl(lock_); status_ = "No track analysed: pattern planned from rhythm only. Use LISTEN (or Import WAV) so it can place sounds in the gaps of your track";
    flagChange(kChStatus);
}

void CandyProcessor::updateAdaptive()
{
    auto a = analysis();
    if (a == nullptr) { adaptive_.eqOn = false; adaptive_.lowCut = 0.0f; adaptive_.width = 1.0f; adaptive_.trim = 0.0f; return; }
    const auto d = mainSound(); MaskingReport rep = estimateMasking(d.spectral.bands, d.spectral.rmsDb, 1.0f - d.motion.stereoMotion, a->bandDb, a->rmsDb);
    const auto ap = adaptiveFor(rep, d.spectral.bands, 1.0f);
    adaptive_.lowCut = ap.lowCutHz; adaptive_.eqOn = ap.eqActive; adaptive_.eqHz = ap.eqFreqHz; adaptive_.eqDb = ap.eqGainDb; adaptive_.eqQ = ap.eqQ;
    adaptive_.width = ap.widthScale; adaptive_.trim = ap.levelTrimDb;
    const juce::ScopedLock sl(lock_); masking_ = rep;
}

//==============================================================================================================
// export
//==============================================================================================================
ExportSource CandyProcessor::exportSource(const Preset& p, const SeqPattern* pat) const
{
    ExportSource s; s.slots = p.slotPointers(); s.pattern = pat;
    s.bpm = apvts.getRawParameterValue(pid::anaBpm)->load();
    return s;
}

juce::AudioBuffer<float> CandyProcessor::renderPreview(const ExportFormat& fmt, ExportKind kind, ExportMix mix, int soloLayer)
{
    auto p = presetSnapshot(); auto pat = pattern(); Exporter ex;
    return ex.render(exportSource(p, pat.get()), kind, mix, soloLayer, fmt);
}

void CandyProcessor::exportAsync(ExportWhat what, const juce::File& target, const ExportFormat& fmt, ExportMix mix)
{
    auto p = presetSnapshot(); auto pat = pattern(); auto cands = candidates(); const double bpm = apvts.getRawParameterValue(pid::anaBpm)->load();
    ExportFormat fmtFit = fmt; if (what == ExportWhat::OneShot) fmtFit.fitMs = (float) syncWindowMs();
    runJob("Exporting...", [this, what, target, fmt, fmtFit, mix, p, pat, cands, bpm]
    {
        Exporter ex; ExportSource s; s.slots = p.slotPointers(); s.pattern = pat.get(); s.bpm = bpm;
        auto cancelled = [this] { return cancel_.load() || ! alive_; }; ExportResult r;
        const juce::String base = sanitizeFileName(p.main().name);
        switch (what)
        {
            case ExportWhat::OneShot:    r = ex.exportFile(target, s, ExportKind::OneShot, mix, fmtFit, cancelled); break;
            case ExportWhat::Loop:       r = ex.exportFile(target, s, ExportKind::Loop, mix, fmt, cancelled); break;
            case ExportWhat::MultiBar:   r = ex.exportFile(target, s, ExportKind::MultiBar, mix, fmt, cancelled); break;
            case ExportWhat::Layers:     r = ex.exportLayers(target, base, s, ExportKind::OneShot, fmt, cancelled); break;
            case ExportWhat::DryWet:     r = ex.exportDryWet(target, base, s, ExportKind::OneShot, fmt, cancelled); break;
            case ExportWhat::Candidates: { std::vector<CandyDNA> v; for (auto& c : cands) v.push_back(c.dna); r = ex.exportBatch(target, v, fmt, cancelled, [this](float x) { progress_ = x; }); break; }
            case ExportWhat::Midi:       r = Exporter::exportMidi(target, s); break;
        }
        postToMessageThread([this, r] { { const juce::ScopedLock sl(lock_); lastExport_ = r.ok ? r.summary() + " -> " + (r.files.isEmpty() ? juce::String() : r.files[0].getParentDirectory().getFullPathName()) : "Export failed: " + r.error; status_ = lastExport_; } flagChange(kChStatus); });
    });
}

juce::File CandyProcessor::makeDragFile(ExportWhat what, const ExportFormat& fmt)
{
    auto p = presetSnapshot(); auto pat = pattern(); auto s = exportSource(p, pat.get()); Exporter ex;
    const juce::String base = sanitizeFileName(p.main().name);
    if (what == ExportWhat::Midi) return dragFiles_.createMidi(base, s);
    const ExportKind k = what == ExportWhat::Loop ? ExportKind::Loop : what == ExportWhat::MultiBar ? ExportKind::MultiBar : ExportKind::OneShot;
    ExportFormat f2 = fmt; if (k == ExportKind::OneShot) f2.fitMs = (float) syncWindowMs();
    auto buf = ex.render(s, k, ExportMix::Full, -1, f2);
    if (buf.getNumSamples() == 0) return {};
    return dragFiles_.create(base + (k == ExportKind::Loop ? "_loop" : k == ExportKind::MultiBar ? "_bars" : ""), buf, fmt.sampleRate, fmt.bitDepth);
}
} // namespace dali

// plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dali::CandyProcessor(); }
