#include "CandyGenerator.h"
#include "DSP/Modulation/ModEngine.h"
#include <algorithm>
#include <bit>
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

int CandyGenerator::pickFrom(Rng& r, const std::vector<int>& idx) const
{
    const auto& t = archetypeTable();
    std::vector<float> w;
    for (int i : idx)
    {
        float x = t[(size_t) i].baseWeight;
        if (taste_) x *= taste_->archetypeMultiplier(t[(size_t) i].category, i);
        w.push_back(x);
    }
    if (idx.empty()) return 0;
    return idx[(size_t) r.pickWeighted(w)];
}

int CandyGenerator::pickArchetype(Rng& r, const Intent& in) const { return pickFrom(r, archetypesOf(in.category)); }

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
    const float target = std::max(20.0f, in.lengthMs * (in.sync ? 1.0f : archetypeTable()[(size_t) std::clamp(d.archetype, 0, (int) archetypeTable().size() - 1)].lengthFactor));
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
    const int inter = std::popcount((unsigned) (ea & eb)), uni = std::popcount((unsigned) (ea | eb));
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

CandyDNA CandyGenerator::buildRaw(const GenSettings& s, Rng& r, int a, const Intent& in) const
{
    const auto& def = archetypeTable()[(size_t) a];
    CandyDNA d;
    d.seed = 0; d.category = def.category; d.archetype = a; d.role = def.role; d.generatorVersion = kGeneratorVersion;
    d.numLayers = 0;
    for (auto& l : d.layers) l = LayerConfig();
    for (auto& rt : d.routes) rt = ModRoute();
    Rng rb = r.derive(1);
    Intent local = in; local.category = def.category;
    local.lengthMs = lengthMsFor(s, def.category) * (s.syncLength ? 1.0f : def.lengthFactor);
    def.build(d, rb, local);
    Rng rm = r.derive(2); applyModulation(d, rm, local);
    Rng rf = r.derive(3); applyEffects(d, rf, local);
    Rng rn = r.derive(4); d.name = makeName(rn, def.name);
    d.numLayers = 0; for (int i = 0; i < kMaxLayers; ++i) if (d.layers[(size_t) i].active) d.numLayers = i + 1;
    return d;
}

