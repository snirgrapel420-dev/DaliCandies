#pragma once
// Host-automatable parameters.  Parameter IDs are part of the saved-state contract: never rename or reuse them.
#include "Core/CandyDNA.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace dali::pid
{
// generation macros (percent)
inline constexpr const char* complexity = "complexity";
inline constexpr const char* motion = "motion";
inline constexpr const char* space = "space";
inline constexpr const char* density = "density";
inline constexpr const char* chaos = "chaos";
inline constexpr const char* brightness = "brightness";
inline constexpr const char* musicality = "musicality";
inline constexpr const char* lengthMode = "lengthMode";
inline constexpr const char* lengthMs = "lengthMs";
inline constexpr const char* category = "category";
inline constexpr const char* negSpace = "negSpace";
inline constexpr const char* rootNote = "rootNote";
inline constexpr const char* scale = "scale";
// sequencer
inline constexpr const char* seqOn = "seqOn";
inline constexpr const char* seqMode = "seqMode";
inline constexpr const char* seqBars = "seqBars";
inline constexpr const char* seqDiv = "seqDiv";
inline constexpr const char* seqFeel = "seqFeel";
inline constexpr const char* seqSwing = "seqSwing";
inline constexpr const char* seqProb = "seqProb";
inline constexpr const char* seqRatchetProb = "seqRatchetProb";
inline constexpr const char* seqRatchetMax = "seqRatchetMax";
inline constexpr const char* seqMicro = "seqMicro";
inline constexpr const char* seqVel = "seqVel";
inline constexpr const char* seqVelRange = "seqVelRange";
inline constexpr const char* seqPitchVar = "seqPitchVar";
inline constexpr const char* seqLength = "seqLength";
inline constexpr const char* euSteps = "euSteps";
inline constexpr const char* euHits = "euHits";
inline constexpr const char* euRotate = "euRotate";
inline constexpr const char* seqEvolve = "seqEvolve";
inline constexpr const char* seqFreeze = "seqFreeze";
inline constexpr const char* seqFreeRun = "seqFreeRun";
inline constexpr const char* variety = "variety";    // how widely the sequence spreads over the eight plates
// performance macros feeding the modulation matrix
inline constexpr const char* macro1 = "macro1";
inline constexpr const char* macro2 = "macro2";
inline constexpr const char* macro3 = "macro3";
inline constexpr const char* macro4 = "macro4";
// audio
inline constexpr const char* quality = "quality";
inline constexpr const char* outGain = "outGain";
inline constexpr const char* wet = "wet";
inline constexpr const char* dry = "dry";
inline constexpr const char* adapt = "adapt";
inline constexpr const char* anaSource = "anaSource";
inline constexpr const char* capBars = "capBars";
inline constexpr const char* anaBpm = "anaBpm";
inline constexpr const char* liveNeg = "liveNeg";
// Kitchen screen
inline constexpr const char* famA = "famA";          // 0 = off (use Category), 1..6 = SNAP, CHIME, TWITCH, SWIRL, MOSS, HAZE
inline constexpr const char* famB = "famB";          // 0 = none, 1..6 = second family (fusion)
inline constexpr const char* lenSync = "lenSync";    // 0 = free (Length / Custom Length), else a tempo-synced window
inline constexpr const char* spice = "spice";        // extra weirdness and motion
inline constexpr const char* drift = "drift";        // evolution per repeat
inline constexpr const char* keepSkel = "keepSkel";  // KEEP: generate variations of the current sound
inline constexpr const char* listenOn = "listenOn";  // LISTEN: steer generation toward the gaps of an analysed track
}

namespace dali
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(bool isFx);
GenSettings genSettingsFrom(juce::AudioProcessorValueTreeState& s, double hostBpm = 0.0);   // hostBpm > 0 resolves a tempo-synced Length
double lenSyncBeats(int index);                       // beats of the Length choice (0 for Free)
const char* lenSyncName(int index);
constexpr int kNumLenSync = 8;
SeqConfig   seqSettingsFrom(juce::AudioProcessorValueTreeState& s, const SeqConfig& previous);
const char* rootNoteName(int i);
}
