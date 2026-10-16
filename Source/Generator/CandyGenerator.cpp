#include "CandyGenerator.h"
#include "DSP/Modulation/ModEngine.h"
#include <algorithm>
#include <numeric>

namespace dali
{
uint64_t CandyGenerator::makeId(uint32_t seed, int archetype, uint32_t salt)
{
    uint64_t s = ((uint64_t) seed << 32) ^ ((uint64_t) (archetype + 1) << 8) ^ salt;
    const uint64_t a = splitmix64(s), b = splitmix64(s);
    const uint64_t id = (a & 0xFFFFFFFF00000000ull) | (b >> 32);
    return id ? id : 1ull;
}

int CandyGenerator::pickArchetype(Rng& r, const Intent& in) const
{
    const auto idx = archetypesOf(in.category);
    const auto& t = archetypeTable();
    std::vector<float> w;
    for (int i : idx)
    {
        float x = t[(size_t) i].baseWeight;
        if (taste_) x *= taste_->archetypeMultiplier(in.category, i);
        w.push_back(x);
    }
    if (idx.empty()) return 0;
    return idx[(size_t) r.pickWeighted(w)];
}

bool CandyGenerator::validate(const BufferFeatures& f, float targetMs, float& why)
{
    why = 0.0f;
    if (! f.finite)                         { why = 1; return false; }
    if (f.peakDb < -45.0f)                  { why = 2; return false; }   // effectively silent
    if (std::abs(f.dcOffset) > 0.02f)       { why = 3; return false; }
    if (f.tailDb > -30.0f)                  { why = 4; return false; }   // would click at the end
    if (f.durationSec * 1000.0f > std::max(9000.0f, targetMs * 6.0f)) { why = 5; return false; }
    if (f.activeFraction < 0.03f)           { why = 6; return false; }   // nothing audible but a blip of dust
    return true;
}

float CandyGenerator::score(const CandyDNA& d, const BufferFeatures& f, const Intent& in) const
{
    auto gauss = [](float x, float w) { return std::exp(-(x * x) / (w * w)); };
    const float durMs = std::max(1.0f, f.durationSec * 1000.0f);
    const float target = std::max(20.0f, in.lengthMs * (archetypeTable()[(size_t) std::clamp(d.archetype, 0, (int) archetypeTable().size() - 1)].lengthFactor));
    const float lengthFit = gauss(std::log2(durMs / target), 1.6f);

    const float expectC = 500.0f * std::exp2(in.brightness * 5.0f);
    const float brightFit = gauss(std::log2(std::max(50.0f, f.centroidHz) / expectC), 2.2f);

    const bool wantsTransient = d.role == MusicalRole::Punctuation || d.role == MusicalRole::Accent;
    const float crestFit = wantsTransient ? std::clamp((f.crestDb - 4.0f) / 10.0f, 0.0f, 1.0f)
                                          : 1.0f - std::clamp((f.crestDb - 9.0f) / 10.0f, 0.0f, 0.8f);

    int nr = 0; float depthSum = 0; for (auto& r : d.routes) if (r.enabled) { ++nr; depthSum += std::abs(r.depth); }
    const float motionAmt = std::clamp(0.5f * (1.0f - f.stereoCorr) + 0.12f * (float) nr + 0.5f * depthSum * 0.2f, 0.0f, 1.0f);
    const float motionFit = 1.0f - std::abs(motionAmt - in.motion) * 0.8f;

    const float cx = std::clamp(0.2f * (float) d.activeLayerCount() + 0.04f * (float) nr, 0.0f, 1.0f);
    const float complexFit = 1.0f - std::abs(cx - in.complexity) * 0.7f;

    float flatPen = 1.0f;
    if (f.flatness > 0.85f && d.layers[0].engine != EngineType::NoiseMotion) flatPen = 0.8f;
    const float activity = std::clamp(f.activeFraction * 3.0f, 0.4f, 1.0f);

    float s = 0.22f * lengthFit + 0.2f * brightFit + 0.16f * crestFit + 0.16f * motionFit + 0.14f * complexFit + 0.12f * activity;
    s *= flatPen;
    if (taste_) s *= taste_->multiplier(d);
    return std::clamp(s, 0.0f, 1.5f);
}

float CandyGenerator::distance(const CandyDNA& a, const CandyDNA& b)
{
    float sb = 0; for (int i = 0; i < 8; ++i) { const float x = a.spectral.bands[(size_t) i] - b.spectral.bands[(size_t) i]; sb += x * x; }
    const float spec = std::min(1.0f, std::sqrt(sb) * 2.0f);
    const float cent = std::min(1.0f, std::abs(std::log2(std::max(50.0f, a.spectral.centroidHz) / std::max(50.0f, b.spectral.centroidHz))) / 3.0f);
    const float dur = std::min(1.0f, std::abs(std::log2(std::max(10.0f, a.durationMs) / std::max(10.0f, b.durationMs))) / 3.0f);
    int ea = 0, eb = 0; for (auto& l : a.layers) if (l.active) ea |= 1 << (int) l.engine; for (auto& l : b.layers) if (l.active) eb |= 1 << (int) l.engine;
    const int inter = __builtin_popcount(ea & eb), uni = __builtin_popcount(ea | eb);
    const float eng = uni ? 1.0f - (float) inter / (float) uni : 0.0f;
    const float arch = (a.category == b.category && a.archetype == b.archetype) ? 0.0f : 1.0f;
    const float mot = std::min(1.0f, std::abs(a.motion.stereoMotion - b.motion.stereoMotion) * 2.0f);
    return 0.28f * spec + 0.16f * cent + 0.14f * dur + 0.16f * eng + 0.22f * arch + 0.04f * mot;
}

bool CandyGenerator::analyseAndFinish(CandyDNA& d, const GenSettings& s, double sr, bool autoGain)
{
    RenderOptions o; o.sampleRate = sr; o.quality = QualityMode::Standard; o.blockSize = 512; o.velocity = 0.9f; o.trimSilence = true;
    const Intent in = Intent::from(s);
    auto run = [&](BufferFeatures& f) -> bool
    {
        auto buf = renderer_.renderOneShot(d, o);
        if (buf.getNumSamples() < 8) return false;
        f = analyzeBuffer(buf.getReadPointer(0), buf.getNumChannels() > 1 ? buf.getReadPointer(1) : buf.getReadPointer(0), buf.getNumSamples(), sr);
        return true;
    };
    BufferFeatures f;
    if (autoGain) d.fx.masterGainDb = 0.0f;
    if (! run(f)) return false;
    // Gain staging to a -6 dBFS peak: master gain first (max +12 dB), then layer gains for very quiet recipes.
    for (int pass = 0; autoGain && pass < 3 && f.finite && f.peakDb > -70.0f; ++pass)
    {
        const float delta = -6.0f - f.peakDb;
        if (std::abs(delta) < 0.4f) break;
        float want = d.fx.masterGainDb + delta;
        const float master = std::clamp(want, -24.0f, 12.0f);
        float extra = want - master;                         // > 0 only when master is exhausted
        d.fx.masterGainDb = master;
        if (extra > 0.05f)
            for (auto& l : d.layers) if (l.active && l.gainDb < 6.0f) { const float g = std::min(extra, 6.0f - l.gainDb); l.gainDb += g; }
        if (! run(f)) return false;
    }
    float why = 0;
    const float target = in.lengthMs;
    if (autoGain && f.peakDb < -9.0f) return false;           // recipe too weak to gain-stage: reject
    if (! validate(f, target, why)) return false;

    auto& sp = d.spectral;
    sp.bands = f.bands; sp.centroidHz = f.centroidHz; sp.lowFrac = f.lowFrac; sp.midFrac = f.midFrac; sp.highFrac = f.highFrac;
    sp.peakDb = f.peakDb; sp.rmsDb = f.rmsDb; sp.flatness = f.flatness; sp.crest = f.crestDb;
    d.durationMs = f.durationSec * 1000.0f;
    d.motion.stereoMotion = std::clamp(1.0f - f.stereoCorr, 0.0f, 2.0f);
    float depth = 0, rate = 0; int n = 0;
    for (auto& r : d.routes) if (r.enabled) { depth += std::abs(r.depth); ++n; if (r.source >= kSrcLfo1 && r.source <= kSrcLfo4) rate += d.mods.lfo[(size_t) r.source].rateHz; }
    d.motion.depth = n ? depth / (float) n : 0.0f; d.motion.rate = n ? rate / (float) n : 0.0f;
    d.score = score(d, f, in);
    return true;
}

bool CandyGenerator::generateOne(const GenSettings& s, uint32_t seed, double sr, CandyDNA& out, int forceArchetype)
{
    const Intent in = Intent::from(s);
    Rng r(seed);
    const int a = forceArchetype >= 0 ? forceArchetype : pickArchetype(r, in);
    const auto& def = archetypeTable()[(size_t) a];
    CandyDNA d;
    d.seed = seed; d.category = def.category; d.archetype = a; d.role = def.role; d.generatorVersion = kGeneratorVersion;
    d.id = makeId(seed, a, 0);
    d.numLayers = 0;
    for (auto& l : d.layers) l = LayerConfig();
    for (auto& rt : d.routes) rt = ModRoute();
    Rng rb = r.derive(1);
    Intent local = in; local.lengthMs = in.lengthMs * def.lengthFactor;
    def.build(d, rb, local);
    Rng rm = r.derive(2); applyModulation(d, rm, local);
    Rng rf = r.derive(3); applyEffects(d, rf, local);
    Rng rn = r.derive(4); d.name = makeName(rn, def.name);
    // layer count limit and per-layer sanity
    d.numLayers = 0; for (int i = 0; i < kMaxLayers; ++i) if (d.layers[(size_t) i].active) d.numLayers = i + 1;
    // taste weighting of engine choice can veto the single worst engine only through scoring (see score())
    if (! analyseAndFinish(d, s, sr)) return false;
    out = std::move(d);
    return true;
}

std::vector<CandyDNA> CandyGenerator::generateBatch(const GenSettings& s, uint32_t seed, const GenerateOptions& o, GenerateStats* stats)
{
    GenerateStats st;
    Rng master(seed);
    const int want = std::max(1, o.count);
    const int target = want * std::max(1, o.oversample);
    const int maxTries = target * 3 + 8;
    std::vector<CandyDNA> pool;
    while ((int) pool.size() < target && st.tried < maxTries)
    {
        if (o.cancelled && o.cancelled()) break;
        CandyDNA c; ++st.tried;
        if (generateOne(s, master.next(), o.sampleRate, c)) pool.push_back(std::move(c)); else ++st.rejected;
        if (o.progress) o.progress(0.85f * (float) pool.size() / (float) target);
    }
    // diverse selection: greedy farthest-point with score blending
    std::vector<CandyDNA> out;
    std::vector<bool> used(pool.size(), false);
    while ((int) out.size() < want && out.size() < pool.size())
    {
        int best = -1; float bestV = -1e9f;
        for (size_t i = 0; i < pool.size(); ++i)
        {
            if (used[i]) continue;
            float minD = 1.0f; for (auto& c : out) minD = std::min(minD, distance(pool[i], c));
            if (out.empty()) minD = 0.5f;
            const float v = (1.0f - o.diversity) * pool[i].score + o.diversity * minD;
            if (v > bestV) { bestV = v; best = (int) i; }
        }
        if (best < 0) break;
        used[(size_t) best] = true; out.push_back(pool[(size_t) best]);
    }
    st.accepted = (int) out.size();
    if (stats) *stats = st;
    if (o.progress) o.progress(1.0f);
    return out;
}

// ---------------------------------------------------------------------------------------------------------------
static bool paramLocked(const LayerConfig& l, int i) { return (l.lockMask >> i) & 1; }

static float snapPitch(float p, const GenSettings& s, Rng& r, float amount)
{
    float v = p + r.bipolar() * amount * 4.0f;
    v = quantizeToScale(v, s.rootNote, s.scale, std::pow(s.musicality, 0.8f));
    return std::clamp(v, 24.0f, 108.0f);
}

CandyDNA CandyGenerator::mutate(const CandyDNA& src, uint32_t seed, float amount, const GenSettings& s)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    CandyDNA d = src; Rng r(seed);
    d.parentA = src.id; d.parentB = 0; d.seed = seed; d.id = makeId(seed, d.archetype, 77);
    d.meta = UserMeta();
    for (int li = 0; li < kMaxLayers; ++li)
    {
        auto& l = d.layers[(size_t) li]; if (! l.active || l.locked) continue;
        for (int i = 0; i < kParamsPerLayer; ++i)
            if (! paramLocked(l, i) && r.chance(0.25f + 0.5f * amount)) l.p[(size_t) i] = clamp01(l.p[(size_t) i] + r.normal() * amount * 0.22f);
        if (r.chance(0.5f)) l.pitch = snapPitch(l.pitch, s, r, amount);
        const float tf = std::exp(r.bipolar() * amount * 0.45f);
        l.decayMs = std::clamp(l.decayMs * tf, 1.0f, 8000.0f); l.releaseMs = std::clamp(l.releaseMs * std::exp(r.bipolar() * amount * 0.4f), 1.0f, 8000.0f);
        l.durationMs = std::clamp(l.durationMs * tf, 5.0f, 12000.0f);
        l.attackMs = std::clamp(l.attackMs * std::exp(r.bipolar() * amount * 0.5f), 0.05f, 4000.0f);
        l.gainDb = std::clamp(l.gainDb + r.bipolar() * amount * 3.0f, -30.0f, 3.0f);
        l.pan = std::clamp(l.pan + r.bipolar() * amount * 0.4f, -1.0f, 1.0f);
        l.sendReverb = clamp01(l.sendReverb + r.bipolar() * amount * 0.2f); l.sendDelay = clamp01(l.sendDelay + r.bipolar() * amount * 0.2f);
        if (r.chance(amount * 0.5f)) l.seed = r.next() | 1u;
        if (l.insert != InsertFx::None) { l.insertA = clamp01(l.insertA + r.bipolar() * amount * 0.3f); l.insertB = clamp01(l.insertB + r.bipolar() * amount * 0.3f); }
    }
    for (auto& rt : d.routes) if (rt.enabled)
    {
        rt.depth = std::clamp(rt.depth + r.normal() * amount * 0.18f, -1.0f, 1.0f);
        if (r.chance(amount * 0.06f)) rt.depth = -rt.depth;
    }
    for (auto& m : d.mods.lfo) m.rateHz = std::clamp(m.rateHz * std::exp(r.bipolar() * amount * 0.5f), 0.05f, 40.0f);
    d.fx.delayFeedback = std::clamp(d.fx.delayFeedback + r.bipolar() * amount * 0.15f, 0.0f, 0.75f);
    d.fx.reverbSize = clamp01(d.fx.reverbSize + r.bipolar() * amount * 0.2f);
    d.name = src.name.upToFirstOccurrenceOf(" ~", false, false) + " ~" + juce::String(r.rangeInt(10, 99));
    return d;
}