bool CandyGenerator::generateOne(const GenSettings& s, uint32_t seed, double sr, CandyDNA& out, int forceArchetype)
{
    const Intent in = Intent::from(s);
    Rng r(seed);
    const bool byFamily = forceArchetype < 0 && s.familyA >= 0;
    const bool fusion = byFamily && s.familyB >= 0 && s.familyB != s.familyA;
    const int a = forceArchetype >= 0 ? forceArchetype : (byFamily ? pickFrom(r, archetypesOfFamily(s.familyA)) : pickArchetype(r, in));
    CandyDNA d = buildRaw(s, r, a, in);
    d.seed = seed; d.id = makeId(seed, a, 0);
    if (fusion)
    {
        const int b = pickFrom(r, archetypesOfFamily(s.familyB));
        Rng rB = r.derive(50);
        CandyDNA db = buildRaw(s, rB, b, in);
        const auto nm = d.name;
        d = fuse(d, db, seed);
        d.archetype = a; d.id = makeId(seed, a, 0);
        Rng rn = r.derive(51); d.name = makeName(rn, fusionName(s.familyA, s.familyB));
        (void) nm;
    }
    if (s.spice > 0.0f) { Rng rs = r.derive(60); applySpice(d, rs, s.spice); }
    if (! analyseAndFinish(d, s, sr)) return false;
    if (s.syncLength && ! fitLength(d, s, sr)) return false;
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

CandyDNA CandyGenerator::mutate(const CandyDNA& src, uint32_t seed, float amount, const GenSettings& s, bool keepSkeleton)
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
        if (r.chance(0.5f)) l.pitch = snapPitch(l.pitch, s, r, keepSkeleton ? amount * 0.35f : amount);
        if (keepSkeleton)
        {
            if (r.chance(0.5f + 0.4f * amount)) l.seed = r.next() | 1u;
            continue;                                                  // timing, level, pan, sends and inserts are the skeleton
        }
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
    for (auto& m : d.mods.lfo) m.rateHz = std::clamp(m.rateHz * std::exp(r.bipolar() * amount * (keepSkeleton ? 0.25f : 0.5f)), 0.05f, 40.0f);
    if (keepSkeleton) { d.name = src.name.upToFirstOccurrenceOf(" ~", false, false) + " ~" + juce::String(r.rangeInt(10, 99)); return d; }
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
// Fusion, spice, tempo-fit and KEEP variations
// ---------------------------------------------------------------------------------------------------------------
namespace
{
struct SrcPool { int first, count; };
static bool poolOf(int src, SrcPool& p)
{
    if (src >= kSrcLfo1 && src <= kSrcLfo4)        { p = { kSrcLfo1, kNumLfos }; return true; }
    if (src >= kSrcMseg1 && src <= kSrcMseg4)      { p = { kSrcMseg1, kNumMsegs }; return true; }
    if (src == kSrcSH1 || src == kSrcSH2)          { p = { kSrcSH1, kNumSH }; return true; }
    if (src == kSrcChaos1 || src == kSrcChaos2)    { p = { kSrcChaos1, kNumChaos }; return true; }
    if (src == kSrcRandomWalk)                     { p = { kSrcRandomWalk, 1 }; return true; }
    if (src == kSrcStep)                           { p = { kSrcStep, 1 }; return true; }
    return false;                                   // live sources (velocity, note, macros ...) carry no configuration
}
static int remapDest(int dest, const std::array<int, kMaxLayers>& map)
{
    if (dest < kLayerDestBase) { const int L = dest / kParamsPerLayer; if (L >= kMaxLayers || map[(size_t) L] < 0) return -1; return layerParamDest(map[(size_t) L], dest % kParamsPerLayer); }
    if (dest < 96)             { const int k = dest - kLayerDestBase; const int L = k / 8; if (L >= kMaxLayers || map[(size_t) L] < 0) return -1; return layerCtlDest(map[(size_t) L], k % 8); }
    return dest;
}
}

CandyDNA CandyGenerator::fuse(const CandyDNA& a, const CandyDNA& b, uint32_t seed)
{
    Rng r(hashCombine(seed, 0xF05Eu));
    auto activeOf = [](const CandyDNA& d) { std::vector<int> v; for (int i = 0; i < kMaxLayers; ++i) if (d.layers[(size_t) i].active) v.push_back(i); return v; };
    const auto la = activeOf(a), lb = activeOf(b);
    const int na = (int) la.size(), nb = (int) lb.size();
    const int keepB = std::min(nb, 2);
    const int keepA = std::min(na, kMaxLayers - keepB);
    const int keepB2 = std::min(nb, kMaxLayers - keepA);
    CandyDNA c = a;
    c.seed = seed; c.id = makeId(seed, a.archetype, 300); c.parentA = 0; c.parentB = 0; c.meta = UserMeta();
    for (auto& l : c.layers) l = LayerConfig();
    for (auto& rt : c.routes) rt = ModRoute();
    std::array<int, kMaxLayers> mapA, mapB; mapA.fill(-1); mapB.fill(-1);
    int slot = 0;
    for (int i = 0; i < keepA; ++i) { c.layers[(size_t) slot] = a.layers[(size_t) la[(size_t) i]]; mapA[(size_t) la[(size_t) i]] = slot++; }
    for (int i = 0; i < keepB2; ++i)
    {
        auto l = b.layers[(size_t) lb[(size_t) i]]; l.seed = hashCombine(l.seed, 0xB0B0u) | 1u; l.startOffsetMs += r.range(0.0f, 25.0f);
        c.layers[(size_t) slot] = l; mapB[(size_t) lb[(size_t) i]] = slot++;
    }
    c.numLayers = slot;
    // routes of A keep their modulators; routes of B get modulators of the same kind that A does not use
    std::array<bool, kNumModSources> used {}; int rn = 0;
    for (auto& rt : a.routes)
    {
        if (! rt.enabled || rn >= kNumModRoutes) continue;
        const int nd = remapDest(rt.dest, mapA); if (nd < 0) continue;
        ModRoute x = rt; x.dest = nd; c.routes[(size_t) rn++] = x; used[(size_t) rt.source] = true;
    }
    std::array<int, kNumModSources> srcMap; srcMap.fill(-2);
    for (auto& rt : b.routes)
    {
        if (! rt.enabled || rn >= kNumModRoutes) continue;
        const int nd = remapDest(rt.dest, mapB); if (nd < 0) continue;
        int src = rt.source; SrcPool p;
        if (poolOf(src, p))
        {
            if (srcMap[(size_t) src] == -2)
            {
                int pick = -1; for (int k = 0; k < p.count; ++k) if (! used[(size_t) (p.first + k)]) { pick = p.first + k; break; }
                srcMap[(size_t) src] = pick;
                if (pick >= 0)
                {
                    used[(size_t) pick] = true;
                    if (src >= kSrcLfo1 && src <= kSrcLfo4)        c.mods.lfo[(size_t) (pick - kSrcLfo1)] = b.mods.lfo[(size_t) (src - kSrcLfo1)];
                    else if (src >= kSrcMseg1 && src <= kSrcMseg4) c.mods.mseg[(size_t) (pick - kSrcMseg1)] = b.mods.mseg[(size_t) (src - kSrcMseg1)];
                    else if (src == kSrcSH1 || src == kSrcSH2)     c.mods.sh[(size_t) (pick - kSrcSH1)] = b.mods.sh[(size_t) (src - kSrcSH1)];
                    else if (src == kSrcChaos1 || src == kSrcChaos2) c.mods.chaos[(size_t) (pick - kSrcChaos1)] = b.mods.chaos[(size_t) (src - kSrcChaos1)];
                    else if (src == kSrcRandomWalk)                c.mods.rw = b.mods.rw;
                    else if (src == kSrcStep)                      c.mods.step = b.mods.step;
                }
            }
            src = srcMap[(size_t) src]; if (src < 0) continue;
        }
        ModRoute x = rt; x.dest = nd; x.source = src; c.routes[(size_t) rn++] = x;
    }
    // FX bus: halfway between the two recipes
    const float t = 0.5f;
    c.fx = a.fx;
    c.fx.delayFeedback = lerp(a.fx.delayFeedback, b.fx.delayFeedback, t); c.fx.reverbSize = lerp(a.fx.reverbSize, b.fx.reverbSize, t);
    c.fx.reverbDamp = lerp(a.fx.reverbDamp, b.fx.reverbDamp, t); c.fx.width = lerp(a.fx.width, b.fx.width, t);
    c.fx.delayLevel = lerp(a.fx.delayLevel, b.fx.delayLevel, t); c.fx.reverbLevel = lerp(a.fx.reverbLevel, b.fx.reverbLevel, t);
    if (r.chance(0.5f)) { c.fx.modType = b.fx.modType; c.fx.modRate = b.fx.modRate; c.fx.modDepth = b.fx.modDepth; c.fx.modLevel = b.fx.modLevel; }
    c.durationMs = std::max(a.durationMs, b.durationMs);
    c.role = a.role;
    return c;
}

void CandyGenerator::applySpice(CandyDNA& d, Rng& r, float spice)
{
    spice = clamp01(spice);
    if (spice <= 0.0f) return;
    // 1) motion: deeper and faster modulation
    for (auto& rt : d.routes) if (rt.enabled) rt.depth = std::clamp(rt.depth * (1.0f + 1.2f * spice), -1.0f, 1.0f);
    for (auto& m : d.mods.lfo) m.rateHz = std::clamp(m.rateHz * (1.0f + 1.5f * spice), 0.05f, 40.0f);
    // 2) weirdness: a chaotic route into a random parameter of a random active layer (when a source is free)
    std::array<bool, kNumModSources> used {}; for (auto& rt : d.routes) if (rt.enabled) used[(size_t) rt.source] = true;
    std::vector<int> act; for (int i = 0; i < kMaxLayers; ++i) if (d.layers[(size_t) i].active) act.push_back(i);
    if (act.empty()) return;
    const int extra = spice > 0.66f ? 2 : (spice > 0.25f ? 1 : 0);
    for (int e = 0; e < extra; ++e)
    {
        int slot = -1; for (int i = 0; i < kNumModRoutes; ++i) if (! d.routes[(size_t) i].enabled) { slot = i; break; }
        int src = -1; for (int cand : { (int) kSrcChaos1, (int) kSrcChaos2, (int) kSrcRandomWalk }) if (! used[(size_t) cand]) { src = cand; break; }
        if (slot < 0 || src < 0) break;
        used[(size_t) src] = true;
        if (src == kSrcChaos1 || src == kSrcChaos2) { auto& c = d.mods.chaos[(size_t) (src - kSrcChaos1)]; c.rateHz = r.range(1.5f, 6.0f + 10.0f * spice); c.r = r.range(3.6f, 3.99f); }
        else d.mods.rw.rateHz = r.range(4.0f, 14.0f);
        auto& rt = d.routes[(size_t) slot];
        rt = ModRoute(); rt.enabled = true; rt.source = src; rt.dest = layerParamDest(act[(size_t) r.rangeInt(0, (int) act.size() - 1)], r.rangeInt(0, kParamsPerLayer - 1));
        rt.depth = (r.chance(0.5f) ? 1.0f : -1.0f) * r.range(0.15f, 0.25f + 0.45f * spice);
    }
    // 3) weirdness: a layer without insert may get a frequency shifter / ring mod / wavefolder
    for (int i : act)
    {
        auto& l = d.layers[(size_t) i];
        if (l.insert == InsertFx::None && r.chance(spice * 0.45f))
        {
            static const InsertFx weird[] = { InsertFx::FreqShift, InsertFx::RingMod, InsertFx::Wavefolder, InsertFx::Comb };
            l.insert = weird[r.rangeInt(0, 3)]; l.insertA = r.range(0.2f, 0.8f); l.insertB = r.range(0.2f, 0.8f);
        }
    }
    d.fx.width = std::clamp(d.fx.width + 0.3f * spice, 0.0f, 1.6f);
    d.fx.modLevel = clamp01(d.fx.modLevel + 0.2f * spice);
}

bool CandyGenerator::fitLength(CandyDNA& d, const GenSettings& s, double sr)
{
    const float target = std::max(20.0f, s.customLengthMs);
    auto okNow = [&] { return d.durationMs >= 0.5f * target && d.durationMs <= 1.3f * target; };
    for (int iter = 0; iter < 5; ++iter)
    {
        const float ratio = target / std::max(5.0f, d.durationMs);
        if (okNow() && ratio > 0.8f && ratio < 1.25f) return true;           // close enough: no more passes
        const float k = std::clamp(ratio, 0.2f, 6.0f);
        for (auto& l : d.layers) if (l.active)
        {
            l.decayMs = std::clamp(l.decayMs * k, 1.0f, 12000.0f); l.releaseMs = std::clamp(l.releaseMs * k, 1.0f, 12000.0f);
            l.durationMs = std::clamp(l.durationMs * k, 5.0f, 20000.0f); l.startOffsetMs = std::clamp(l.startOffsetMs * k, 0.0f, 8000.0f);
            l.attackMs = std::clamp(l.attackMs * std::sqrt(k), 0.05f, 4000.0f);
            if (k > 1.6f) l.sendDelay = std::max(l.sendDelay, 0.25f);
            else if (k < 1.0f) { const float q = k * k; l.sendReverb *= q; l.sendDelay *= q; l.sendMod *= q; }   // short windows cannot afford long tails
        }
        if (k > 1.6f)       { d.fx.delayFeedback = std::max(d.fx.delayFeedback, 0.5f); d.fx.reverbSize = std::max(d.fx.reverbSize, 0.6f); }
        else if (k < 0.7f)  { d.fx.delayFeedback *= 0.5f; d.fx.reverbSize *= 0.5f; }
        for (auto& m : d.mods.lfo) if (! m.sync) m.rateHz = std::clamp(m.rateHz / std::sqrt(k), 0.05f, 40.0f);
        if (! analyseAndFinish(d, s, sr)) return false;
    }
    return okNow();
}

std::vector<CandyDNA> CandyGenerator::variations(const CandyDNA& base, uint32_t seed, int count, float detail, const GenSettings& s, double sr)
{
    std::vector<CandyDNA> out; Rng r(hashCombine(seed, 0x4B33Bu));
    for (int i = 0, tries = 0; (int) out.size() < count && tries < count * 4; ++tries, ++i)
    {
        // spread the amounts a little so the eight variations are not all alike
        const float amt = std::clamp(detail * (0.6f + 0.8f * ((float) (i % count) / (float) std::max(1, count - 1))), 0.02f, 1.0f);
        CandyDNA v = mutate(base, hashCombine(seed, (uint32_t) (i + 1)), amt, s, true);
        if (s.spice > 0.0f) { Rng rs = r.derive((uint32_t) i); applySpice(v, rs, s.spice * 0.6f); }
        v.id = makeId(seed, base.archetype, 400 + (uint32_t) i);
        if (analyseAndFinish(v, s, sr)) out.push_back(std::move(v));
    }
    return out;
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
