#include "NegativeSpace.h"

namespace dali::NegativeSpace
{
float clutter(const NegSpaceContext& c)
{
    const float v = 0.22f * c.eventDensity + 0.18f * c.overlap + 0.14f * c.transient
                  + 0.20f * c.maskingRisk + 0.14f * c.competition + 0.12f * c.phraseCrowding;
    return clamp01(v * 1.25f);
}

float omitProbability(float clutterScore, float sensitivity, float density)
{
    // threshold in clutter at which omission starts; more sensitivity -> lower threshold.
    const float thr = lerp(0.85f, 0.30f, clamp01(sensitivity)) + 0.12f * clamp01(density);
    const float width = 0.25f;
    const float t = clamp01((clutterScore - thr) / width + 0.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    // even a very empty position is occasionally left empty (deliberate omission) when sensitivity is high
    const float floorP = 0.06f * clamp01(sensitivity) * (1.0f - clamp01(density) * 0.5f);
    return std::max(smooth, floorP);
}

bool shouldOmit(const NegSpaceContext& c, float sensitivity, float density, Rng& rng)
{
    return rng.uniform() < omitProbability(clutter(c), sensitivity, density);
}
}
