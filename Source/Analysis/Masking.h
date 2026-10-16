#pragma once
// Psychoacoustic awareness: spectral occupancy comparison between a candy and the track, with
// conservative adaptive actions.  This is an *estimate* - it never guarantees the absence of masking.
#include "AudioFeatures.h"

namespace dali
{
struct MaskingReport
{
    std::array<float, kNumBands> candyMaskedRisk {};     // 0..1: candy likely inaudible in this band
    std::array<float, kNumBands> trackMaskedRisk {};     // 0..1: candy likely to compete with the track in this band
    float overall = 0.0f;                                // combined 0..1
    int   worstBand = 0;
    float stereoCompat = 1.0f;                           // 1 = mono-compatible
};

struct AdaptiveParams
{
    float lowCutHz = 30.0f;        // low-frequency cleanup
    bool  eqActive = false;        // conservative dynamic EQ band
    float eqFreqHz = 1000.0f, eqGainDb = 0.0f, eqQ = 1.2f;
    float widthScale = 1.0f;       // reduce width if the candy is not mono compatible
    float levelTrimDb = 0.0f;      // output level management (never above 0)
};

// candyBands: fractions (sum 1), candyRmsDb: level of the candy as it will be heard.
// trackBandDb: average per-band level of the track (dB, same reference as candyRmsDb), trackRmsDb: overall.
MaskingReport estimateMasking(const std::array<float, kNumBands>& candyBands, float candyRmsDb, float candyStereoCorr,
                              const std::array<float, kNumBands>& trackBandDb, float trackRmsDb);

// strength 0..1 scales how much processing is applied (0 = none).
AdaptiveParams adaptiveFor(const MaskingReport& r, const std::array<float, kNumBands>& candyBands, float strength);

float bandCentreHz(int b);
} // namespace dali
