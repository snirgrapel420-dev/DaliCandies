// Tests for the Kitchen features: six families, 15 fusions, SPICE, KEEP skeleton, tempo-synced length.
#include "TestFramework.h"
#include "Generator/CandyGenerator.h"
#include "Sequencer/ContextBuilder.h"
#include <set>

using namespace dali;

namespace
{
GenSettings famSettings(int a, int b = -1) { GenSettings g; g.familyA = a; g.familyB = b; return g; }
bool routesValid(const CandyDNA& d)
{
    for (auto& r : d.routes)
    {
        if (! r.enabled) continue;
        int L = -1;
        if (r.dest < kLayerDestBase) L = r.dest / kParamsPerLayer; else if (r.dest < 96) L = (r.dest - kLayerDestBase) / 8;
        if (L >= 0 && (L >= kMaxLayers || ! d.layers[(size_t) L].active)) return false;
        if (r.source < 0 || r.source >= kNumModSources) return false;
    }
    return true;
}
}

DTEST(families_and_fusion_names)
{
    std::set<int> all;
    for (int f = 0; f < kNumFamilies; ++f)
    {
        const auto v = archetypesOfFamily(f);
        CHECK_MSG(v.size() >= 3, std::string(familyName(f)) + " has " + std::to_string(v.size()) + " archetypes");
        for (int i : v) CHECK(all.insert(i).second);                                  // no archetype belongs to two families
    }
    CHECK(all.size() == archetypeTable().size());                                     // and none is left out
    std::set<std::string> names; int pairs = 0;
    for (int a = 0; a < kNumFamilies; ++a) for (int b = a + 1; b < kNumFamilies; ++b)
    {
        const std::string n = fusionName(a, b); CHECK(! n.empty()); CHECK(fusionName(b, a) == n); names.insert(n); ++pairs;
    }
    CHECK(pairs == 15); CHECK(names.size() == 15); CHECK(kNumDishes == 21);
}

DTEST(every_family_pair_makes_valid_fusions)
{
    CandyGenerator gen;
    for (int a = 0; a < kNumFamilies; ++a) for (int b = a + 1; b < kNumFamilies; ++b)
    {
        int ok = 0, maxLayers = 0;
        for (uint32_t s = 0; s < 4; ++s)
        {
            CandyDNA d;
            if (! gen.generateOne(famSettings(a, b), 500u + s * 131u, 44100.0, d)) continue;
            ++ok; maxLayers = std::max(maxLayers, d.activeLayerCount());
            CHECK(d.activeLayerCount() >= 1 && d.activeLayerCount() <= kMaxLayers);
            CHECK_MSG(routesValid(d), std::string(fusionName(a, b)) + " has a route into a dead layer");
            CHECK(std::isfinite(d.spectral.peakDb) && d.spectral.peakDb < -3.0f && d.spectral.peakDb > -9.5f);
        }
        CHECK_MSG(ok >= 2, std::string(fusionName(a, b)) + " accepted " + std::to_string(ok) + "/4");
        CHECK_MSG(maxLayers >= 2, std::string(fusionName(a, b)) + " never combined layers of both families");
    }
}

DTEST(fusion_is_deterministic_and_differs_from_pure)
{
    CandyGenerator gen; CandyDNA x, y, pure;
    CHECK(gen.generateOne(famSettings(0, 4), 77, 44100.0, x));
    CHECK(gen.generateOne(famSettings(0, 4), 77, 44100.0, y));
    CHECK(juce::JSON::toString(x.toVar()) == juce::JSON::toString(y.toVar()));
    CHECK(gen.generateOne(famSettings(0), 77, 44100.0, pure));
    CHECK(pure.activeLayerCount() <= x.activeLayerCount());
    CHECK(CandyGenerator::distance(x, pure) > 0.05f);
}

DTEST(pure_family_only_uses_its_own_archetypes)
{
    CandyGenerator gen;
    for (int f = 0; f < kNumFamilies; ++f)
    {
        const auto pool = archetypesOfFamily(f); int ok = 0;
        for (uint32_t s = 0; s < 4; ++s)
        {
            CandyDNA d; if (! gen.generateOne(famSettings(f), 900u + s * 17u, 44100.0, d)) continue;
            ++ok; CHECK(std::find(pool.begin(), pool.end(), d.archetype) != pool.end());
        }
        CHECK(ok >= 2);
    }
}

