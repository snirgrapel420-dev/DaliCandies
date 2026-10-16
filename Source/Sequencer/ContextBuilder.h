#pragma once
// Context-aware pattern building: places candy where the analysed track has room and chooses, for every position,
// the sound of the palette that fits that spot (free frequency band, free time until the next hit, musical role),
// without repeating the same sound back to back.  Pure and deterministic: (analysis, palette, options) -> events.
#include "Analysis/TrackAnalyzer.h"
#include "Core/CandyDNA.h"
#include <vector>

namespace dali
{
struct PaletteEntry
{
    int slot = 0;                                   // sound slot this entry plays
    std::array<float, kNumBands> bands {};          // spectral shape of the sound (fractions of power)
    float durationMs = 300.0f;
    MusicalRole role = MusicalRole::Accent;
};

struct ContextOptions
{
    float variety = 0.6f;                           // 0 = always the best fit, 1 = spread over the whole palette
    float velBase = 0.8f, velRange = 0.25f;
    float pitchVariation = 2.0f;                    // semitones
    int   rootNote = 9, scale = 1; float musicality = 0.65f;
    uint32_t seed = 1;
    bool  addTransitions = true;                    // risers / transition sounds into estimated section changes
};

struct ContextReport { int placed = 0, transitions = 0, dropped = 0; std::array<int, kNumSoundSlots> perSlot {}; };

// number of 1/16 steps (max 16) from `step` until the track's next hit: the room a sound placed there has
int gapStepsAt(const TrackAnalysis& a, int step);
// cost of putting palette sound `p` at `step` (lower = better): masking of the track's busy bands, length vs room, role vs position
float placementCost(const TrackAnalysis& a, int step, const PaletteEntry& p);

std::vector<SeqEvent> buildContextEvents(const TrackAnalysis& a, const std::vector<PaletteEntry>& palette, const ContextOptions& o, ContextReport* report = nullptr);
}