void CandyGenerator::regenerateLayer(CandyDNA& d, int li, uint32_t seed, const GenSettings& s)
{
    if (li < 0 || li >= kMaxLayers) return;
    auto& l = d.layers[(size_t) li]; if (l.locked) return;
    Rng r(seed); const Intent in = Intent::from(s);
    const LayerConfig old = l;
    if (! l.active) { initLayer(l, (EngineType) r.rangeInt(0, kNumEngines - 1), r.next()); l.active = true; }
    else if (old.lockMask == 0 && r.chance(0.3f)) l.engine = (EngineType) r.rangeInt(0, kNumEngines - 1);
    l.seed = r.next() | 1u;
    randomEngineParams(l, r, in);
    for (int i = 0; i < kParamsPerLayer; ++i) if (paramLocked(old, i) && old.engine == l.engine) l.p[(size_t) i] = old.p[(size_t) i];
    l.lockMask = old.lockMask; l.solo = old.solo; l.mute = old.mute; l.locked = old.locked;
    l.pan = old.pan; l.gainDb = old.gainDb; l.startOffsetMs = old.startOffsetMs; l.insert = old.insert; l.insertA = old.insertA; l.insertB = old.insertB;
    l.sendDelay = old.sendDelay; l.sendReverb = old.sendReverb; l.sendMod = old.sendMod;
    d.numLayers = 0; for (int i = 0; i < kMaxLayers; ++i) if (d.layers[(size_t) i].active) d.numLayers = i + 1;
    d.id = makeId(seed, d.archetype, 100 + (uint32_t) li);
}

