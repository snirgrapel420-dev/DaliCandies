#pragma once
// Intelligent generation pipeline:
//   archetype choice (weighted, taste-aware) -> correlated parameters -> modulation -> effects
//   -> offline render preview -> analysis -> rejection -> auto gain -> score -> diverse selection.
// Also: mutate / regenerate-layer / evolve / breed / recall. Everything is deterministic for a given seed.
#include "Archetypes.h"
#include "Families.h"
#include "TasteModel.h"
#include "Analysis/AudioFeatures.h"
#include "Rendering/OfflineRenderer.h"
#include <functional>

namespace dali
{
struct GenerateOptions
{
    double sampleRate = 44100.0;
    int    count = 8;                 // candidates returned (>= 8 recommended)
    int    oversample = 3;            // candidates tried per returned candidate
    float  diversity = 0.45f;         // 0 = pure score, 1 = pure diversity
    std::function<bool()> cancelled;  // optional
    std::function<void(float)> progress;
};

struct GenerateStats { int tried = 0, rejected = 0, accepted = 0; };

class CandyGenerator
{
public:
    explicit CandyGenerator(const TasteModel* taste = nullptr) : taste_(taste) {}
    void setTaste(const TasteModel* t) { taste_ = t; }

    // One candidate (rendered, analysed, auto-gained). Returns false if rejected.
    bool generateOne(const GenSettings& s, uint32_t seed, double sr, CandyDNA& out, int forceArchetype = -1);
    std::vector<CandyDNA> generateBatch(const GenSettings& s, uint32_t seed, const GenerateOptions& o, GenerateStats* stats = nullptr);

    // DNA operations (never touch locked layers / locked parameter bits)
    // keepSkeleton: engines, layer structure, envelopes, levels and sends stay; only details (parameters, pitch, seeds, modulation) move
    CandyDNA mutate(const CandyDNA& d, uint32_t seed, float amount, const GenSettings& s, bool keepSkeleton = false);
    // KEEP: `count` rendered variations of a sound that share its skeleton (a few may be rejected -> fewer returned)
    std::vector<CandyDNA> variations(const CandyDNA& base, uint32_t seed, int count, float detail, const GenSettings& s, double sr);
    void     regenerateLayer(CandyDNA& d, int layer, uint32_t seed, const GenSettings& s);
    CandyDNA evolve(const CandyDNA& d, uint32_t seed, float amount, const GenSettings& s);   // gentle drift, keeps identity
    CandyDNA breed(const CandyDNA& a, const CandyDNA& b, uint32_t seed, const GenSettings& s);

    // Render + analyse + gain-stage an existing DNA (fills spectral chars, duration, score). False if invalid.
    bool analyseAndFinish(CandyDNA& d, const GenSettings& s, double sr, bool autoGain = true);
    float score(const CandyDNA& d, const BufferFeatures& f, const Intent& in) const;
    static float distance(const CandyDNA& a, const CandyDNA& b);

    // Fusion of two raw (un-analysed) sounds: layers of both, routes re-targeted, modulators re-allocated, FX blended.
    static CandyDNA fuse(const CandyDNA& a, const CandyDNA& b, uint32_t seed);
    // SPICE: more modulation depth and rate, a chaotic route, a "weird" insert.  spice 0 leaves the sound untouched.
    static void applySpice(CandyDNA& d, Rng& r, float spice);
    // Tempo-synced length: rescales envelopes / tails until the audible length is near the target.  False if it cannot be fitted.
    bool fitLength(CandyDNA& d, const GenSettings& s, double sr);

    static uint64_t makeId(uint32_t seed, int archetype, uint32_t salt);
    static bool validate(const BufferFeatures& f, float targetMs, float& whyNot);
private:
    int pickArchetype(Rng& r, const Intent& in) const;
    int pickFrom(Rng& r, const std::vector<int>& idx) const;
    CandyDNA buildRaw(const GenSettings& s, Rng& r, int archetype, const Intent& in) const;
    const TasteModel* taste_ = nullptr;
    OfflineRenderer renderer_;
};

// Bounded history used by Recall / Evolve (ring of the most recent sounds).
class CandyHistory
{
public:
    void push(const CandyDNA& d, size_t cap = 64);
    const CandyDNA* find(uint64_t id) const;
    const CandyDNA* back(size_t stepsBack = 0) const;
    size_t size() const { return items_.size(); }
    void clear() { items_.clear(); }
private:
    std::vector<CandyDNA> items_;
};
} // namespace dali
