#include "Params.h"
#include "Generator/Scale.h"
#include "Generator/Families.h"

namespace dali
{
using namespace juce;
namespace
{
using Attr = AudioParameterFloatAttributes;
std::unique_ptr<AudioParameterFloat> pct(const char* id, const char* name, float def)
{
    return std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(0.0f, 100.0f, 0.1f), def,
        Attr().withStringFromValueFunction([](float v, int) { return String(v, 0) + " %"; }).withValueFromStringFunction([](const String& s) { return s.getFloatValue(); }));
}
std::unique_ptr<AudioParameterChoice> choice(const char* id, const char* name, StringArray c, int def)
{ return std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, c, def); }
std::unique_ptr<AudioParameterInt> integer(const char* id, const char* name, int lo, int hi, int def)
{ return std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, lo, hi, def); }
std::unique_ptr<AudioParameterBool> boolean(const char* id, const char* name, bool def)
{ return std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, def); }
std::unique_ptr<AudioParameterFloat> flt(const char* id, const char* name, float lo, float hi, float def, const char* unit, float skew = 1.0f, float step = 0.01f)
{
    NormalisableRange<float> r(lo, hi, step); r.setSkewForCentre(skew > 0 ? lo + (hi - lo) * skew : (lo + hi) * 0.5f);
    const String u(unit);
    return std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def,
        Attr().withStringFromValueFunction([u](float v, int) { return String(v, 1) + " " + u; }).withValueFromStringFunction([](const String& s) { return s.getFloatValue(); }));
}
}

const char* rootNoteName(int i) { return noteName(i); }

