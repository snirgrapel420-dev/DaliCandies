#pragma once
// Lightweight real-time analysis (audio thread safe): level, envelope follower, transient detector,
// 3-band energy, brightness estimate, onset density.  Results are published through relaxed atomics.
#include "Core/Types.h"
#include "Core/DspUtil.h"
#include <atomic>

namespace dali
{
class LiveAnalyzer
{
public:
    void prepare(double sampleRate);
    void reset();
    void process(const float* L, const float* R, int n);

    float rms() const        { return rms_.load(std::memory_order_relaxed); }
    float peak() const       { return peak_.load(std::memory_order_relaxed); }
    float follower() const   { return follower_.load(std::memory_order_relaxed); }     // 0..1 (unipolar)
    float transient() const  { return transient_.load(std::memory_order_relaxed); }    // 0..1
    float low() const        { return low_.load(std::memory_order_relaxed); }          // band energies, 0..1
    float mid() const        { return mid_.load(std::memory_order_relaxed); }
    float high() const       { return high_.load(std::memory_order_relaxed); }
    float brightness() const { return bright_.load(std::memory_order_relaxed); }       // 0..1 estimate
    float onsetDensity() const { return density_.load(std::memory_order_relaxed); }    // onsets per second
    uint32_t onsetCount() const { return onsets_.load(std::memory_order_relaxed); }
    float stereoCorrelation() const { return corr_.load(std::memory_order_relaxed); }
    // history of block levels for GUI display (single writer, benign races)
    static constexpr int kHistory = 256;
    float history(int i) const { return hist_[(size_t) (i & (kHistory - 1))].load(std::memory_order_relaxed); }
    int historyPos() const { return histPos_.load(std::memory_order_relaxed); }

private:
    double sr_ = 44100.0;
    float envFast_ = 0, envSlow_ = 0, fol_ = 0, rmsAcc_ = 0, lowE_ = 0, midE_ = 0, highE_ = 0, zcr_ = 0, lastSample_ = 0, ll_ = 0, rr_ = 0, lr_ = 0;
    Svf lp_, hp_;
    int refractory_ = 0; float densityAcc_ = 0; int densityCount_ = 0;
    std::atomic<float> rms_ { 0 }, peak_ { 0 }, follower_ { 0 }, transient_ { 0 }, low_ { 0 }, mid_ { 0 }, high_ { 0 }, bright_ { 0 }, density_ { 0 }, corr_ { 1 };
    std::atomic<uint32_t> onsets_ { 0 };
    std::array<std::atomic<float>, kHistory> hist_ {}; std::atomic<int> histPos_ { 0 };
};
} // namespace dali
