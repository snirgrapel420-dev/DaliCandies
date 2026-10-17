#pragma once
// Captured analysis (worker thread): rhythm / spectrum / dynamics maps on a 1/16 grid, repetition,
// section estimation, opportunity map and user-editable placement suggestions.
#include "AudioFeatures.h"
#include "NegativeSpace.h"
#include <atomic>
#include <functional>

namespace dali
{
constexpr int kWaveformPoints = 1024;
constexpr int kSpectrumBins = 96;

struct Suggestion
{
    int   step = 0;                    // 1/16-note step index from the start of the captured region
    float score = 0.0f;
    float rhythm = 0, spectral = 0, dynamic = 0, phrase = 0, novelty = 0;
    bool  accepted = false, rejected = false, locked = false;
};

struct TrackAnalysis
{
    bool   valid = false;
    double sampleRate = 44100.0, bpm = 120.0;
    int    beatsPerBar = 4, stepsPerBar = 16, bars = 0, numSteps = 0;
    std::vector<float> rms, peak, centroid, flux, onset, transient, activity, phrasePos, opportunity;
    std::array<std::vector<float>, kNumBands> band;      // per step, normalised 0..1 (1 = loudest step for that band)
    std::vector<int>   sectionBars;                      // bar indices where a new section is estimated to start
    std::vector<float> barRepetition;                    // per bar: similarity to a previous bar (0..1)
    std::vector<float> waveform;                         // min/max pairs, 2 * kWaveformPoints
    std::vector<float> spectrum;                         // kSpectrumBins values (dB), log-spaced 20 Hz - 20 kHz
    float rmsDb = -120, peakDb = -120, dynamicDensity = 0, rhythmicActivity = 0, centroidHz = 0;
    std::array<float, kNumBands> bandDb {};              // average level per band (dB)
    std::vector<Suggestion> suggestions;
    int   keyRoot = -1; bool keyMinor = true; float keyConfidence = 0.0f;   // detected key (keyRoot < 0 = unknown)
    uint32_t revision = 0;
};

// Heavy analysis.  `cancel` is polled regularly; returns an invalid analysis if cancelled.
TrackAnalysis analyzeTrack(const float* mono, int n, double sampleRate, double bpm, int beatsPerBar,
                           const std::atomic<bool>* cancel = nullptr, const std::function<void(float)>& progress = {});

// Estimates the key from the pitched content (Goertzel chroma + Krumhansl profiles).  Cheap enough for the worker thread.
void detectKey(TrackAnalysis& a, const float* mono, int n);

// (Re)computes the opportunity map from the analysis features.  Cheap.
void computeOpportunity(TrackAnalysis& a);

// Generates placement suggestions.  Suggestions that were accepted or locked by the user are preserved,
// rejected ones are not proposed again.  density 0..1, sensitivity 0..1 (negative space).
void generateSuggestions(TrackAnalysis& a, float density, float negSpaceSensitivity, uint32_t seed);

// Negative-space context for a step of the analysed track (used by the sequencer and by suggestions)
NegSpaceContext contextAt(const TrackAnalysis& a, int step, const std::array<float, kNumBands>& candyBands,
                          const std::vector<int>& plannedSteps);

} // namespace dali
