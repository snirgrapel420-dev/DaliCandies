#pragma once
// Sound archetypes: each builds a *correlated* set of engine / envelope / modulation / effect settings that
// describes one musically meaningful kind of ear candy, steered by the user's macro controls (Intent).
#include "Core/CandyDNA.h"
#include "Core/Rng.h"
#include "Scale.h"
#include <functional>
#include <vector>

namespace dali
{
struct Intent
{
    float complexity = 0.65f, motion = 0.75f, space = 0.45f, density = 0.38f, chaos = 0.4f, brightness = 0.5f, musicality = 0.65f;
    float lengthMs = 600.0f;
    int   root = 9, scale = 1;
    Category category = Category::MicroDetails;
    static Intent from(const GenSettings& g);
};

struct ArchetypeDef
{
    const char* name;
    Category    category;
    MusicalRole role;
    float       baseWeight;
    float       lengthFactor;      // relative to the requested length
    std::function<void(CandyDNA&, Rng&, const Intent&)> build;
};

const std::vector<ArchetypeDef>& archetypeTable();
// indices into archetypeTable() belonging to a category
std::vector<int> archetypesOf(Category c);

// Shared building blocks (also used by regenerate / mutate / breed)
void initLayer(LayerConfig& l, EngineType e, uint32_t seed);
void randomEngineParams(LayerConfig& l, Rng& r, const Intent& in);        // plausible parameters for any engine
void applyModulation(CandyDNA& d, Rng& r, const Intent& in);
void applyEffects(CandyDNA& d, Rng& r, const Intent& in);
juce::String makeName(Rng& r, const char* archetype);
float pitchNote(Rng& r, const Intent& in, float lo, float hi);
} // namespace dali
