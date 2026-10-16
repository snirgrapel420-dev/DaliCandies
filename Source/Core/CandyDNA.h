#pragma once
// CandyDNA: the complete, serialisable description of one ear-candy sound.
// All engine parameters are stored normalised (0..1) so that the generator,
// mutation, breeding, modulation and GUI can treat them uniformly.
#include "Types.h"
#include "Rng.h"
#include <vector>
#include <string>

namespace dali
{
struct LayerConfig
{
    bool        active   = false;
    EngineType  engine   = EngineType::MicroSynth;
    float       gainDb   = -6.0f;        // -60..+6
    float       pitch    = 60.0f;        // base MIDI note (fractional)
    float       pan      = 0.0f;         // -1..1
    float       attackMs = 1.0f, decayMs = 120.0f, sustain = 0.0f, releaseMs = 60.0f;
    float       startOffsetMs = 0.0f;
    float       durationMs    = 300.0f;  // note-on length (release adds tail)
    float       sendDelay = 0.0f, sendReverb = 0.0f, sendMod = 0.0f;   // 0..1
    uint32_t    seed     = 1;
    bool        mute = false, solo = false, locked = false;
    uint16_t    lockMask = 0;            // per-parameter lock bits (generator keeps locked params)
    InsertFx    insert   = InsertFx::None;
    float       insertA  = 0.5f, insertB = 0.5f;
    std::array<float, kParamsPerLayer> p {};   // engine parameters, normalised
};

struct LfoCfg   { int shape = 0; float rateHz = 1.0f; bool sync = false; int syncDiv = 4; float phase = 0.0f; std::array<float, kCurvePoints> curve {}; };
struct MsegCfg  { int numPoints = 3; std::array<float, kMsegPoints> t {}, v {}, c {}; bool loop = false; float lengthMs = 500.0f; };
struct SHCfg    { float rateHz = 4.0f; bool sync = false; int syncDiv = 4; float slewMs = 0.0f; };
struct ChaosCfg { float rateHz = 3.0f; float r = 3.8f; int type = 0; };
struct RWCfg    { float rateHz = 8.0f; float step = 0.15f; };
struct StepCfg  { std::array<float, kStepCount> steps {}; int length = 8; float rateHz = 6.0f; bool sync = true; int syncDiv = 4; float slewMs = 5.0f; };

struct ModulatorBank
{
    std::array<LfoCfg, kNumLfos>   lfo;
    std::array<MsegCfg, kNumMsegs> mseg;
    std::array<SHCfg, kNumSH>      sh;
    std::array<ChaosCfg, kNumChaos> chaos;
    RWCfg   rw;
    StepCfg step;
    std::array<float, kNumMacros> macros { 0.5f, 0.5f, 0.5f, 0.5f };
    float   followerAttackMs = 2.0f, followerReleaseMs = 80.0f;
};

struct ModRoute
{
    int   source  = 0;
    int   dest    = 0;
    float depth   = 0.0f;     // bipolar -1..1
    int   curve   = 0;        // 0 linear, 1 exponential, 2 logarithmic, 3 s-curve
    float slewMs  = 0.0f;
    bool  sync    = false;    // quantise route to tempo grid (sample & hold at syncDiv)
    int   syncDiv = 4;        // steps per beat for sync
    bool  enabled = false;
};

struct FxConfig
{
    // Delay (shared send bus)
    int   delayDiv = 2;          // division index (see divisionBeats)
    float delayFeedback = 0.35f, delayTone = 0.6f;
    bool  pingPong = true;
    float delayLevel = 0.8f;
    // Reverb
    float reverbSize = 0.5f, reverbDamp = 0.5f, reverbLevel = 0.7f;
    // Modulation FX bus: 0 chorus 1 flanger 2 phaser
    int   modType = 0;
    float modRate = 0.4f, modDepth = 0.5f, modFeedback = 0.3f, modLevel = 0.7f;
    // Master
    float width = 1.0f, masterGainDb = 0.0f, lowCutHz = 30.0f;
};

struct SpectralChars { std::array<float, 8> bands {}; float centroidHz = 2000.0f; float lowFrac = 0.2f, midFrac = 0.5f, highFrac = 0.3f;
                       float peakDb = -12.0f, rmsDb = -24.0f, flatness = 0.3f, crest = 6.0f; };
struct MotionChars   { float depth = 0.0f, rate = 0.0f, stereoMotion = 0.0f; };
struct UserMeta      { int rating = 0; bool favorite = false; bool rejected = false; int auditions = 0; juce::String tags; };

struct CandyDNA
{
    uint64_t     id = 0;
    int          generatorVersion = kGeneratorVersion;
    uint32_t     seed = 1;
    Category     category = Category::MicroDetails;
    int          archetype = 0;
    juce::String name { "Init Candy" };
    int          numLayers = 1;
    std::array<LayerConfig, kMaxLayers> layers;
    std::array<ModRoute, kNumModRoutes> routes;
    ModulatorBank mods;
    FxConfig     fx;
    float        durationMs = 400.0f;
    SpectralChars spectral;
    MotionChars   motion;
    MusicalRole  role = MusicalRole::Accent;
    UserMeta     meta;
    uint64_t     parentA = 0, parentB = 0;
    float        score = 0.0f;           // generator quality score (not user rating)

