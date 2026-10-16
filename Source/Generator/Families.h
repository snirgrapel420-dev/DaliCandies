#pragma once
// The six sound families of the Kitchen screen and the 15 fusions (every pair of families).
//
//   SNAP  - clicks, blips, chirps                (Micro Details archetypes)
//   CHIME - bells, metal ticks, springs          (Metallic Accents)
//   TWITCH- stutters, slices, bit shreds         (Rhythmic Glitches)
//   SWIRL - sweeps, spirals, ring/fold oddities  (Psychedelic Movement + Experimental)
//   MOSS  - breaths, insects, wet clusters       (Organic Textures)
//   HAZE  - washes, clouds, risers, tails        (Atmospheric Candy + Transitions)
#include "Core/CandyDNA.h"
#include <vector>

namespace dali
{
constexpr int kNumFamilies = 6;
const char* familyName(int f);                    // "SNAP" ...
const char* familyBlurb(int f);                   // one-line description
const char* fusionName(int a, int b);             // dish name of the pair (a == b -> the pure family name)
std::vector<int> archetypesOfFamily(int f);       // indices into archetypeTable()
int familyOfCategory(Category c);

// Number of distinct dishes: 6 pure + 15 fusions
constexpr int kNumDishes = kNumFamilies + kNumFamilies * (kNumFamilies - 1) / 2;
}
