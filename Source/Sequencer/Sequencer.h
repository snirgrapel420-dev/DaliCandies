#pragma once
// Sequencer: pattern planning (8 modes, message/worker thread) and sample-accurate host-synced playback
// (audio thread, allocation-free).  A pattern is a list of events in beats from the loop start; the
// pattern loops every `bars * beatsPerBar` beats and positions are derived from the host's PPQ, so
// transport jumps, loops and tempo changes stay in sync.
#include "Core/CandyDNA.h"
#include "Analysis/TrackAnalyzer.h"
#include "DSP/CandyEngine.h"
#include <memory>
#include <mutex>
#include <atomic>

namespace dali
{
struct SeqContext
{
    const TrackAnalysis* analysis = nullptr;     // optional: enables Groove Follower / Call&Response gap finding and negative space
    std::array<float, kNumBands> candyBands { 0.0f, 0.02f, 0.05f, 0.13f, 0.25f, 0.3f, 0.18f, 0.07f };
    int   numSlots = 1;                          // sound slots available for per-event sound selection
    float negSpaceSensitivity = 0.5f;            // 0 = never omit, 1 = omit readily
    float density = 0.38f;                       // generator density macro (0..1)
    float chaos = 0.4f;
    int   beatsPerBar = 4;
    double bpm = 120.0;
};

struct SeqPattern
{
    SeqConfig cfg;
    double lengthBeats = 16.0;
    std::vector<SeqEvent> events;                // sorted by position
};

// Plans a pattern.  Events of `cfg.events` that are frozen or locked are preserved (when cfg.freezeAll
// every existing event is preserved).  Deterministic for identical (cfg, ctx).
std::vector<SeqEvent> planEvents(const SeqConfig& cfg, const SeqContext& ctx);
// Phrase evolution: derive the next variation of a pattern (frozen / locked events are untouched).
std::vector<SeqEvent> evolveEvents(const SeqConfig& cfg, const SeqContext& ctx, uint32_t evolveSeed);
std::shared_ptr<SeqPattern> makePattern(const SeqConfig& cfg, const SeqContext& ctx, bool replan);

// Euclidean rhythm (Bjorklund-style distribution), exposed for tests
std::vector<bool> euclid(int steps, int hits, int rotate);

// Audio-thread player.  publish() from any non-audio thread; render() from the audio thread.
class SeqPlayer
{
public:
    void publish(std::shared_ptr<SeqPattern> p);
    // Emits the events that fall inside the block [host.ppq, host.ppq + n * bpm / 60 / sr) to the engine.
    // Returns the number of events queued.
    int render(const HostInfo& host, double sampleRate, int n, CandyEngine& engine, float basePitchNote = 60.0f);
    void resetCounters() { played_ = skipped_ = 0; }
    // Live negative space: probability (0..1) of leaving out non-frozen events because the incoming audio is busy.
    void setLiveOmit(float p) { liveOmit_ = p; }
    void resetPosition() { haveEnd_ = false; }                 // call on transport start / seek
    uint64_t played() const { return played_; }
    uint64_t skipped() const { return skipped_; }
    static bool eventFires(const SeqPattern& p, size_t index, int64_t loopIndex);
private:
    std::atomic<const SeqPattern*> cur_ { nullptr };
    std::vector<std::shared_ptr<SeqPattern>> graveyard_; std::mutex m_;
    uint64_t played_ = 0, skipped_ = 0;
    double lastEnd_ = 0.0; bool haveEnd_ = false; float liveOmit_ = 0.0f;
};
} // namespace dali
