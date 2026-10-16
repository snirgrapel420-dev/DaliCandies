#include "Families.h"
#include "Archetypes.h"

namespace dali
{
static const char* kNames[kNumFamilies]  = { "SNAP", "CHIME", "TWITCH", "SWIRL", "MOSS", "HAZE" };
static const char* kBlurbs[kNumFamilies] = { "clicks, blips and chirps", "bells, metal ticks and springs", "stutters, slices and bit shreds",
                                             "sweeps, spirals and ring-mod oddities", "breaths, insects and wet clusters", "washes, clouds and risers" };

const char* familyName(int f)  { return kNames[std::clamp(f, 0, kNumFamilies - 1)]; }
const char* familyBlurb(int f) { return kBlurbs[std::clamp(f, 0, kNumFamilies - 1)]; }

const char* fusionName(int a, int b)
{
    a = std::clamp(a, 0, kNumFamilies - 1); b = std::clamp(b, 0, kNumFamilies - 1);
    if (a == b) return kNames[a];
    if (a > b) std::swap(a, b);
    static const char* n[kNumFamilies][kNumFamilies] = {
        /* SNAP   */ { "", "Tin Tick",        "Pop Rocks",       "Fizz Spiral",   "Cricket Pop", "Mist Pop"    },
        /* CHIME  */ { "", "",                "Shattered Glass", "Bell Vortex",   "Wind Chimes", "Frozen Bells" },
        /* TWITCH */ { "", "",                "",                "Glitch Tornado","Bug Static",  "Static Fog"   },
        /* SWIRL  */ { "", "",                "",                "",              "Tide Pool",   "Nebula Candy" },
        /* MOSS   */ { "", "",                "",                "",              "",            "Swamp Mist"   },
        /* HAZE   */ { "", "",                "",                "",              "",            ""             } };
    return n[a][b];
}

int familyOfCategory(Category c)
{
    switch (c)
    {
        case Category::MicroDetails:        return 0;
        case Category::MetallicAccents:     return 1;
        case Category::RhythmicGlitches:    return 2;
        case Category::PsychedelicMovement:
        case Category::Experimental:        return 3;
        case Category::OrganicTextures:     return 4;
        case Category::AtmosphericCandy:
        case Category::Transitions:         return 5;
        default:                            return 0;
    }
}

std::vector<int> archetypesOfFamily(int f)
{
    std::vector<int> v; const auto& t = archetypeTable();
    for (int i = 0; i < (int) t.size(); ++i) if (familyOfCategory(t[(size_t) i].category) == f) v.push_back(i);
    return v;
}
}