const char* lenSyncName(int i)
{
    static const char* n[kNumLenSync] = { "Free", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
    return n[std::clamp(i, 0, kNumLenSync - 1)];
}
double lenSyncBeats(int i)
{
    static const double b[kNumLenSync] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };     // 4/4
    return b[std::clamp(i, 0, kNumLenSync - 1)];
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout(bool isFx)
{
    AudioProcessorValueTreeState::ParameterLayout l;
    using namespace pid;
    {   auto g = std::make_unique<AudioProcessorParameterGroup>("gen", "Generate", "|");
        g->addChild(pct(complexity, "Complexity", 65), pct(motion, "Motion", 75), pct(space, "Space", 45), pct(density, "Density", 38),
                    pct(chaos, "Chaos", 40), pct(brightness, "Brightness", 50), pct(musicality, "Musicality", 65));
        g->addChild(choice(lengthMode, "Length", { "Short", "Medium", "Long", "Custom" }, 1));
        g->addChild(flt(lengthMs, "Custom Length", 20.0f, 8000.0f, 800.0f, "ms", 0.15f, 1.0f));
        StringArray cats; for (int i = 0; i < kNumCategories; ++i) cats.add(categoryName((Category) i));
        g->addChild(choice(category, "Category", cats, 0));
        g->addChild(pct(negSpace, "Negative Space", 50));
        { StringArray fa { "Off (Category)" }, fb { "None" }; for (int i = 0; i < kNumFamilies; ++i) { fa.add(familyName(i)); fb.add(familyName(i)); }
          g->addChild(choice(famA, "Family", fa, 0), choice(famB, "Fusion Family", fb, 0)); }
        { StringArray ls; for (int i = 0; i < kNumLenSync; ++i) ls.add(lenSyncName(i)); g->addChild(choice(lenSync, "Synced Length", ls, 0)); }
        g->addChild(pct(spice, "Spice", 0), pct(drift, "Drift", 0));
        g->addChild(boolean(keepSkel, "Keep Skeleton", false), boolean(listenOn, "Listen", false));
        g->addChild(boolean(autoListen, "Auto Listen", isFx));
        StringArray roots; for (int i = 0; i < 12; ++i) roots.add(noteName(i));
        g->addChild(choice(rootNote, "Root Note", roots, 9));
        StringArray scales; for (int i = 0; i < 7; ++i) scales.add(dali::scaleName(i));
        g->addChild(choice(scale, "Scale", scales, 1));
        l.add(std::move(g)); }
    {   auto g = std::make_unique<AudioProcessorParameterGroup>("seq", "Sequence", "|");
        g->addChild(boolean(seqOn, "Sequencer", false));
        StringArray modes; for (int i = 0; i < (int) SeqMode::Count; ++i) modes.add(seqModeName((SeqMode) i));
        g->addChild(choice(seqMode, "Mode", modes, 0));
        g->addChild(integer(seqBars, "Bars", 1, 32, 4));
        StringArray divs; for (int i = 0; i < kNumDivisions; ++i) divs.add(divisionName(i));
        g->addChild(choice(seqDiv, "Division", divs, 2));
        g->addChild(choice(seqFeel, "Feel", { "Straight", "Triplet", "Dotted" }, 0));
        g->addChild(pct(seqSwing, "Swing", 0), pct(seqProb, "Probability", 70), pct(seqRatchetProb, "Ratchet", 10));
        g->addChild(integer(seqRatchetMax, "Ratchet Max", 2, 8, 3));
        g->addChild(flt(seqMicro, "Microtiming", 0.0f, 30.0f, 4.0f, "ms", -1.0f, 0.1f));
        g->addChild(pct(seqVel, "Velocity", 80), pct(seqVelRange, "Velocity Range", 25));
        g->addChild(flt(seqPitchVar, "Pitch Variation", 0.0f, 12.0f, 2.0f, "st", -1.0f, 0.5f));
        g->addChild(flt(seqLength, "Event Length", 0.0625f, 4.0f, 0.25f, "beats", 0.3f, 0.0625f));
        g->addChild(integer(euSteps, "Euclid Steps", 2, 32, 16), integer(euHits, "Euclid Hits", 0, 32, 5), integer(euRotate, "Euclid Rotate", 0, 31, 0));
        g->addChild(pct(seqEvolve, "Evolve", 35));
        g->addChild(boolean(seqFreeze, "Freeze Pattern", false));
        g->addChild(boolean(seqFreeRun, "Free Run", false));
        g->addChild(pct(variety, "Variety", 60));
        l.add(std::move(g)); }
    {   auto g = std::make_unique<AudioProcessorParameterGroup>("mod", "Macros", "|");
        g->addChild(pct(macro1, "Macro 1", 50), pct(macro2, "Macro 2", 50), pct(macro3, "Macro 3", 50), pct(macro4, "Macro 4", 50));
        l.add(std::move(g)); }
    {   auto g = std::make_unique<AudioProcessorParameterGroup>("audio", "Audio", "|");
        g->addChild(choice(quality, "Quality", { "Eco", "Standard", "High" }, 1));
        g->addChild(flt(outGain, "Output Gain", -24.0f, 6.0f, 0.0f, "dB", -1.0f, 0.1f));
        g->addChild(pct(wet, "Candy Level", 100));
        if (isFx) g->addChild(pct(dry, "Dry Level", 100));
        g->addChild(pct(adapt, "Adaptive Mix", 50));
        if (isFx) g->addChild(choice(anaSource, "Analysis Source", { "Main Input", "Sidechain" }, 0));
        g->addChild(choice(capBars, "Capture Length", { "4 bars", "8 bars", "16 bars", "32 bars" }, 1));
        g->addChild(flt(anaBpm, "Analysis BPM", 40.0f, 240.0f, 120.0f, "bpm", -1.0f, 0.1f));
        g->addChild(boolean(liveNeg, "Live Negative Space", true));
        l.add(std::move(g)); }
    return l;
}

GenSettings genSettingsFrom(AudioProcessorValueTreeState& s, double hostBpm)
{
    using namespace pid;
    auto v = [&](const char* id) { return s.getRawParameterValue(id)->load(); };
    GenSettings g;
    g.complexity = v(complexity) * 0.01f; g.motion = v(motion) * 0.01f; g.space = v(space) * 0.01f; g.density = v(density) * 0.01f;
    g.chaos = v(chaos) * 0.01f; g.brightness = v(brightness) * 0.01f; g.musicality = v(musicality) * 0.01f;
    g.lengthMode = (int) v(lengthMode); g.customLengthMs = v(lengthMs); g.category = (Category) std::clamp((int) v(category), 0, kNumCategories - 1);
    g.negSpaceSensitivity = v(negSpace) * 0.01f; g.rootNote = (int) v(rootNote); g.scale = (int) v(scale);
    g.familyA = (int) v(famA) - 1; g.familyB = (int) v(famB) - 1; g.spice = v(spice) * 0.01f;
    if (g.familyA < 0) g.familyB = -1;
    const double beats = lenSyncBeats((int) v(lenSync));
    if (beats > 0.0 && hostBpm > 0.0) { g.syncLength = true; g.lengthMode = 3; g.customLengthMs = (float) std::clamp(beats * 60000.0 / hostBpm, 20.0, 20000.0); }
    return g;
}

SeqConfig seqSettingsFrom(AudioProcessorValueTreeState& s, const SeqConfig& prev)
{
    using namespace pid;
    auto v = [&](const char* id) { return s.getRawParameterValue(id)->load(); };
    SeqConfig c = prev;
    c.enabled = v(seqOn) > 0.5f; c.mode = (SeqMode) std::clamp((int) v(seqMode), 0, (int) SeqMode::Count - 1);
    c.bars = (int) v(seqBars); c.division = (int) v(seqDiv); c.feel = (int) v(seqFeel);
    c.swing = v(seqSwing) * 0.01f; c.probability = v(seqProb) * 0.01f; c.ratchetProb = v(seqRatchetProb) * 0.01f; c.ratchetMax = (int) v(seqRatchetMax);
    c.microtimingMs = v(seqMicro); c.velBase = v(seqVel) * 0.01f; c.velRange = v(seqVelRange) * 0.01f; c.pitchVariation = v(seqPitchVar);
    c.lengthBeats = v(seqLength); c.euclidSteps = (int) v(euSteps); c.euclidHits = (int) v(euHits); c.euclidRotate = (int) v(euRotate);
    c.evolveAmount = v(seqEvolve) * 0.01f; c.freezeAll = v(seqFreeze) > 0.5f;
    return c;
}
}
