#include "AudioFeatures.h"
#include "Core/DspUtil.h"

namespace dali
{
const float kBandEdgesHz[kNumBands + 1] = { 0.0f, 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 12000.0f, 1.0e6f };
const char* bandName(int b)
{
    static const char* n[] = { "Sub", "Bass", "Low-Mid", "Mid", "Upper-Mid", "Presence", "Air", "Sparkle" };
    return n[std::clamp(b, 0, kNumBands - 1)];
}

SpectrumAnalyzer::SpectrumAnalyzer(int order) : order_(order), n_(1 << order)
{
    fft_ = std::make_unique<juce::dsp::FFT>(order);
    win_.resize((size_t) n_); buf_.assign((size_t) (2 * n_), 0.0f);
    for (int i = 0; i < n_; ++i) win_[(size_t) i] = 0.5f - 0.5f * std::cos(kTwoPi * (float) i / (float) n_);
}

void SpectrumAnalyzer::magnitude(const float* x, int count, std::vector<float>& mag)
{
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    const int c = std::min(count, n_);
    for (int i = 0; i < c; ++i) buf_[(size_t) i] = x[i] * win_[(size_t) i];
    fft_->performRealOnlyForwardTransform(buf_.data(), true);
    mag.resize((size_t) (n_ / 2 + 1));
    const float norm = 2.0f / ((float) n_ * 0.5f);
    for (int k = 0; k <= n_ / 2; ++k)
    {
        const float re = buf_[(size_t) (2 * k)], im = buf_[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt(re * re + im * im) * norm;
    }
}

void SpectrumAnalyzer::bandPowers(const std::vector<float>& mag, double sr, int fftSize, std::array<float, kNumBands>& out)
{
    out.fill(0.0f);
    const float binHz = (float) sr / (float) fftSize;
    for (size_t k = 1; k < mag.size(); ++k)
    {
        const float f = (float) k * binHz;
        int b = kNumBands - 1; for (int i = 0; i < kNumBands; ++i) if (f < kBandEdgesHz[i + 1]) { b = i; break; }
        out[(size_t) b] += mag[k] * mag[k];
    }
}

float SpectrumAnalyzer::centroid(const std::vector<float>& mag, double sr, int fftSize)
{
    double num = 0, den = 0; const double binHz = sr / fftSize;
    for (size_t k = 1; k < mag.size(); ++k) { const double p = (double) mag[k] * mag[k]; num += p * (double) k * binHz; den += p; }
    return den > 1.0e-18 ? (float) (num / den) : 0.0f;
}

float SpectrumAnalyzer::flatness(const std::vector<float>& mag, double sr, int fftSize)
{
    const double binHz = sr / fftSize; double lg = 0, ar = 0; int cnt = 0;
    for (size_t k = 1; k < mag.size(); ++k)
    {
        const double f = (double) k * binHz; if (f < 100.0 || f > 16000.0) continue;
        const double p = (double) mag[k] * mag[k] + 1.0e-14; lg += std::log(p); ar += p; ++cnt;
    }
    if (cnt == 0 || ar <= 0) return 0.0f;
    return (float) std::clamp(std::exp(lg / cnt) / (ar / cnt), 0.0, 1.0);
}

BufferFeatures analyzeBuffer(const float* L, const float* R, int n, double sr)
{
    BufferFeatures f; f.durationSec = (float) ((double) n / sr);
    if (n <= 0) return f;
    double e = 0, dc = 0, ll = 0, rr = 0, lr = 0; float pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const float l = L[i], r = R ? R[i] : L[i];
        if (! std::isfinite(l) || ! std::isfinite(r)) { f.finite = false; return f; }
        const float m = 0.5f * (l + r); e += (double) m * m; dc += m; pk = std::max(pk, std::max(std::abs(l), std::abs(r)));
        ll += (double) l * l; rr += (double) r * r; lr += (double) l * r;
    }
    f.rmsDb = toDb((float) std::sqrt(e / n)); f.peakDb = toDb(pk); f.crestDb = f.peakDb - f.rmsDb; f.dcOffset = (float) (dc / n);
    f.stereoCorr = (ll > 1.0e-12 && rr > 1.0e-12) ? (float) (lr / std::sqrt(ll * rr)) : 1.0f;
    // tail level: last 10% of the buffer
    { const int t0 = n - std::max(1, n / 10); double te = 0; for (int i = t0; i < n; ++i) { const float m = 0.5f * (L[i] + (R ? R[i] : L[i])); te += (double) m * m; }
      f.tailDb = toDb((float) std::sqrt(te / (n - t0))); }
    // active fraction + onsets from a 5 ms envelope
    {
        const int w = std::max(1, (int) (0.005 * sr)); std::vector<float> env; env.reserve((size_t) (n / w + 1));
        for (int i = 0; i < n; i += w) { float m = 0; for (int k = i; k < std::min(n, i + w); ++k) m = std::max(m, std::abs(0.5f * (L[k] + (R ? R[k] : L[k])))); env.push_back(m); }
        const float thr = pk * 0.003f; int act = 0; for (float v : env) if (v > thr) ++act;
        f.activeFraction = env.empty() ? 0.0f : (float) act / (float) env.size();
        float prev = 0; int refractory = 0;
        for (float v : env) { if (refractory > 0) --refractory; if (v > prev * 2.2f && v > pk * 0.1f && refractory == 0) { ++f.onsets; refractory = 6; } prev = prev * 0.7f + v * 0.3f; }
    }
    // spectral features: average of up to 8 Hann frames across the buffer
    SpectrumAnalyzer sa(11); std::vector<float> mag; std::array<float, kNumBands> acc {}, tmp {}; double cen = 0, flat = 0; int frames = 0;
    const int N = sa.size(); std::vector<float> mono((size_t) N);
    const int hop = std::max(N / 2, std::max(1, (n - N) / 8));
    for (int s = 0; s + 1 <= n && frames < 12; s += hop)
    {
        std::fill(mono.begin(), mono.end(), 0.0f);
        const int c = std::min(N, n - s); for (int i = 0; i < c; ++i) mono[(size_t) i] = 0.5f * (L[s + i] + (R ? R[s + i] : L[s + i]));
        sa.magnitude(mono.data(), c, mag); SpectrumAnalyzer::bandPowers(mag, sr, N, tmp);
        double tot = 0; for (float v : tmp) tot += v; if (tot < 1.0e-12) continue;
        for (int b = 0; b < kNumBands; ++b) acc[(size_t) b] += tmp[(size_t) b];
        cen += SpectrumAnalyzer::centroid(mag, sr, N); flat += SpectrumAnalyzer::flatness(mag, sr, N); ++frames;
        if (s + N >= n) break;
    }
    if (frames > 0)
    {
        double tot = 0; for (float v : acc) tot += v;
        for (int b = 0; b < kNumBands; ++b) f.bands[(size_t) b] = (float) (acc[(size_t) b] / std::max(tot, 1.0e-18));
        f.centroidHz = (float) (cen / frames); f.flatness = (float) (flat / frames);
        f.lowFrac = f.bands[0] + f.bands[1] + f.bands[2]; f.midFrac = f.bands[3] + f.bands[4]; f.highFrac = f.bands[5] + f.bands[6] + f.bands[7];
    }
    return f;
}
} // namespace dali