DTEST(spice_adds_motion_and_weirdness)
{
    CandyGenerator gen; int moreMotion = 0, n = 0;
    for (uint32_t s = 0; s < 6; ++s)
    {
        GenSettings plain = famSettings(3), hot = famSettings(3); hot.spice = 1.0f;
        CandyDNA a, b; if (! gen.generateOne(plain, 300u + s, 44100.0, a) || ! gen.generateOne(hot, 300u + s, 44100.0, b)) continue;
        ++n; CHECK(routesValid(b)); CHECK(std::isfinite(b.spectral.peakDb));
        auto energy = [](const CandyDNA& d) { float e = 0; int c = 0; for (auto& r : d.routes) if (r.enabled) { e += std::abs(r.depth); ++c; } return e + 0.2f * (float) c; };
        if (energy(b) > energy(a) + 0.05f) ++moreMotion;
    }
    CHECK(n >= 4); CHECK_MSG(moreMotion >= n - 1, "spice raised modulation in " + std::to_string(moreMotion) + "/" + std::to_string(n));
    GenSettings z = famSettings(1); CandyDNA q = CandyDNA(); Rng r(3); CandyGenerator::applySpice(q, r, 0.0f);   // spice 0 is a no-op
    CHECK(! q.routes[0].enabled);
}

DTEST(keep_skeleton_varies_only_details)
{
    CandyGenerator gen; CandyDNA base;
    GenSettings g = famSettings(1, 5);
    CHECK(gen.generateOne(g, 4242, 44100.0, base));
    const auto vars = gen.variations(base, 11, 8, 0.3f, g, 44100.0);
    CHECK_MSG(vars.size() >= 5, "variations returned " + std::to_string(vars.size()));
    int differing = 0;
    for (auto& v : vars)
    {
        CHECK(v.id != base.id); CHECK(v.activeLayerCount() == base.activeLayerCount());
        bool diff = false;
        for (int l = 0; l < kMaxLayers; ++l)
        {
            const auto& a = base.layers[(size_t) l]; const auto& b = v.layers[(size_t) l];
            CHECK(a.active == b.active); CHECK(a.engine == b.engine); CHECK(a.insert == b.insert);
            CHECK(a.attackMs == b.attackMs); CHECK(a.decayMs == b.decayMs); CHECK(a.releaseMs == b.releaseMs); CHECK(a.durationMs == b.durationMs);
            CHECK(a.gainDb == b.gainDb); CHECK(a.pan == b.pan); CHECK(a.sendReverb == b.sendReverb);
            for (int i = 0; i < kParamsPerLayer; ++i) if (a.p[(size_t) i] != b.p[(size_t) i]) diff = true;
        }
        if (diff) ++differing;
    }
    CHECK_MSG(differing >= (int) vars.size() - 1, "variations that changed parameters: " + std::to_string(differing));
}

DTEST(tempo_synced_lengths_fit_their_window)
{
    CandyGenerator gen; int ok = 0, tries = 0;
    struct W { double beats; };
    for (double beats : { 0.25, 1.0, 4.0 })
        for (int f : { 0, 3, 5 })
            for (uint32_t s = 0; s < 3; ++s)
            {
                GenSettings g = famSettings(f); g.syncLength = true; g.lengthMode = 3; g.customLengthMs = (float) (beats * 60000.0 / 120.0);
                CandyDNA d; ++tries;
                if (! gen.generateOne(g, 100u + s * 31u, 44100.0, d)) continue;
                ++ok;
                CHECK_MSG(d.durationMs >= 0.5f * g.customLengthMs && d.durationMs <= 1.3f * g.customLengthMs,
                          std::string(familyName(f)) + " " + std::to_string(beats) + " beats: " + std::to_string(d.durationMs) + " ms vs " + std::to_string(g.customLengthMs));
            }
    CHECK_MSG(ok * 3 >= tries * 2, "synced candidates accepted " + std::to_string(ok) + "/" + std::to_string(tries));
}

// ---------------------------------------------------------------------------------------------------------------------------
// Context-aware placement
// ---------------------------------------------------------------------------------------------------------------------------
namespace
{
// 4 bars at 120 bpm: kick on every beat; dense bright hats in bars 1 and 3 only
TrackAnalysis syntheticTrack(float density = 0.9f)
{
    const double sr = 44100.0; const int bars = 4, N = (int) (bars * 4 * 0.5 * sr); std::vector<float> x((size_t) N, 0.0f); juce::Random rnd(9);
    for (int b = 0; b < bars * 4; ++b)
    {
        const int kk = (int) (b * 0.5 * sr); for (int i = 0; i < 7000 && kk + i < N; ++i) x[(size_t) (kk + i)] += 0.8f * std::sin(6.28318f * 55.0f * (float) i / (float) sr) * std::exp(-(float) i / 1800.0f);
    }
    for (int bar : { 0, 2 })
        for (int s = 0; s < 16; ++s) { const int h = (int) ((bar * 16 + s) * 0.125 * sr); for (int i = 0; i < 2600 && h + i < N; ++i) { const float n = rnd.nextFloat() * 2 - 1; x[(size_t) (h + i)] += 0.35f * n * std::exp(-(float) i / 700.0f); } }
    auto a = analyzeTrack(x.data(), N, sr, 120.0, 4);
    generateSuggestions(a, density, 0.2f, 5);
    return a;
}
PaletteEntry entry(int slot, std::array<float, kNumBands> bands, MusicalRole role = MusicalRole::Accent, float ms = 250.0f) { PaletteEntry e; e.slot = slot; e.bands = bands; e.role = role; e.durationMs = ms; return e; }
}