    int activeLayerCount() const { int n = 0; for (auto& l : layers) n += l.active ? 1 : 0; return n; }
    juce::var toVar() const;
    static bool fromVar(const juce::var& v, CandyDNA& out);
};

struct SeqEvent
{
    double position = 0.0;     // beats from loop start
    float  velocity = 0.8f;
    int    slot = 0;           // sound slot (per-event sound selection)
    float  pitchOffset = 0.0f; // semitones
    float  lengthBeats = 0.25f;
    uint32_t modSeed = 0;      // per-event modulation variation
    float  probability = 1.0f;
    bool   frozen = false, muted = false, locked = false;
    float  microMs = 0.0f;
};

struct SeqConfig
{
    SeqMode mode = SeqMode::Sparse;
    int   bars = 4;               // 1..32
    int   division = 2;           // division index: 0=1/4 1=1/8 2=1/16 3=1/32 4=1/64
    int   feel = 0;               // 0 straight, 1 triplet, 2 dotted
    float swing = 0.0f;           // 0..1
    float probability = 0.7f;
    float ratchetProb = 0.1f; int ratchetMax = 3;
    float microtimingMs = 4.0f;
    float velBase = 0.8f, velRange = 0.25f;
    float pitchVariation = 2.0f;  // semitones
    float lengthBeats = 0.25f;
    int   euclidSteps = 16, euclidHits = 5, euclidRotate = 0;
    float evolveAmount = 0.35f;   // phrase evolution strength
    bool  freezeAll = false;
    uint32_t seed = 1;
    bool  enabled = true;
    std::vector<SeqEvent> events; // generated/edited base pattern (frozen events survive regeneration)
    juce::var toVar() const;
    static bool fromVar(const juce::var& v, SeqConfig& out);
};

struct GenSettings
{
    float complexity = 0.65f, motion = 0.75f, space = 0.45f, density = 0.38f,
          chaos = 0.40f, brightness = 0.50f, musicality = 0.65f;
    int   lengthMode = 1;          // 0 short 1 medium 2 long 3 custom
    float customLengthMs = 800.0f;
    Category category = Category::MicroDetails;
    float negSpaceSensitivity = 0.5f;
    int   rootNote = 9;            // pitch class 0..11 (9 = A)
    int   scale = 1;               // 0 minor, 1 phrygian, 2 harmonic minor, 3 minor pentatonic, 4 phrygian dominant, 5 chromatic
    // Kitchen screen: sound families (0..5, -1 = use `category`), optional second family = fusion, spice, tempo-synced length
    int   familyA = -1, familyB = -1;
    float spice = 0.0f;            // 0..1: extra weirdness and motion
    bool  syncLength = false;      // customLengthMs is a tempo-derived window the sound must fill (fitted and verified)
    juce::var toVar() const;
    static bool fromVar(const juce::var& v, GenSettings& out);
};

// Tempo-sync divisions (index: 0=1/4, 1=1/8, 2=1/16, 3=1/32, 4=1/64), optionally triplet / dotted.
constexpr int kNumDivisions = 5;
inline double divisionBeats(int idx, int feel)
{
    static const double base[kNumDivisions] = { 1.0, 0.5, 0.25, 0.125, 0.0625 };
    double b = base[std::clamp(idx, 0, kNumDivisions - 1)];
    if (feel == 1) b *= 2.0 / 3.0;        // triplet
    else if (feel == 2) b *= 1.5;         // dotted
    return b;
}
const char* divisionName(int idx);

// Length presets for generation
float lengthMsFor(const GenSettings& g, Category c);

juce::var vecToVar(const float* d, int n);
void varToVec(const juce::var& v, float* d, int n);
juce::String idToString(uint64_t id);
uint64_t stringToId(const juce::String& s);

} // namespace dali
