#include "CandyDNA.h"

namespace dali
{
//==============================================================================
// Names
//==============================================================================
const char* engineName(EngineType e)
{
    static const char* n[] = { "Micro Synth", "Resonator", "Granular", "Glitch", "Spectral", "Noise & Motion" };
    return n[std::clamp((int) e, 0, kNumEngines - 1)];
}
const char* categoryName(Category c)
{
    static const char* n[] = { "Micro Details", "Psychedelic Movement", "Organic Textures", "Metallic Accents",
                               "Rhythmic Glitches", "Atmospheric Candy", "Transitions", "Experimental" };
    return n[std::clamp((int) c, 0, kNumCategories - 1)];
}
const char* roleName(MusicalRole r)
{
    static const char* n[] = { "Accent", "Fill", "Texture", "Movement", "Transition", "Punctuation" };
    return n[std::clamp((int) r, 0, 5)];
}
const char* insertName(InsertFx f)
{
    static const char* n[] = { "None", "Saturation", "Wavefolder", "Bitcrusher", "Ring Mod", "Freq Shift", "Comb", "Filter", "Transient" };
    return n[std::clamp((int) f, 0, 8)];
}
const char* seqModeName(SeqMode m)
{
    static const char* n[] = { "Sparse", "Groove Follower", "Call & Response", "Controlled Chaos",
                               "Phrase Evolution", "Euclidean", "Probability Seq", "Transition Builder" };
    return n[std::clamp((int) m, 0, 7)];
}
const char* modSourceName(int s)
{
    static const char* n[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "MSEG 1", "MSEG 2", "MSEG 3", "MSEG 4",
                               "S&H 1", "S&H 2", "Chaos 1", "Chaos 2", "Random Walk", "Step Mod", "Env Follower", "Transient",
                               "Velocity", "Note", "Mod Wheel", "Aftertouch", "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return n[std::clamp(s, 0, kNumModSources - 1)];
}
const char* divisionName(int idx)
{
    static const char* n[] = { "1/4", "1/8", "1/16", "1/32", "1/64" };
    return n[std::clamp(idx, 0, kNumDivisions - 1)];
}

const char* engineParamName(EngineType e, int i)
{
    static const char* t[kNumEngines][kParamsPerLayer] = {
        { "Wave", "Pulse Width", "FM Ratio", "FM Amount", "AM Amount", "Ring Mod", "Pitch Env", "Pitch Decay",
          "Cutoff", "Resonance", "Filter Type", "Noise", "Drive", "Filter Env", "Detune", "Vibrato" },
        { "Structure", "Inharmonic", "Brightness", "Damping", "Feedback", "Excite Color", "Excite Len", "Ring Freq",
          "Ring Amount", "Freq Shift", "Shift Mix", "Comb Mix", "Partials", "Spread", "Position", "Spring" },
        { "Source", "Src Pitch", "Grain Size", "Density", "Grain Pitch", "Pitch Spread", "Position", "Pos Spread",
          "Reverse", "Stereo", "Grain Env", "Freeze", "Scan Speed", "Jitter", "Src Bright", "Level Var" },
        { "Slice Len", "Reverse", "Stutter", "Retrigger", "Bit Depth", "Rate Reduce", "Gate Rate", "Gate Duty",
          "Probability", "Micro Loop", "Source", "Src Pitch", "Src Bright", "Crossfade", "Pitch Drift", "Artifact Mix" },
        { "Wet Mix", "Freeze", "Smear", "Filter Center", "Filter Width", "Bin Mod Depth", "Bin Mod Rate", "Randomize",
          "Pitch Shift", "Source", "Src Pitch", "Src Bright", "Tilt", "Hold Pos", "Phase Random", "Level" },
        { "Color", "Filter Center", "Resonance", "Sweep Amount", "Sweep Rate", "Vowel", "Formant Mix", "Random Walk",
          "Chaos", "Breath Shape", "Breath Rate", "Stereo Move", "Move Rate", "Organic Mod", "Chirp", "Tonalness" }
    };
    return t[std::clamp((int) e, 0, kNumEngines - 1)][std::clamp(i, 0, kParamsPerLayer - 1)];
}

juce::String modDestName(int d)
{
    if (d < kLayerDestBase)
        return "L" + juce::String(d / kParamsPerLayer + 1) + " P" + juce::String(d % kParamsPerLayer + 1);
    if (d < 96)
    {
        static const char* ctl[] = { "Gain", "Pan", "Pitch", "Send Dly", "Send Rev", "Send Mod", "Insert A", "Insert B" };
        const int k = d - kLayerDestBase;
        return "L" + juce::String(k / 8 + 1) + " " + ctl[k % 8];
    }
    static const char* g[] = { "Dly Feedback", "Reverb Size", "ModFx Depth", "Width", "Master Gain", "Delay Time" };
    return g[std::clamp(d - 96, 0, 5)];
}

float lengthMsFor(const GenSettings& g, Category c)
{
    static const float modeMs[] = { 180.0f, 600.0f, 2200.0f };
    float base = g.lengthMode >= 3 ? g.customLengthMs : modeMs[std::clamp(g.lengthMode, 0, 2)];
    static const float catScale[kNumCategories] = { 0.6f, 1.2f, 1.1f, 0.8f, 0.9f, 1.6f, 1.8f, 1.0f };
    if (g.lengthMode < 3) base *= catScale[std::clamp((int) c, 0, kNumCategories - 1)];
    return std::clamp(base, 20.0f, 20000.0f);
}

juce::String idToString(uint64_t id) { return juce::String::toHexString((juce::int64) id).paddedLeft('0', 16); }
uint64_t stringToId(const juce::String& s) { return (uint64_t) s.getHexValue64(); }

juce::var vecToVar(const float* d, int n)
{
    juce::Array<juce::var> a;
    for (int i = 0; i < n; ++i) a.add((double) d[i]);
    return a;
}
void varToVec(const juce::var& v, float* d, int n)
{
    if (auto* a = v.getArray())
        for (int i = 0; i < n && i < a->size(); ++i) d[i] = (float) (double) (*a)[i];
}

//==============================================================================
// Serialisation helpers
//==============================================================================
namespace
{
using Obj = juce::DynamicObject;
float  getF(const juce::var& v, const char* k, float def)   { return v.hasProperty(k) ? (float) (double) v[k] : def; }
int    getI(const juce::var& v, const char* k, int def)     { return v.hasProperty(k) ? (int) v[k] : def; }
bool   getB(const juce::var& v, const char* k, bool def)    { return v.hasProperty(k) ? (bool) v[k] : def; }
juce::String getS(const juce::var& v, const char* k, const juce::String& def) { return v.hasProperty(k) ? v[k].toString() : def; }

template <size_t N> juce::var arrToVar(const std::array<float, N>& a) { return vecToVar(a.data(), (int) N); }

juce::var layerToVar(const LayerConfig& l)
{
    auto* o = new Obj();
    o->setProperty("active", l.active); o->setProperty("engine", (int) l.engine);
    o->setProperty("gainDb", l.gainDb); o->setProperty("pitch", l.pitch); o->setProperty("pan", l.pan);
    o->setProperty("a", l.attackMs); o->setProperty("d", l.decayMs); o->setProperty("s", l.sustain); o->setProperty("r", l.releaseMs);
    o->setProperty("start", l.startOffsetMs); o->setProperty("dur", l.durationMs);
    o->setProperty("sdl", l.sendDelay); o->setProperty("srv", l.sendReverb); o->setProperty("smd", l.sendMod);
    o->setProperty("seed", (juce::int64) l.seed);
    o->setProperty("mute", l.mute); o->setProperty("solo", l.solo); o->setProperty("locked", l.locked);
    o->setProperty("lockMask", (int) l.lockMask);
    o->setProperty("insert", (int) l.insert); o->setProperty("iA", l.insertA); o->setProperty("iB", l.insertB);
    o->setProperty("p", arrToVar(l.p));
    return juce::var(o);
}
void layerFromVar(const juce::var& v, LayerConfig& l)
{
    l.active = getB(v, "active", false); l.engine = (EngineType) std::clamp(getI(v, "engine", 0), 0, kNumEngines - 1);
    l.gainDb = getF(v, "gainDb", -6.f); l.pitch = getF(v, "pitch", 60.f); l.pan = getF(v, "pan", 0.f);
    l.attackMs = getF(v, "a", 1.f); l.decayMs = getF(v, "d", 120.f); l.sustain = getF(v, "s", 0.f); l.releaseMs = getF(v, "r", 60.f);
    l.startOffsetMs = getF(v, "start", 0.f); l.durationMs = getF(v, "dur", 300.f);
    l.sendDelay = getF(v, "sdl", 0.f); l.sendReverb = getF(v, "srv", 0.f); l.sendMod = getF(v, "smd", 0.f);
    l.seed = (uint32_t) (juce::int64) v.getProperty("seed", 1);
    l.mute = getB(v, "mute", false); l.solo = getB(v, "solo", false); l.locked = getB(v, "locked", false);
    l.lockMask = (uint16_t) getI(v, "lockMask", 0);
    l.insert = (InsertFx) std::clamp(getI(v, "insert", 0), 0, (int) InsertFx::Count - 1);
    l.insertA = getF(v, "iA", 0.5f); l.insertB = getF(v, "iB", 0.5f);
    varToVec(v["p"], l.p.data(), kParamsPerLayer);
}

juce::var modsToVar(const ModulatorBank& m)
{
    auto* o = new Obj();
    juce::Array<juce::var> lfos, msegs, shs, chaos;
    for (auto& l : m.lfo)
    {
        auto* x = new Obj(); x->setProperty("shape", l.shape); x->setProperty("rate", l.rateHz); x->setProperty("sync", l.sync);
        x->setProperty("div", l.syncDiv); x->setProperty("phase", l.phase); x->setProperty("curve", arrToVar(l.curve)); lfos.add(juce::var(x));
    }
    for (auto& s : m.mseg)
    {
        auto* x = new Obj(); x->setProperty("n", s.numPoints); x->setProperty("t", arrToVar(s.t)); x->setProperty("v", arrToVar(s.v));
        x->setProperty("c", arrToVar(s.c)); x->setProperty("loop", s.loop); x->setProperty("len", s.lengthMs); msegs.add(juce::var(x));
    }
    for (auto& s : m.sh)
    {
        auto* x = new Obj(); x->setProperty("rate", s.rateHz); x->setProperty("sync", s.sync); x->setProperty("div", s.syncDiv);
        x->setProperty("slew", s.slewMs); shs.add(juce::var(x));
    }
    for (auto& s : m.chaos)
    {
        auto* x = new Obj(); x->setProperty("rate", s.rateHz); x->setProperty("r", s.r); x->setProperty("type", s.type); chaos.add(juce::var(x));
    }
    o->setProperty("lfo", lfos); o->setProperty("mseg", msegs); o->setProperty("sh", shs); o->setProperty("chaos", chaos);
    { auto* x = new Obj(); x->setProperty("rate", m.rw.rateHz); x->setProperty("step", m.rw.step); o->setProperty("rw", juce::var(x)); }
    { auto* x = new Obj(); x->setProperty("steps", arrToVar(m.step.steps)); x->setProperty("len", m.step.length); x->setProperty("rate", m.step.rateHz);
      x->setProperty("sync", m.step.sync); x->setProperty("div", m.step.syncDiv); x->setProperty("slew", m.step.slewMs); o->setProperty("step", juce::var(x)); }
    o->setProperty("macros", arrToVar(m.macros));
    o->setProperty("fa", m.followerAttackMs); o->setProperty("fr", m.followerReleaseMs);
    return juce::var(o);
}
void modsFromVar(const juce::var& v, ModulatorBank& m)
{
    if (auto* a = v["lfo"].getArray())
        for (int i = 0; i < kNumLfos && i < a->size(); ++i)
        { auto x = (*a)[i]; auto& l = m.lfo[(size_t) i];
          l.shape = getI(x, "shape", 0); l.rateHz = getF(x, "rate", 1.f); l.sync = getB(x, "sync", false); l.syncDiv = getI(x, "div", 4);
          l.phase = getF(x, "phase", 0.f); varToVec(x["curve"], l.curve.data(), kCurvePoints); }
    if (auto* a = v["mseg"].getArray())
        for (int i = 0; i < kNumMsegs && i < a->size(); ++i)
        { auto x = (*a)[i]; auto& s = m.mseg[(size_t) i];
          s.numPoints = std::clamp(getI(x, "n", 3), 2, kMsegPoints); varToVec(x["t"], s.t.data(), kMsegPoints);
          varToVec(x["v"], s.v.data(), kMsegPoints); varToVec(x["c"], s.c.data(), kMsegPoints);
          s.loop = getB(x, "loop", false); s.lengthMs = getF(x, "len", 500.f); }
    if (auto* a = v["sh"].getArray())
        for (int i = 0; i < kNumSH && i < a->size(); ++i)
        { auto x = (*a)[i]; auto& s = m.sh[(size_t) i]; s.rateHz = getF(x, "rate", 4.f); s.sync = getB(x, "sync", false);
          s.syncDiv = getI(x, "div", 4); s.slewMs = getF(x, "slew", 0.f); }
    if (auto* a = v["chaos"].getArray())
        for (int i = 0; i < kNumChaos && i < a->size(); ++i)
        { auto x = (*a)[i]; auto& s = m.chaos[(size_t) i]; s.rateHz = getF(x, "rate", 3.f); s.r = getF(x, "r", 3.8f); s.type = getI(x, "type", 0); }
    m.rw.rateHz = getF(v["rw"], "rate", 8.f); m.rw.step = getF(v["rw"], "step", 0.15f);
    varToVec(v["step"]["steps"], m.step.steps.data(), kStepCount);
    m.step.length = std::clamp(getI(v["step"], "len", 8), 1, kStepCount); m.step.rateHz = getF(v["step"], "rate", 6.f);
    m.step.sync = getB(v["step"], "sync", true); m.step.syncDiv = getI(v["step"], "div", 4); m.step.slewMs = getF(v["step"], "slew", 5.f);
    varToVec(v["macros"], m.macros.data(), kNumMacros);
    m.followerAttackMs = getF(v, "fa", 2.f); m.followerReleaseMs = getF(v, "fr", 80.f);
}

juce::var fxToVar(const FxConfig& f)
{
    auto* o = new Obj();
    o->setProperty("dDiv", f.delayDiv); o->setProperty("dFb", f.delayFeedback); o->setProperty("dTone", f.delayTone);
    o->setProperty("pp", f.pingPong); o->setProperty("dLvl", f.delayLevel);
    o->setProperty("rSize", f.reverbSize); o->setProperty("rDamp", f.reverbDamp); o->setProperty("rLvl", f.reverbLevel);
    o->setProperty("mType", f.modType); o->setProperty("mRate", f.modRate); o->setProperty("mDepth", f.modDepth);
    o->setProperty("mFb", f.modFeedback); o->setProperty("mLvl", f.modLevel);
    o->setProperty("width", f.width); o->setProperty("gain", f.masterGainDb); o->setProperty("lowCut", f.lowCutHz);
    return juce::var(o);
}
void fxFromVar(const juce::var& v, FxConfig& f)
{
    f.delayDiv = getI(v, "dDiv", 2); f.delayFeedback = getF(v, "dFb", .35f); f.delayTone = getF(v, "dTone", .6f);
    f.pingPong = getB(v, "pp", true); f.delayLevel = getF(v, "dLvl", .8f);
    f.reverbSize = getF(v, "rSize", .5f); f.reverbDamp = getF(v, "rDamp", .5f); f.reverbLevel = getF(v, "rLvl", .7f);
    f.modType = getI(v, "mType", 0); f.modRate = getF(v, "mRate", .4f); f.modDepth = getF(v, "mDepth", .5f);
    f.modFeedback = getF(v, "mFb", .3f); f.modLevel = getF(v, "mLvl", .7f);
    f.width = getF(v, "width", 1.f); f.masterGainDb = getF(v, "gain", 0.f); f.lowCutHz = getF(v, "lowCut", 30.f);
}
} // namespace

juce::var CandyDNA::toVar() const
{
    auto* o = new Obj();
    o->setProperty("id", idToString(id)); o->setProperty("gv", generatorVersion);
    o->setProperty("seed", (juce::int64) seed); o->setProperty("cat", (int) category); o->setProperty("arch", archetype);
    o->setProperty("name", name); o->setProperty("nl", numLayers);
    juce::Array<juce::var> ls; for (auto& l : layers) ls.add(layerToVar(l));
    o->setProperty("layers", ls);
    juce::Array<juce::var> rs;
    for (auto& r : routes)
    {
        auto* x = new Obj(); x->setProperty("s", r.source); x->setProperty("d", r.dest); x->setProperty("depth", r.depth);
        x->setProperty("curve", r.curve); x->setProperty("slew", r.slewMs); x->setProperty("sync", r.sync);
        x->setProperty("div", r.syncDiv); x->setProperty("on", r.enabled); rs.add(juce::var(x));
    }
    o->setProperty("routes", rs);
    o->setProperty("mods", modsToVar(mods)); o->setProperty("fx", fxToVar(fx));
    o->setProperty("dur", durationMs);
    { auto* x = new Obj(); x->setProperty("cen", spectral.centroidHz); x->setProperty("lo", spectral.lowFrac); x->setProperty("mid", spectral.midFrac);
      x->setProperty("hi", spectral.highFrac); x->setProperty("pk", spectral.peakDb); x->setProperty("rms", spectral.rmsDb);
      x->setProperty("flat", spectral.flatness); x->setProperty("crest", spectral.crest); x->setProperty("bands", vecToVar(spectral.bands.data(), 8)); o->setProperty("spectral", juce::var(x)); }
    { auto* x = new Obj(); x->setProperty("depth", motion.depth); x->setProperty("rate", motion.rate); x->setProperty("st", motion.stereoMotion);
      o->setProperty("motion", juce::var(x)); }
    o->setProperty("role", (int) role);
    { auto* x = new Obj(); x->setProperty("rating", meta.rating); x->setProperty("fav", meta.favorite); x->setProperty("rej", meta.rejected);
      x->setProperty("aud", meta.auditions); x->setProperty("tags", meta.tags); o->setProperty("meta", juce::var(x)); }
    o->setProperty("pA", idToString(parentA)); o->setProperty("pB", idToString(parentB)); o->setProperty("score", score);
    return juce::var(o);
}

bool CandyDNA::fromVar(const juce::var& v, CandyDNA& d)
{
    if (! v.isObject()) return false;
    d = CandyDNA();
    d.id = stringToId(getS(v, "id", "0")); d.generatorVersion = getI(v, "gv", 1);
    d.seed = (uint32_t) (juce::int64) v.getProperty("seed", 1);
    d.category = (Category) std::clamp(getI(v, "cat", 0), 0, kNumCategories - 1); d.archetype = getI(v, "arch", 0);
    d.name = getS(v, "name", "Candy"); d.numLayers = std::clamp(getI(v, "nl", 1), 1, kMaxLayers);
    if (auto* a = v["layers"].getArray())
        for (int i = 0; i < kMaxLayers && i < a->size(); ++i) layerFromVar((*a)[i], d.layers[(size_t) i]);
    if (auto* a = v["routes"].getArray())
        for (int i = 0; i < kNumModRoutes && i < a->size(); ++i)
        { auto x = (*a)[i]; auto& r = d.routes[(size_t) i];
          r.source = std::clamp(getI(x, "s", 0), 0, kNumModSources - 1); r.dest = std::clamp(getI(x, "d", 0), 0, kNumDestinations - 1);
          r.depth = getF(x, "depth", 0.f); r.curve = getI(x, "curve", 0); r.slewMs = getF(x, "slew", 0.f);
          r.sync = getB(x, "sync", false); r.syncDiv = getI(x, "div", 4); r.enabled = getB(x, "on", false); }
    modsFromVar(v["mods"], d.mods); fxFromVar(v["fx"], d.fx);
    d.durationMs = getF(v, "dur", 400.f);
    auto sp = v["spectral"];
    d.spectral = { {}, getF(sp, "cen", 2000.f), getF(sp, "lo", .2f), getF(sp, "mid", .5f), getF(sp, "hi", .3f),
                   getF(sp, "pk", -12.f), getF(sp, "rms", -24.f), getF(sp, "flat", .3f), getF(sp, "crest", 6.f) };
    varToVec(sp["bands"], d.spectral.bands.data(), 8);
    auto mo = v["motion"]; d.motion = { getF(mo, "depth", 0.f), getF(mo, "rate", 0.f), getF(mo, "st", 0.f) };
    d.role = (MusicalRole) std::clamp(getI(v, "role", 0), 0, 5);
    auto me = v["meta"]; d.meta.rating = getI(me, "rating", 0); d.meta.favorite = getB(me, "fav", false);
    d.meta.rejected = getB(me, "rej", false); d.meta.auditions = getI(me, "aud", 0); d.meta.tags = getS(me, "tags", "");
    d.parentA = stringToId(getS(v, "pA", "0")); d.parentB = stringToId(getS(v, "pB", "0")); d.score = getF(v, "score", 0.f);
    return true;
}

juce::var SeqConfig::toVar() const
{
    auto* o = new Obj();
    o->setProperty("mode", (int) mode); o->setProperty("bars", bars); o->setProperty("div", division); o->setProperty("feel", feel);
    o->setProperty("swing", swing); o->setProperty("prob", probability); o->setProperty("ratP", ratchetProb); o->setProperty("ratMax", ratchetMax);
    o->setProperty("micro", microtimingMs); o->setProperty("vb", velBase); o->setProperty("vr", velRange); o->setProperty("pv", pitchVariation);
    o->setProperty("len", lengthBeats); o->setProperty("eS", euclidSteps); o->setProperty("eH", euclidHits); o->setProperty("eR", euclidRotate);
    o->setProperty("evolve", evolveAmount); o->setProperty("freeze", freezeAll); o->setProperty("seed", (juce::int64) seed); o->setProperty("on", enabled);
    juce::Array<juce::var> ev;
    for (auto& e : events)
    {
        auto* x = new Obj(); x->setProperty("pos", e.position); x->setProperty("vel", e.velocity); x->setProperty("slot", e.slot);
        x->setProperty("pitch", e.pitchOffset); x->setProperty("len", e.lengthBeats); x->setProperty("ms", (juce::int64) e.modSeed);
        x->setProperty("prob", e.probability); x->setProperty("fz", e.frozen); x->setProperty("mu", e.muted); x->setProperty("lk", e.locked);
        x->setProperty("micro", e.microMs); ev.add(juce::var(x));
    }
    o->setProperty("events", ev);
    return juce::var(o);
}
bool SeqConfig::fromVar(const juce::var& v, SeqConfig& s)
{
    if (! v.isObject()) return false;
    s = SeqConfig();
    s.mode = (SeqMode) std::clamp(getI(v, "mode", 0), 0, (int) SeqMode::Count - 1); s.bars = std::clamp(getI(v, "bars", 4), 1, 32);
    s.division = std::clamp(getI(v, "div", 2), 0, kNumDivisions - 1); s.feel = std::clamp(getI(v, "feel", 0), 0, 2);
    s.swing = getF(v, "swing", 0.f); s.probability = getF(v, "prob", .7f); s.ratchetProb = getF(v, "ratP", .1f); s.ratchetMax = getI(v, "ratMax", 3);
    s.microtimingMs = getF(v, "micro", 4.f); s.velBase = getF(v, "vb", .8f); s.velRange = getF(v, "vr", .25f); s.pitchVariation = getF(v, "pv", 2.f);
    s.lengthBeats = getF(v, "len", .25f); s.euclidSteps = getI(v, "eS", 16); s.euclidHits = getI(v, "eH", 5); s.euclidRotate = getI(v, "eR", 0);
    s.evolveAmount = getF(v, "evolve", .35f); s.freezeAll = getB(v, "freeze", false);
    s.seed = (uint32_t) (juce::int64) v.getProperty("seed", 1); s.enabled = getB(v, "on", true);
    if (auto* a = v["events"].getArray())
        for (auto& x : *a)
        {
            SeqEvent e; e.position = (double) x["pos"]; e.velocity = getF(x, "vel", .8f); e.slot = std::clamp(getI(x, "slot", 0), 0, kNumSoundSlots - 1);
            e.pitchOffset = getF(x, "pitch", 0.f); e.lengthBeats = getF(x, "len", .25f); e.modSeed = (uint32_t) (juce::int64) x.getProperty("ms", 0);
            e.probability = getF(x, "prob", 1.f); e.frozen = getB(x, "fz", false); e.muted = getB(x, "mu", false); e.locked = getB(x, "lk", false);
            e.microMs = getF(x, "micro", 0.f); s.events.push_back(e);
        }
    return true;
}

juce::var GenSettings::toVar() const
{
    auto* o = new Obj();
    o->setProperty("cx", complexity); o->setProperty("mo", motion); o->setProperty("sp", space); o->setProperty("de", density);
    o->setProperty("ch", chaos); o->setProperty("br", brightness); o->setProperty("mu", musicality); o->setProperty("lm", lengthMode);
    o->setProperty("cl", customLengthMs); o->setProperty("cat", (int) category); o->setProperty("ns", negSpaceSensitivity);
    o->setProperty("root", rootNote); o->setProperty("scale", scale);
    o->setProperty("fa", familyA); o->setProperty("fb", familyB); o->setProperty("spice", spice); o->setProperty("sync", syncLength);
    return juce::var(o);
}
bool GenSettings::fromVar(const juce::var& v, GenSettings& g)
{
    if (! v.isObject()) return false;
    g = GenSettings();
    g.complexity = getF(v, "cx", .65f); g.motion = getF(v, "mo", .75f); g.space = getF(v, "sp", .45f); g.density = getF(v, "de", .38f);
    g.chaos = getF(v, "ch", .4f); g.brightness = getF(v, "br", .5f); g.musicality = getF(v, "mu", .65f); g.lengthMode = getI(v, "lm", 1);
    g.customLengthMs = getF(v, "cl", 800.f); g.category = (Category) std::clamp(getI(v, "cat", 0), 0, kNumCategories - 1);
    g.negSpaceSensitivity = getF(v, "ns", .5f);
    g.rootNote = std::clamp(getI(v, "root", 9), 0, 11); g.scale = std::clamp(getI(v, "scale", 1), 0, 5);
    g.familyA = std::clamp(getI(v, "fa", -1), -1, 5); g.familyB = std::clamp(getI(v, "fb", -1), -1, 5); g.spice = clamp01(getF(v, "spice", 0.0f));
    g.syncLength = getI(v, "sync", 0) != 0;
    return true;
}

} // namespace dali
