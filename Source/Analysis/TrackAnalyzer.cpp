#include "TrackAnalyzer.h"
#include "Masking.h"
#include "Core/Rng.h"
#include <numeric>

namespace dali
{
namespace
{
float percentile(std::vector<float> v, float p)
{
    if (v.empty()) return 1.0f;
    const size_t k = std::min(v.size() - 1, (size_t) (p * (float) (v.size() - 1)));
    std::nth_element(v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}
void normalise(std::vector<float>& v, float p = 0.95f)
{
    const float m = std::max(1.0e-9f, percentile(v, p));
    for (auto& x : v) x = clamp01(x / m);
}
bool cancelled(const std::atomic<bool>* c) { return c != nullptr && c->load(std::memory_order_relaxed); }
}

TrackAnalysis analyzeTrack(const float* mono, int n, double sr, double bpm, int beatsPerBar,
                           const std::atomic<bool>* cancel, const std::function<void(float)>& progress)
{
    TrackAnalysis a; a.sampleRate = sr; a.bpm = bpm > 20.0 ? bpm : 120.0; a.beatsPerBar = std::max(1, beatsPerBar);
    a.stepsPerBar = a.beatsPerBar * 4;
    const double stepSamples = sr * 60.0 / a.bpm / 4.0;
    a.numSteps = (int) ((double) n / stepSamples);
    if (n < 2048 || a.numSteps < a.stepsPerBar) return a;
    a.bars = a.numSteps / a.stepsPerBar; a.numSteps = a.bars * a.stepsPerBar;
    const int S = a.numSteps;
    for (auto* v : { &a.rms, &a.peak, &a.centroid, &a.flux, &a.onset, &a.transient, &a.activity, &a.phrasePos, &a.opportunity }) v->assign((size_t) S, 0.0f);
    for (auto& b : a.band) b.assign((size_t) S, 0.0f);

    // ---- global level + waveform overview
    { double e = 0; float pk = 0; for (int i = 0; i < n; ++i) { e += (double) mono[i] * mono[i]; pk = std::max(pk, std::abs(mono[i])); }
      a.rmsDb = toDb((float) std::sqrt(e / n)); a.peakDb = toDb(pk); }
    a.waveform.assign((size_t) (2 * kWaveformPoints), 0.0f);
    for (int i = 0; i < kWaveformPoints; ++i)
    {
        const int s0 = (int) ((int64_t) n * i / kWaveformPoints), s1 = std::max(s0 + 1, (int) ((int64_t) n * (i + 1) / kWaveformPoints));
        float lo = 0, hi = 0; for (int k = s0; k < s1 && k < n; ++k) { lo = std::min(lo, mono[k]); hi = std::max(hi, mono[k]); }
        a.waveform[(size_t) (2 * i)] = lo; a.waveform[(size_t) (2 * i + 1)] = hi;
    }

    // ---- per-step features
    SpectrumAnalyzer sa(11); const int N = sa.size();
    std::vector<float> mag, prevMag; std::array<float, kNumBands> bp {};
    std::array<double, kNumBands> bandAvg {}; std::vector<double> spectrumAcc; spectrumAcc.assign((size_t) kSpectrumBins, 0.0); int specFrames = 0;
    double centSum = 0;
    for (int s = 0; s < S; ++s)
    {
        if ((s & 15) == 0) { if (cancelled(cancel)) return TrackAnalysis(); if (progress) progress(0.5f * (float) s / (float) S); }
        const int i0 = (int) (s * stepSamples), i1 = std::min(n, (int) ((s + 1) * stepSamples));
        double e = 0; float pk = 0; for (int i = i0; i < i1; ++i) { e += (double) mono[i] * mono[i]; pk = std::max(pk, std::abs(mono[i])); }
        const float r = (float) std::sqrt(e / std::max(1, i1 - i0));
        a.rms[(size_t) s] = r; a.peak[(size_t) s] = pk; a.transient[(size_t) s] = r > 1.0e-5f ? clamp01((pk / r - 1.5f) / 5.0f) : 0.0f;
        const int c = std::clamp((i0 + i1) / 2 - N / 2, 0, std::max(0, n - N));
        sa.magnitude(mono + c, std::min(N, n - c), mag);
        SpectrumAnalyzer::bandPowers(mag, sr, N, bp);
        for (int b = 0; b < kNumBands; ++b) { a.band[(size_t) b][(size_t) s] = bp[(size_t) b]; bandAvg[(size_t) b] += bp[(size_t) b]; }
        a.centroid[(size_t) s] = SpectrumAnalyzer::centroid(mag, sr, N); centSum += a.centroid[(size_t) s];
        if (! prevMag.empty()) { float fl = 0; for (size_t k = 1; k < mag.size(); ++k) fl += std::max(0.0f, mag[k] - prevMag[k]); a.flux[(size_t) s] = fl; }
        prevMag = mag;
        // log-spaced spectrum accumulation
        const double binHz = sr / N;
        for (size_t k = 1; k < mag.size(); ++k)
        {
            const double f = (double) k * binHz; if (f < 20.0 || f > 20000.0) continue;
            const int bi = std::clamp((int) (std::log(f / 20.0) / std::log(1000.0) * kSpectrumBins), 0, kSpectrumBins - 1);
            spectrumAcc[(size_t) bi] = std::max(spectrumAcc[(size_t) bi], (double) mag[k]);
        }
        ++specFrames;
    }
    a.spectrum.assign((size_t) kSpectrumBins, -100.0f);
    for (int i = 0; i < kSpectrumBins; ++i) a.spectrum[(size_t) i] = toDb((float) spectrumAcc[(size_t) i]);
    a.centroidHz = (float) (centSum / S);
    for (int b = 0; b < kNumBands; ++b) a.bandDb[(size_t) b] = powToDb((float) (bandAvg[(size_t) b] / S)) * 1.0f + 0.0f;

    // ---- fine-grained onset detection (spectral flux, hop 512) mapped onto steps
    {
        const int hop = 512; const int frames = (n - 1024) / hop; SpectrumAnalyzer fa(10); std::vector<float> m0, m1;
        std::vector<float> flux((size_t) std::max(0, frames), 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            if ((f & 255) == 0) { if (cancelled(cancel)) return TrackAnalysis(); if (progress) progress(0.5f + 0.35f * (float) f / (float) std::max(1, frames)); }
            fa.magnitude(mono + f * hop, 1024, m1);
            if (! m0.empty()) { float fl = 0; for (size_t k = 1; k < m1.size() && k < 200; ++k) fl += std::max(0.0f, std::log1p(40.0f * m1[k]) - std::log1p(40.0f * m0[k])); flux[(size_t) f] = fl; }
            m0 = m1;
        }
        for (int f = 0; f < frames; ++f)
        {
            const double t = (double) (f * hop + 512) / stepSamples; const int s = (int) t; if (s < 0 || s >= S) continue;
            a.onset[(size_t) s] = std::max(a.onset[(size_t) s], flux[(size_t) f]);
        }
    }
    normalise(a.onset, 0.97f); normalise(a.flux, 0.95f);
    { std::vector<float> rn = a.rms; normalise(rn, 0.95f); for (int s = 0; s < S; ++s) a.activity[(size_t) s] = clamp01(0.6f * rn[(size_t) s] + 0.4f * a.onset[(size_t) s]); a.rms = rn; }
    for (auto& b : a.band) normalise(b, 0.97f);
    { int on = 0; float sum = 0; for (float v : a.onset) { if (v > 0.35f) ++on; sum += v; } a.dynamicDensity = (float) on / (float) S; a.rhythmicActivity = sum / (float) S; }

    // ---- repetition + sections (bar feature vectors)
    const int B = a.bars; const int D = a.stepsPerBar * (kNumBands + 1);
    std::vector<std::vector<float>> bf((size_t) B, std::vector<float>((size_t) D, 0.0f));
    for (int b = 0; b < B; ++b)
        for (int k = 0; k < a.stepsPerBar; ++k)
        {
            const int s = b * a.stepsPerBar + k; auto& v = bf[(size_t) b];
            for (int bd = 0; bd < kNumBands; ++bd) v[(size_t) (k * (kNumBands + 1) + bd)] = a.band[(size_t) bd][(size_t) s];
            v[(size_t) (k * (kNumBands + 1) + kNumBands)] = a.onset[(size_t) s];
        }
    auto cosine = [&](int x, int y) {
        double dot = 0, nx = 0, ny = 0; for (int i = 0; i < D; ++i) { dot += (double) bf[(size_t) x][(size_t) i] * bf[(size_t) y][(size_t) i]; nx += (double) bf[(size_t) x][(size_t) i] * bf[(size_t) x][(size_t) i]; ny += (double) bf[(size_t) y][(size_t) i] * bf[(size_t) y][(size_t) i]; }
        return (nx > 1.0e-9 && ny > 1.0e-9) ? (float) (dot / std::sqrt(nx * ny)) : 0.0f; };
    a.barRepetition.assign((size_t) B, 0.0f);
    for (int b = 1; b < B; ++b) { float best = 0; for (int lag : { 1, 2, 4, 8 }) if (b - lag >= 0) best = std::max(best, cosine(b, b - lag)); a.barRepetition[(size_t) b] = best; }
    std::vector<float> novelty((size_t) B, 0.0f);
    for (int b = 2; b + 2 <= B; ++b)
    {
        double d = 0; for (int i = 0; i < D; ++i) { const float p = 0.5f * (bf[(size_t) b - 1][(size_t) i] + bf[(size_t) b - 2][(size_t) i]); const float q = 0.5f * (bf[(size_t) b][(size_t) i] + bf[(size_t) std::min(B - 1, b + 1)][(size_t) i]); d += std::abs(p - q); }
        novelty[(size_t) b] = (float) d;
    }
    { double mean = 0, var = 0; int cnt = 0; for (int b = 2; b + 2 <= B; ++b) { mean += novelty[(size_t) b]; ++cnt; } mean = cnt ? mean / cnt : 0;
      for (int b = 2; b + 2 <= B; ++b) { var += (novelty[(size_t) b] - mean) * (novelty[(size_t) b] - mean); }
      const double sd = cnt ? std::sqrt(var / cnt) : 0;
      int last = -100; a.sectionBars.push_back(0);
      for (int b = 2; b + 2 <= B; ++b) if (novelty[(size_t) b] > mean + 0.8 * sd && b - last >= 4 && novelty[(size_t) b] >= novelty[(size_t) b - 1] && novelty[(size_t) b] >= novelty[(size_t) std::min(B - 1, b + 1)]) { a.sectionBars.push_back(b); last = b; } }

    // ---- phrase position (8-bar phrases anchored at section starts)
    for (int s = 0; s < S; ++s)
    {
        const int bar = s / a.stepsPerBar; int start = 0; for (int sb : a.sectionBars) if (sb <= bar) start = sb;
        const float within = (float) ((bar - start) % 8) + (float) (s % a.stepsPerBar) / (float) a.stepsPerBar;
        a.phrasePos[(size_t) s] = within / 8.0f;
    }
    computeOpportunity(a);
    a.valid = true; a.revision = 1;
    if (progress) progress(1.0f);
    return a;
}

void computeOpportunity(TrackAnalysis& a)
{
    const int S = a.numSteps; if (S <= 0) return;
    // average the track's strongest upper-band occupancy to normalise "spectral availability"
    for (int s = 0; s < S; ++s)
    {
        const int k = s % a.stepsPerBar;
        // rhythmic suitability: off-beat positions and bar-end lead-ins are favoured; kick positions avoided
        float posW = (k % 4 == 0) ? 0.35f : (k % 2 == 0 ? 0.9f : 0.72f);
        if (k >= a.stepsPerBar - 2) posW = std::min(1.0f, posW + 0.2f);
        const float rhythm = clamp01(0.5f * posW + 0.5f * (1.0f - a.onset[(size_t) s]));
        // spectral availability in the bands where ear candy lives (mid..sparkle)
        const float occ = 0.12f * a.band[3][(size_t) s] + 0.2f * a.band[4][(size_t) s] + 0.3f * a.band[5][(size_t) s] + 0.25f * a.band[6][(size_t) s] + 0.13f * a.band[7][(size_t) s];
        const float spectral = clamp01(1.0f - std::pow(occ, 0.8f));
        // dynamic space: quiet gaps between hits, but total silence is treated as neutral (breakdown)
        float dyn = clamp01(1.0f - 0.55f * a.rms[(size_t) s] - 0.45f * a.transient[(size_t) s]);
        if (a.rms[(size_t) s] < 0.03f) dyn = 0.6f;
        // phrase position: lead-ins before phrase ends / section changes
        const float pp = a.phrasePos[(size_t) s]; const float toEnd = 1.0f - pp;
        float phrase = 0.4f + 0.6f * clamp01(1.0f - toEnd / 0.125f);
        const int bar = s / a.stepsPerBar;
        for (int sb : a.sectionBars) if (sb > 0 && bar == sb - 1 && k >= a.stepsPerBar - 4) phrase = 1.0f;
        if (k == 0 && (bar % 4) == 0) phrase *= 0.6f;                     // protect strong downbeats of phrases
        const float nov = 0.2f + 0.8f * a.barRepetition[(size_t) bar];     // repetitive bars welcome variation
        a.opportunity[(size_t) s] = clamp01(0.28f * rhythm + 0.22f * spectral + 0.18f * dyn + 0.17f * phrase + 0.15f * nov);
    }
}

NegSpaceContext contextAt(const TrackAnalysis& a, int step, const std::array<float, kNumBands>& candyBands, const std::vector<int>& planned)
{
    NegSpaceContext c; if (! a.valid || step < 0 || step >= a.numSteps) return c;
    int near = 0; for (int p : planned) if (std::abs(p - step) <= 4 && p != step) ++near;
    c.eventDensity = clamp01((float) near / 4.0f + 0.5f * a.dynamicDensity);
    c.overlap = clamp01(0.6f * a.activity[(size_t) step] + 0.4f * a.rms[(size_t) step]);
    c.transient = a.onset[(size_t) step] * 0.7f + a.transient[(size_t) step] * 0.3f;
    float mr = 0, comp = 0; for (int b = 0; b < kNumBands; ++b) { const float tb = a.band[(size_t) b][(size_t) step]; mr += candyBands[(size_t) b] * tb; comp += candyBands[(size_t) b] * tb * tb; }
    c.maskingRisk = clamp01(mr); c.competition = clamp01(comp * 1.5f);
    const int k = step % a.stepsPerBar; const int bar = step / a.stepsPerBar;
    c.phraseCrowding = (k == 0 ? 0.6f : 0.0f) + ((bar % 4 == 0 && k < 4) ? 0.3f : 0.0f);
    c.phraseCrowding = clamp01(c.phraseCrowding);
    return c;
}

void generateSuggestions(TrackAnalysis& a, float density, float negSpaceSensitivity, uint32_t seed)
{
    if (! a.valid) return;
    density = clamp01(density);
    std::vector<Suggestion> keep; std::vector<char> blocked((size_t) a.numSteps, 0);
    for (auto& s : a.suggestions) if (s.accepted || s.locked) { keep.push_back(s); }
    std::vector<int> rejected; for (auto& s : a.suggestions) if (s.rejected) rejected.push_back(s.step);
    for (auto& s : keep) if (s.step >= 0 && s.step < a.numSteps) blocked[(size_t) s.step] = 1;
    for (int r : rejected) if (r >= 0 && r < a.numSteps) blocked[(size_t) r] = 1;

    const int target = std::max(1, (int) std::round((float) a.bars * lerp(0.5f, 6.0f, density))) - (int) keep.size();
    const int spacing = (int) std::round(lerp(8.0f, 2.0f, density));
    Rng rng(seed ^ 0x51A9u);
    std::vector<int> order((size_t) a.numSteps); std::iota(order.begin(), order.end(), 0);
    std::vector<float> jitter((size_t) a.numSteps); for (auto& j : jitter) j = rng.uniform() * 0.06f;
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) { return a.opportunity[(size_t) x] + jitter[(size_t) x] > a.opportunity[(size_t) y] + jitter[(size_t) y]; });
    std::vector<int> planned; for (auto& s : keep) planned.push_back(s.step);
    std::array<float, kNumBands> candyBands { 0.0f, 0.02f, 0.05f, 0.13f, 0.25f, 0.3f, 0.18f, 0.07f };      // typical candy occupancy used for planning
    int placed = 0;
    for (int s : order)
    {
        if (placed >= target) break;
        if (blocked[(size_t) s] != 0) continue;
        bool tooClose = false; for (int p : planned) if (std::abs(p - s) < spacing) { tooClose = true; break; }
        if (tooClose) continue;
        auto ctx = contextAt(a, s, candyBands, planned);
        Rng r2 = rng.derive((uint32_t) s);
        if (NegativeSpace::shouldOmit(ctx, negSpaceSensitivity, density, r2)) continue;       // deliberate omission
        Suggestion g; g.step = s; g.score = a.opportunity[(size_t) s]; planned.push_back(s); keep.push_back(g); ++placed;
    }
    std::sort(keep.begin(), keep.end(), [](const Suggestion& x, const Suggestion& y) { return x.step < y.step; });
    for (auto& g : keep)
    {   // fill in the per-criterion explanations
        const int k = g.step % a.stepsPerBar;
        g.rhythm = clamp01(0.5f * ((k % 4 == 0) ? 0.35f : (k % 2 == 0 ? 0.9f : 0.72f)) + 0.5f * (1.0f - a.onset[(size_t) g.step]));
        const float occ = 0.12f * a.band[3][(size_t) g.step] + 0.2f * a.band[4][(size_t) g.step] + 0.3f * a.band[5][(size_t) g.step] + 0.25f * a.band[6][(size_t) g.step] + 0.13f * a.band[7][(size_t) g.step];
        g.spectral = clamp01(1.0f - std::pow(occ, 0.8f)); g.dynamic = clamp01(1.0f - 0.55f * a.rms[(size_t) g.step] - 0.45f * a.transient[(size_t) g.step]);
        g.phrase = clamp01(a.phrasePos[(size_t) g.step]); g.novelty = a.barRepetition[(size_t) (g.step / a.stepsPerBar)];
        g.score = a.opportunity[(size_t) g.step];
    }
    a.suggestions = std::move(keep); ++a.revision;
}
} // namespace dali
