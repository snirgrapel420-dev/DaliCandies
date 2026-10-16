#pragma once
// Offline / worker-thread audio feature extraction shared by the generator (candidate scoring)
// and the track analysis engine.  Never call from the audio thread.
#include "Core/Types.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <memory>

namespace dali
{
constexpr int kNumBands = 8;
extern const float kBandEdgesHz[kNumBands + 1];     // 0-60-150-400-1k-2.5k-6k-12k-Nyquist
const char* bandName(int b);

struct BufferFeatures
{
    bool   finite = true;
    float  rmsDb = -120.0f, peakDb = -120.0f, crestDb = 0.0f, tailDb = -120.0f;
    float  centroidHz = 0.0f, flatness = 0.0f;
    float  lowFrac = 0.0f, midFrac = 0.0f, highFrac = 0.0f;
    float  stereoCorr = 1.0f, dcOffset = 0.0f, durationSec = 0.0f;
    int    onsets = 0;
    float  activeFraction = 0.0f;     // fraction of time above -50 dB re peak
    std::array<float, kNumBands> bands {};   // fractions of total power, sum = 1
};

class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer(int order = 11);
    int size() const { return n_; }
    // Hann window, zero padding; fills mag[0..n/2] (linear magnitude, normalised by window sum)
    void magnitude(const float* x, int count, std::vector<float>& mag);
    static void bandPowers(const std::vector<float>& mag, double sr, int fftSize, std::array<float, kNumBands>& out);   // raw power per band
    static float centroid(const std::vector<float>& mag, double sr, int fftSize);
    static float flatness(const std::vector<float>& mag, double sr, int fftSize);
private:
    int order_, n_;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::vector<float> win_, buf_;
};

BufferFeatures analyzeBuffer(const float* L, const float* R, int n, double sr);

inline float toDb(float lin) { return 20.0f * std::log10(std::max(lin, 1.0e-6f)); }
inline float powToDb(float p) { return 10.0f * std::log10(std::max(p, 1.0e-12f)); }

} // namespace dali
