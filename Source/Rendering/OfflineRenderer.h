#pragma once
// Offline (non-realtime) rendering of Candy sounds and sequences.  Used by the generator (candidate
// previews), the audition system and every export path.  Rendering is deterministic: identical DNA,
// options and sample rate produce bit-identical audio regardless of the block size.
#include "DSP/CandyEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace dali
{
struct RenderOptions
{
    double sampleRate = 44100.0;
    QualityMode quality = QualityMode::High;
    int    blockSize = 512;
    float  velocity = 0.9f;
    float  pitchOffset = 0.0f;
    uint32_t eventSeed = 0;
    float  tailMs = -1.0f;            // < 0: automatic (derived from delay / reverb settings)
    bool   dry = false;               // skip send effects
    int    soloLayer = -1;            // >= 0: render only this layer
    bool   trimSilence = false;       // trim trailing digital silence (keeps tails above -90 dB)
    double bpm = 140.0;
    const SourceBuffer* external = nullptr;
};

class OfflineRenderer
{
public:
    OfflineRenderer();
    // One-shot: a single event at t = 0.
    juce::AudioBuffer<float> renderOneShot(const CandyDNA& dna, const RenderOptions& o);
    // Sequence: events (positions in beats from 0) for a number of bars.  slots[i] are the sounds for slot i
    // (slot 0 must be set).  loop = true wraps the tail back to the start so the result loops seamlessly.
    struct Ev { double beat; float velocity; int slot; float pitchOffset; float lengthBeats /* length scale: 1 = layer durations as designed */; uint32_t seed; };
    juce::AudioBuffer<float> renderSequence(const std::array<const CandyDNA*, kNumSoundSlots>& slots, const std::vector<Ev>& events,
                                            double lengthBeats, const RenderOptions& o, bool loopWrap, const std::function<bool()>& cancelled = {});
    static float automaticTailMs(const CandyDNA& dna);
    static float peakOf(const juce::AudioBuffer<float>& b);
private:
    void ensure(double sr, QualityMode q, int block);
    std::unique_ptr<CandyEngine> eng_; double sr_ = 0; QualityMode q_ = QualityMode::Standard; int block_ = 0;
};
} // namespace dali
