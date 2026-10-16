#pragma once
// Common interface for the six synthesis engines.
#include "Core/Types.h"
#include "Core/CandyDNA.h"
#include "Core/DspUtil.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>

namespace dali
{
//------------------------------------------------------------------------------
// Immutable mono audio source used by Granular / Glitch / Spectral engines
// (captured track audio or an imported WAV).  Read from the audio thread through
// SourceHolder, which never frees a buffer that may still be in use.
//------------------------------------------------------------------------------
struct SourceBuffer
{
    std::vector<float> data;
    double sampleRate = 44100.0;
    int size() const { return (int) data.size(); }
};

class SourceHolder
{
public:
    // message thread
    void publish(std::shared_ptr<const SourceBuffer> b)
    {
        std::lock_guard<std::mutex> lk(m);
        graveyard.push_back(b);                      // keep alive: audio thread may hold the raw pointer
        if (graveyard.size() > 6) graveyard.erase(graveyard.begin());
        current.store(b.get(), std::memory_order_release);
    }
    void clear() { std::lock_guard<std::mutex> lk(m); current.store(nullptr, std::memory_order_release); }
    // audio thread (lock free)
    const SourceBuffer* get() const noexcept { return current.load(std::memory_order_acquire); }
private:
    std::atomic<const SourceBuffer*> current { nullptr };
    std::mutex m;
    std::vector<std::shared_ptr<const SourceBuffer>> graveyard;
};

struct ControlFrame
{
    std::array<float, kParamsPerLayer> p {};   // modulated normalised engine parameters
    float hz = 440.0f;                         // modulated pitch in Hz
    float velocity = 1.0f;
    float progress = 0.0f;                     // 0..1 position within the event's nominal duration
    float timeSec = 0.0f;                      // time since trigger
    float bpm = 120.0f;
};

class IEngine
{
public:
    virtual ~IEngine() = default;
    virtual void prepare(double sampleRate, QualityMode q) = 0;
    virtual void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer* src) = 0;
    // Writes (overwrites) n samples of stereo output.  No allocation, no locks.
    virtual void process(const ControlFrame& cf, float* outL, float* outR, int n) = 0;
    virtual EngineType type() const = 0;
};

std::unique_ptr<IEngine> createEngine(EngineType t);

// Source material preparation (runs on non-audio threads when a sound is loaded).
// Returns the internally synthesised source for Granular / Glitch layers (nullptr for others).
std::shared_ptr<SourceBuffer> makeInternalSource(const LayerConfig& cfg, double sampleRate);
// True when this layer's "Source" parameter asks for imported / captured audio.
bool wantsExternalSource(const LayerConfig& cfg);

// Parameter mapping helpers shared by engines
inline float expMap(float v, float lo, float hi) { return lo * std::pow(hi / lo, clamp01(v)); }
inline float linMap(float v, float lo, float hi) { return lo + (hi - lo) * clamp01(v); }

// Cheap 2x oversampler for nonlinear per-sample stages (4th-order Butterworth anti-image / anti-alias).
struct Os2
{
    Biquad up1, up2, dn1, dn2;
    void prepare(double baseSr)
    {
        const double sr2 = baseSr * 2.0;
        const float fc = (float) std::min(0.45 * baseSr, 20000.0);
        up1.setLowpass(sr2, fc, 0.5412f); up2.setLowpass(sr2, fc, 1.3066f);
        dn1.setLowpass(sr2, fc, 0.5412f); dn2.setLowpass(sr2, fc, 1.3066f);
    }
    void reset() { up1.reset(); up2.reset(); dn1.reset(); dn2.reset(); }
    template <typename F> inline float process(float x, F&& fn) noexcept
    {
        const float a = fn(up2.process(up1.process(2.0f * x)));
        const float b = fn(up2.process(up1.process(0.0f)));
        dn2.process(dn1.process(a));
        return dn2.process(dn1.process(b));
    }
};

} // namespace dali
