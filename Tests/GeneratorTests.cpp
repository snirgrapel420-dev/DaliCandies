#include "TestFramework.h"
#include "Generator/CandyGenerator.h"
#include <set>

using namespace dali;

namespace
{
GenSettings defaults(Category c) { GenSettings g; g.category = c; return g; }
juce::String dump(const CandyDNA& d) { return juce::JSON::toString(d.toVar(), true); }
}

DTEST(archetypes_cover_all_categories)
{
    const auto& t = archetypeTable();
    CHECK(t.size() >= 32);
    for (int c = 0; c < kNumCategories; ++c) CHECK(archetypesOf((Category) c).size() >= 3);
}

DTEST(every_archetype_renders_valid_sound)
{
    CandyGenerator gen;
    const auto& t = archetypeTable();
    int ok = 0, total = 0;
    for (int a = 0; a < (int) t.size(); ++a)
    {
        int okA = 0; const int tries = 6;
        for (int s = 0; s < tries; ++s)
        {
            CandyDNA d; ++total;
            if (gen.generateOne(defaults(t[(size_t) a].category), 1000u + (uint32_t) s * 7919u, 44100.0, d, a))
            {
                ++ok; ++okA;
                CHECK(d.activeLayerCount() >= 1);
                CHECK(std::isfinite(d.spectral.peakDb));
                { const bool pk = d.spectral.peakDb < -3.0f && d.spectral.peakDb > -9.5f; if (! pk) std::printf("    peak %s seed %u: %.2f dB master %.1f\n", t[(size_t) a].name, 1000u + (uint32_t) s * 7919u, d.spectral.peakDb, d.fx.masterGainDb); CHECK(pk); }
                CHECK(d.score > 0.0f);
            }
        }
        CHECK_MSG(okA >= 3, std::string(t[(size_t) a].name) + " accepted " + std::to_string(okA) + "/" + std::to_string(tries));
    }
    std::printf("    archetype acceptance %d / %d\n", ok, total);
    CHECK(ok >= total * 8 / 10);
}

DTEST(generation_is_deterministic)
{
    CandyGenerator g1, g2;
    for (int c = 0; c < kNumCategories; ++c)
    {
        CandyDNA a, b; bool oa = false, ob = false;
        for (uint32_t s = 5; s < 40 && ! (oa && ob); ++s) { oa = g1.generateOne(defaults((Category) c), s, 48000.0, a); ob = g2.generateOne(defaults((Category) c), s, 48000.0, b); if (oa != ob) CHECK(false); }
        CHECK(oa && ob);
        CHECK(dump(a) == dump(b));
    }
}

DTEST(batch_has_at_least_eight_diverse_candidates)
{
    CandyGenerator gen;
    for (int c = 0; c < kNumCategories; ++c)
    {
        GenerateOptions o; o.count = 8; o.sampleRate = 44100.0; GenerateStats st;
        auto v = gen.generateBatch(defaults((Category) c), 4242u + (uint32_t) c, o, &st);
        CHECK_MSG((int) v.size() >= 8, std::string(categoryName((Category) c)) + " produced " + std::to_string(v.size()));
        std::set<int> arch; std::set<uint64_t> ids; float minDist = 9;
        for (size_t i = 0; i < v.size(); ++i) { arch.insert(v[i].archetype); ids.insert(v[i].id); for (size_t j = i + 1; j < v.size(); ++j) minDist = std::min(minDist, CandyGenerator::distance(v[i], v[j])); }
        CHECK(ids.size() == v.size());
        CHECK_MSG(arch.size() >= 2, std::string(categoryName((Category) c)) + " archetypes " + std::to_string(arch.size()));
        CHECK(minDist > 0.02f);
        std::printf("    %-22s tried %d rejected %d  archetypes %d  minDist %.2f\n", categoryName((Category) c), st.tried, st.rejected, (int) arch.size(), minDist);
    }
}

DTEST(batch_cancel_stops_early)
{
    CandyGenerator gen; GenerateOptions o; o.count = 8; int calls = 0; o.cancelled = [&] { return ++calls > 2; };
    auto v = gen.generateBatch(defaults(Category::Transitions), 9u, o);
    CHECK((int) v.size() <= 3);
}

DTEST(settings_steer_results)
{
    CandyGenerator gen; GenerateOptions o; o.count = 8; o.oversample = 2;
    auto avgCentroid = [&](float bright)
    {
        GenSettings g = defaults(Category::AtmosphericCandy); g.brightness = bright;
        double s = 0; int n = 0; for (auto& d : gen.generateBatch(g, 77u, o)) { s += std::log2(d.spectral.centroidHz); ++n; }
        return n ? s / n : 0.0;
    };
    const double dark = avgCentroid(0.05f), bright = avgCentroid(0.95f);
    std::printf("    mean log2 centroid dark %.2f bright %.2f\n", dark, bright);
    CHECK(bright > dark + 0.3);
}

