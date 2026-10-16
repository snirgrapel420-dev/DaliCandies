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
}

namespace dali
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(bool isFx);
GenSettings genSettingsFrom(juce::AudioProcessorValueTreeState& s);
SeqConfig   seqSettingsFrom(juce::AudioProcessorValueTreeState& s, const SeqConfig& previous);
const char* rootNoteName(int i);
}
