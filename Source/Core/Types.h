#pragma once
// DaliCandy - core constants and enumerations shared by every module.
#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace dali
{
constexpr int kMaxLayers        = 4;
constexpr int kParamsPerLayer   = 16;
constexpr int kNumModRoutes     = 32;
constexpr int kNumLfos          = 4;
constexpr int kNumMsegs         = 4;
constexpr int kNumSH            = 2;
constexpr int kNumChaos         = 2;
constexpr int kNumMacros        = 4;
constexpr int kMsegPoints       = 8;
constexpr int kStepCount        = 16;
constexpr int kCurvePoints      = 16;
constexpr int kMaxVoices        = 8;     // concurrent candy events (each up to 4 layers)
constexpr int kMaxBlock         = 2048;  // maximum internal processing block
constexpr int kControlRate      = 16;    // samples per modulation/control update
constexpr int kNumSoundSlots    = 8;     // per-event sound selection bank
constexpr int kGeneratorVersion = 1;
constexpr int kStateVersion     = 1;

enum class EngineType : int { MicroSynth = 0, Resonator, Granular, Glitch, Spectral, NoiseMotion, Count };
constexpr int kNumEngines = (int) EngineType::Count;

enum class Category : int { MicroDetails = 0, PsychedelicMovement, OrganicTextures, MetallicAccents,
                            RhythmicGlitches, AtmosphericCandy, Transitions, Experimental, Count };
constexpr int kNumCategories = (int) Category::Count;

enum class MusicalRole : int { Accent = 0, Fill, Texture, Movement, Transition, Punctuation, Count };

enum class InsertFx : int { None = 0, Saturation, Wavefolder, Bitcrusher, RingMod, FreqShift, Comb, Filter, Transient, Count };

enum class SeqMode : int { Sparse = 0, GrooveFollower, CallResponse, ControlledChaos, PhraseEvolution,
                           Euclidean, ProbabilitySequence, TransitionBuilder, Count };

enum class QualityMode : int { Eco = 0, Standard, High, Count };

enum ModSource : int
{
    kSrcLfo1 = 0, kSrcLfo2, kSrcLfo3, kSrcLfo4,
    kSrcMseg1, kSrcMseg2, kSrcMseg3, kSrcMseg4,
    kSrcSH1, kSrcSH2, kSrcChaos1, kSrcChaos2,
    kSrcRandomWalk, kSrcStep, kSrcEnvFollower, kSrcTransient,
    kSrcVelocity, kSrcNote, kSrcModWheel, kSrcAftertouch,
    kSrcMacro1, kSrcMacro2, kSrcMacro3, kSrcMacro4,
    kNumModSources
};

// Modulation destinations
//   0..63    : engine parameter  (layer * 16 + paramIndex)   [normalised 0..1 domain]
//   64..95   : per-layer controls (64 + layer*8 + k) with k in LayerDest
//   96..     : global controls
enum LayerDest : int { kLdGain = 0, kLdPan, kLdPitch, kLdSendDelay, kLdSendReverb, kLdSendMod, kLdInsertA, kLdInsertB };
enum GlobalDest : int { kGdDelayFeedback = 96, kGdReverbSize, kGdModFxDepth, kGdWidth, kGdMasterGain, kGdDelayTime, kNumDestinations };
constexpr int kLayerDestBase = 64;

inline int layerParamDest(int layer, int param) { return layer * kParamsPerLayer + param; }
inline int layerCtlDest(int layer, int ctl)       { return kLayerDestBase + layer * 8 + ctl; }

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline float midiToHz(float n) { return 440.0f * std::exp2((n - 69.0f) * (1.0f / 12.0f)); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Flush denormals / NaNs defensively in feedback paths.
inline float sanitize(float x) noexcept
{
    // branch-light: NaN/inf compare false against the bound
    return (std::abs(x) < 1.0e6f) ? x : 0.0f;
}
inline float killDenormal(float x) noexcept { return (std::abs(x) < 1.0e-20f) ? 0.0f : x; }

const char* engineName(EngineType e);
const char* categoryName(Category c);
const char* roleName(MusicalRole r);
const char* insertName(InsertFx f);
const char* seqModeName(SeqMode m);
const char* modSourceName(int s);
juce::String modDestName(int d);
const char* engineParamName(EngineType e, int idx);

} // namespace dali