CandyDNA CandyGenerator::evolve(const CandyDNA& d, uint32_t seed, float amount, const GenSettings& s)
{
    CandyDNA e = mutate(d, seed, std::clamp(amount, 0.0f, 1.0f) * 0.5f, s);
    e.name = d.name; return e;
}

static int destLayer(int dest)
{
    if (dest < 64) return dest / kParamsPerLayer;
    if (dest < 96) return (dest - kLayerDestBase) / 8;
    return -1;
}
static void copyModulator(int src, const ModulatorBank& from, ModulatorBank& to)
{
    if (src >= kSrcLfo1 && src <= kSrcLfo4)         to.lfo[(size_t) (src - kSrcLfo1)] = from.lfo[(size_t) (src - kSrcLfo1)];
    else if (src >= kSrcMseg1 && src <= kSrcMseg4)  to.mseg[(size_t) (src - kSrcMseg1)] = from.mseg[(size_t) (src - kSrcMseg1)];
    else if (src == kSrcSH1 || src == kSrcSH2)      to.sh[(size_t) (src - kSrcSH1)] = from.sh[(size_t) (src - kSrcSH1)];
    else if (src == kSrcChaos1 || src == kSrcChaos2) to.chaos[(size_t) (src - kSrcChaos1)] = from.chaos[(size_t) (src - kSrcChaos1)];
    else if (src == kSrcRandomWalk)                 to.rw = from.rw;
    else if (src == kSrcStep)                       to.step = from.step;
}

