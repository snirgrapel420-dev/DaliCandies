// Tests for the Kitchen features: six families, 15 fusions, SPICE, KEEP skeleton, tempo-synced length.
#include "TestFramework.h"
#include "Generator/CandyGenerator.h"
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
