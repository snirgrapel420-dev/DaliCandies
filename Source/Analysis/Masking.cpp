#include "Masking.h"
#include <cmath>

namespace dali
{
float bandCentreHz(int b)
{
    static const float c[kNumBands] = { 40.0f, 95.0f, 250.0f, 640.0f, 1600.0f, 3900.0f, 8500.0f, 14000.0f };
    return c[std::clamp(b, 0, kNumBands - 1)];
}

MaskingReport estimateMasking(const std::array<float, kNumBands>& candyBands, float candyRmsDb, float candyStereoCorr,
                              const std::array<float, kNumBands>& trackBandDb, float trackRmsDb)
{
    MaskingReport r;
    float worst = -1.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float candyDb = candyRmsDb + powToDb(std::max(candyBands[(size_t) b], 1.0e-6f));
        // simple spreading: neighbouring bands of the track mask upwards more than downwards
        const float lower = b > 0 ? trackBandDb[(size_t) (b - 1)] - 8.0f : -200.0f, upper = b + 1 < kNumBands ? trackBandDb[(size_t) (b + 1)] - 14.0f : -200.0f;
        const float masker = std::max(trackBandDb[(size_t) b], std::max(lower, upper));
        // candy is masked when it sits well below the masker (~10 dB), fully masked at ~25 dB below
        r.candyMaskedRisk[(size_t) b] = clamp01((masker - candyDb - 10.0f) / 15.0f) * clamp01(candyBands[(size_t) b] * 8.0f);
        // candy competes when it is close to / above the track's level in a band the track relies on
        const float relative = candyDb - trackBandDb[(size_t) b];
        const float trackWeight = clamp01((trackBandDb[(size_t) b] - (trackRmsDb - 24.0f)) / 18.0f);
        r.trackMaskedRisk[(size_t) b] = clamp01((relative + 6.0f) / 12.0f) * trackWeight * clamp01(candyBands[(size_t) b] * 6.0f);
        const float comb = std::max(r.candyMaskedRisk[(size_t) b], r.trackMaskedRisk[(size_t) b]);
        if (comb > worst) { worst = comb; r.worstBand = b; }
    }
    float sum = 0.0f, wsum = 0.0f;
    for (int b = 0; b < kNumBands; ++b) { sum += std::max(r.candyMaskedRisk[(size_t) b], r.trackMaskedRisk[(size_t) b]) * candyBands[(size_t) b]; wsum += candyBands[(size_t) b]; }
    r.overall = wsum > 1.0e-9f ? clamp01(sum / wsum) : 0.0f;
    r.stereoCompat = clamp01(0.5f + 0.5f * candyStereoCorr);
    return r;
}

AdaptiveParams adaptiveFor(const MaskingReport& r, const std::array<float, kNumBands>& candyBands, float strength)
{
    AdaptiveParams a; strength = clamp01(strength);
    if (strength <= 0.0f) return a;
    // low-frequency cleanup: raise the high-pass when the candy has low content that competes with the track
    const float lowCompete = std::max(r.trackMaskedRisk[0], std::max(r.trackMaskedRisk[1], r.trackMaskedRisk[2] * 0.7f));
    a.lowCutHz = lerp(30.0f, 220.0f, clamp01(lowCompete * 1.3f) * strength);
    if (candyBands[0] + candyBands[1] > 0.25f) a.lowCutHz = std::max(a.lowCutHz, lerp(30.0f, 90.0f, strength));
    // conservative dynamic EQ: at most -3 dB in the single most competing band
    int worst = 0; float wv = 0.0f;
    for (int b = 2; b < kNumBands; ++b) if (r.trackMaskedRisk[(size_t) b] > wv) { wv = r.trackMaskedRisk[(size_t) b]; worst = b; }
    if (wv > 0.35f) { a.eqActive = true; a.eqFreqHz = bandCentreHz(worst); a.eqGainDb = -3.0f * clamp01((wv - 0.35f) / 0.5f) * strength; a.eqQ = 1.0f; }
    // stereo compatibility
    a.widthScale = lerp(1.0f, std::max(0.5f, r.stereoCompat), strength);
    // level management: trim a little when the whole candy sits on top of the track
    a.levelTrimDb = -2.5f * clamp01((r.overall - 0.5f) / 0.5f) * strength;
    return a;
}
} // namespace dali