CandyDNA CandyGenerator::breed(const CandyDNA& a, const CandyDNA& b, uint32_t seed, const GenSettings& s)
{
    Rng r(seed);
    CandyDNA c = a;
    c.parentA = a.id; c.parentB = b.id; c.seed = seed; c.meta = UserMeta();
    c.id = makeId(seed, a.archetype, 200);
    const bool aMain = r.chance(0.5f);
    c.category = aMain ? a.category : b.category; c.archetype = aMain ? a.archetype : b.archetype; c.role = aMain ? a.role : b.role;
    std::array<bool, kMaxLayers> fromB {};
    for (int i = 0; i < kMaxLayers; ++i)
    {
        const auto& la = a.layers[(size_t) i]; const auto& lb = b.layers[(size_t) i];
        bool useB = r.chance(0.5f);
        if (la.locked) useB = false;
        if (! la.active && lb.active) useB = true; else if (la.active && ! lb.active && r.chance(0.7f)) useB = false;
        fromB[(size_t) i] = useB;
        c.layers[(size_t) i] = useB ? lb : la;
        if (la.active && lb.active && la.engine == lb.engine && ! c.layers[(size_t) i].locked)
        {
            const float t = r.range(0.25f, 0.75f);
            for (int k = 0; k < kParamsPerLayer; ++k) if (! paramLocked(c.layers[(size_t) i], k)) c.layers[(size_t) i].p[(size_t) k] = lerp(la.p[(size_t) k], lb.p[(size_t) k], t);
        }
    }
    c.numLayers = 0; for (int i = 0; i < kMaxLayers; ++i) if (c.layers[(size_t) i].active) c.numLayers = i + 1;
    // routes: each follows the parent that supplied its target layer
    for (auto& rt : c.routes) rt = ModRoute();
    int slot = 0;
    auto take = [&](const CandyDNA& p, bool bSide)
    {
        for (auto& rt : p.routes)
        {
            if (! rt.enabled || slot >= kNumModRoutes) continue;
            const int L = destLayer(rt.dest);
            const bool ok = L >= 0 ? (L < kMaxLayers && fromB[(size_t) L] == bSide && c.layers[(size_t) L].active) : (r.chance(0.5f) == bSide);
            if (! ok) continue;
            if (bSide) copyModulator(rt.source, b.mods, c.mods);
            c.routes[(size_t) slot++] = rt;
        }
    };
    take(a, false); take(b, true);
    // effects: interpolate / pick
    const float t = r.range(0.2f, 0.8f);
    c.fx = a.fx;
    c.fx.delayFeedback = lerp(a.fx.delayFeedback, b.fx.delayFeedback, t); c.fx.reverbSize = lerp(a.fx.reverbSize, b.fx.reverbSize, t);
    c.fx.reverbDamp = lerp(a.fx.reverbDamp, b.fx.reverbDamp, t); c.fx.width = lerp(a.fx.width, b.fx.width, t);
    if (r.chance(0.5f)) { c.fx.modType = b.fx.modType; c.fx.modRate = b.fx.modRate; c.fx.modDepth = b.fx.modDepth; }
    if (r.chance(0.5f)) { c.fx.delayDiv = b.fx.delayDiv; c.fx.pingPong = b.fx.pingPong; }
    // light mutation so that children are never exact copies
    c = mutate(c, hashCombine(seed, 5u), 0.08f, s);
    c.parentA = a.id; c.parentB = b.id; c.id = makeId(seed, c.archetype, 201);
    c.name = a.name.upToFirstOccurrenceOf(" ~", false, false).upToFirstOccurrenceOf(" x ", false, false) + " x " + b.name.upToFirstOccurrenceOf(" ~", false, false);
    return c;
}

// ---------------------------------------------------------------------------------------------------------------
void CandyHistory::push(const CandyDNA& d, size_t cap)
{
    for (size_t i = 0; i < items_.size(); ++i) if (items_[i].id == d.id) { items_.erase(items_.begin() + (long) i); break; }
    items_.push_back(d);
    while (items_.size() > cap) items_.erase(items_.begin());
}
const CandyDNA* CandyHistory::find(uint64_t id) const { for (auto& d : items_) if (d.id == id) return &d; return nullptr; }
const CandyDNA* CandyHistory::back(size_t n) const { return n < items_.size() ? &items_[items_.size() - 1 - n] : nullptr; }
} // namespace dali