DTEST(context_builder_fits_sounds_to_the_free_band)
{
    auto a = syntheticTrack(); CHECK(a.valid && a.suggestions.size() >= 8);
    const auto dark = entry(0, { 0.0f, 0.05f, 0.35f, 0.4f, 0.15f, 0.05f, 0.0f, 0.0f });      // lives in the mids
    const auto bright = entry(1, { 0.0f, 0.0f, 0.0f, 0.02f, 0.08f, 0.3f, 0.4f, 0.2f });      // lives in the highs
    ContextOptions o; o.variety = 0.1f;
    auto mask = [&](const PaletteEntry& p, int step) { float m = 0; for (int b = 0; b < kNumBands; ++b) m += p.bands[(size_t) b] * a.band[(size_t) b][(size_t) step]; return m; };
    int better = 0, n = 0, brightPicked = 0; double chosenMask = 0, otherMask = 0;
    for (uint32_t seed = 1; seed <= 6; ++seed)
    {
        o.seed = seed; auto ev = buildContextEvents(a, { dark, bright }, o);
        for (auto& e : ev)
        {
            const int step = (int) std::lround(e.position / 0.25); const auto& ch = e.slot == 1 ? bright : dark; const auto& ot = e.slot == 1 ? dark : bright;
            ++n; if (mask(ch, step) <= mask(ot, step) + 1e-4f) ++better; chosenMask += mask(ch, step); otherMask += mask(ot, step); brightPicked += e.slot == 1;
        }
    }
    CHECK(n >= 12);
    CHECK_MSG(better * 100 >= n * 75, "the sound that masks less was chosen in " + std::to_string(better) + " of " + std::to_string(n));
    CHECK_MSG(chosenMask < otherMask * 0.8, "chosen masking " + std::to_string(chosenMask) + " vs the alternative " + std::to_string(otherMask));
    CHECK_MSG(brightPicked > 0 && brightPicked < n, "both sounds are used");
}

DTEST(context_builder_avoids_hits_spreads_over_the_palette_and_respects_room)
{
    auto a = syntheticTrack();
    std::vector<PaletteEntry> pal;
    for (int i = 0; i < 5; ++i) pal.push_back(entry(i, { 0.0f, 0.02f, 0.1f, 0.2f, 0.25f, 0.2f, 0.15f, 0.08f }, MusicalRole::Accent, 120.0f + 60.0f * (float) i));
    ContextOptions o; o.variety = 0.85f; o.seed = 11; ContextReport rep; auto ev = buildContextEvents(a, pal, o, &rep);
    CHECK(ev.size() >= 8);
    int repeats = 0, distinct = 0; for (int n : rep.perSlot) distinct += n > 0 ? 1 : 0;
    for (size_t i = 1; i < ev.size(); ++i) if (ev[i].slot == ev[i - 1].slot) ++repeats;
    CHECK_MSG(distinct >= 4, "different sounds used " + std::to_string(distinct));
    CHECK_MSG(repeats * 6 <= (int) ev.size(), "back-to-back repeats " + std::to_string(repeats) + " of " + std::to_string(ev.size()));
    for (auto& e : ev)
    {
        const int step = (int) std::lround(e.position / 0.25);
        CHECK(a.onset[(size_t) step] < 0.45f);                                   // never on top of a hit
        CHECK(e.lengthBeats > 0.0f && e.lengthBeats <= 0.25f + 1e-6f);           // sounds are only ever shortened to fit the room
        CHECK(e.frozen && e.velocity >= 0.12f && e.velocity <= 1.0f);
    }
    CHECK(buildContextEvents(a, pal, o).size() == ev.size());                    // deterministic
}

DTEST(context_builder_leads_into_section_changes_with_a_transition)
{
    auto a = syntheticTrack(0.6f); a.sectionBars = { 0, 2 };
    const auto riser = entry(3, { 0.0f, 0.0f, 0.05f, 0.15f, 0.3f, 0.3f, 0.15f, 0.05f }, MusicalRole::Transition, 900.0f);
    const auto tick = entry(0, { 0.0f, 0.0f, 0.1f, 0.3f, 0.3f, 0.2f, 0.08f, 0.02f }, MusicalRole::Punctuation, 90.0f);
    ContextOptions o; o.variety = 0.3f; ContextReport rep; auto ev = buildContextEvents(a, { tick, riser }, o, &rep);
    bool riserBeforeChange = false; for (auto& e : ev) if (e.slot == 3 && e.position >= 6.5 && e.position <= 8.0) riserBeforeChange = true;
    CHECK_MSG(riserBeforeChange, "no transition sound in the last beat before bar 3");
    int risersElsewhere = 0; for (auto& e : ev) if (e.slot == 3 && (e.position < 6.5 || e.position > 8.0)) ++risersElsewhere;
    CHECK_MSG(risersElsewhere <= 1, "transition sounds used away from section changes: " + std::to_string(risersElsewhere));
}