DTEST(mutate_respects_locks)
{
    CandyGenerator gen; CandyDNA d;
    for (uint32_t s = 1; s < 50; ++s) if (gen.generateOne(defaults(Category::Experimental), s, 44100.0, d, (int) archetypesOf(Category::Experimental)[2])) break;
    CHECK(d.numLayers >= 2);
    d.layers[0].locked = true; d.layers[1].lockMask = 0x000F;
    const auto l0 = d.layers[0]; const auto l1 = d.layers[1];
    auto m = gen.mutate(d, 123u, 0.9f, defaults(Category::Experimental));
    CHECK(m.id != d.id && m.parentA == d.id);
    for (int i = 0; i < kParamsPerLayer; ++i) CHECK(m.layers[0].p[(size_t) i] == l0.p[(size_t) i]);
    CHECK(m.layers[0].pitch == l0.pitch && m.layers[0].decayMs == l0.decayMs);
    for (int i = 0; i < 4; ++i) CHECK(m.layers[1].p[(size_t) i] == l1.p[(size_t) i]);
    bool changed = false; for (int i = 4; i < kParamsPerLayer; ++i) changed |= m.layers[1].p[(size_t) i] != l1.p[(size_t) i];
    CHECK(changed);
    CHECK(dump(gen.mutate(d, 123u, 0.9f, defaults(Category::Experimental))) == dump(m));      // deterministic
    CHECK(m.layers[0].p.size() == (size_t) kParamsPerLayer);
    for (auto& l : m.layers) for (float v : l.p) CHECK(v >= 0.0f && v <= 1.0f);
}

DTEST(regenerate_layer_keeps_locked_params)
{
    CandyGenerator gen; CandyDNA d;
    for (uint32_t s = 1; s < 50; ++s) if (gen.generateOne(defaults(Category::MetallicAccents), s, 44100.0, d)) break;
    d.layers[0].lockMask = 0x0003; const auto before = d.layers[0];
    gen.regenerateLayer(d, 0, 555u, defaults(Category::MetallicAccents));
    if (d.layers[0].engine == before.engine) { CHECK(d.layers[0].p[0] == before.p[0] && d.layers[0].p[1] == before.p[1]); }
    CHECK(d.layers[0].gainDb == before.gainDb);
    d.layers[0].locked = true; const auto frozen = d.layers[0];
    gen.regenerateLayer(d, 0, 556u, defaults(Category::MetallicAccents));
    CHECK(d.layers[0].seed == frozen.seed && d.layers[0].p == frozen.p);
}

DTEST(breed_produces_valid_child)
{
    CandyGenerator gen; CandyDNA a, b;
    for (uint32_t s = 1; s < 60; ++s) if (gen.generateOne(defaults(Category::PsychedelicMovement), s, 44100.0, a)) break;
    for (uint32_t s = 1; s < 60; ++s) if (gen.generateOne(defaults(Category::RhythmicGlitches), s, 44100.0, b)) break;
    int valid = 0;
    for (uint32_t s = 0; s < 6; ++s)
    {
        auto c = gen.breed(a, b, 900u + s, defaults(Category::MicroDetails));
        CHECK(c.parentA == a.id && c.parentB == b.id && c.activeLayerCount() >= 1);
        if (gen.analyseAndFinish(c, defaults(Category::MicroDetails), 44100.0)) ++valid;
    }
    CHECK(valid >= 3);
}

DTEST(dna_roundtrip_through_json)
{
    CandyGenerator gen; CandyDNA d;
    for (uint32_t s = 1; s < 60; ++s) if (gen.generateOne(defaults(Category::AtmosphericCandy), s, 44100.0, d)) break;
    CandyDNA e; CHECK(CandyDNA::fromVar(juce::JSON::parse(dump(d)), e));
    CHECK(dump(e) == dump(d));
}

DTEST(taste_model_biases_selection)
{
    TasteModel tm; CandyGenerator gen(&tm); CandyDNA d;
    for (uint32_t s = 1; s < 60; ++s) if (gen.generateOne(defaults(Category::MicroDetails), s, 44100.0, d, 0)) break;
    const float base = tm.archetypeMultiplier(Category::MicroDetails, 0);
    for (int i = 0; i < 6; ++i) tm.rate(d, +1);
    CHECK(tm.archetypeMultiplier(Category::MicroDetails, 0) > base * 1.4f);
    CHECK(tm.multiplier(d) > 1.0f);
    juce::TemporaryFile tf; CHECK(tm.saveToFile(tf.getFile()));
    TasteModel t2; CHECK(t2.loadFromFile(tf.getFile()));
    CHECK(std::abs(t2.archetypeMultiplier(Category::MicroDetails, 0) - tm.archetypeMultiplier(Category::MicroDetails, 0)) < 1e-5f);
    tm.reset(); CHECK(std::abs(tm.archetypeMultiplier(Category::MicroDetails, 0) - base) < 1e-6f && tm.ratingCount() == 0);
}

DTEST(history_recall)
{
    CandyHistory h; CandyDNA a, b; a.id = 11; b.id = 22; h.push(a); h.push(b);
    CHECK(h.find(11) && h.find(22) && ! h.find(33)); CHECK(h.back()->id == 22 && h.back(1)->id == 11);
    for (uint64_t i = 100; i < 200; ++i) { CandyDNA x; x.id = i; h.push(x, 16); }
    CHECK(h.size() == 16 && ! h.find(11));
}
