#pragma once
// Negative Space engine: decides when extra ear candy would only add clutter.
#include "Core/Types.h"
#include "Core/Rng.h"
#include <array>

namespace dali
{
struct NegSpaceContext
{
    float eventDensity   = 0.0f;   // 0..1  planned + detected events nearby (per beat, normalised)
    float overlap        = 0.0f;   // 0..1  temporal overlap with existing transients / loud material
    float transient      = 0.0f;   // 0..1  transient activity at the candidate position
    float maskingRisk    = 0.0f;   // 0..1  spectral masking risk of the candidate against the track
    float competition    = 0.0f;   // 0..1  frequency competition in the candy's dominant bands
    float phraseCrowding = 0.0f;   // 0..1  phrase structure (strong downbeats / section starts are protected)
};

namespace NegativeSpace
{
// 0 = plenty of room, 1 = cluttered.  Weighted, monotonic in every input.
float clutter(const NegSpaceContext& c);
// Deterministic decision.  sensitivity 0..1 (higher = omits more readily), density 0..1 from the
// generator controls (raises tolerance only slightly: more density never means "always place").
float omitProbability(float clutterScore, float sensitivity, float density);
bool  shouldOmit(const NegSpaceContext& c, float sensitivity, float density, Rng& rng);
}
} // namespace dali
