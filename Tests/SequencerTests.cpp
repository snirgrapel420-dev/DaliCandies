#include "TestFramework.h"
#include "Sequencer/Sequencer.h"
#include "Generator/CandyGenerator.h"
#include <set>

using namespace dali;

namespace
{
SeqContext ctx0(int slots = 3) { SeqContext c; c.numSlots = slots; return c; }
SeqConfig cfgFor(SeqMode m, uint32_t seed = 7) { SeqConfig c; c.mode = m; c.seed = seed; c.bars = 8; c.enabled = true; return c; }
bool sorted(const std::vector<SeqEvent>& v) { for (size_t i = 1; i < v.size(); ++i) if (v[i].position < v[i - 1].position) return false; return true; }
}

DTEST(euclid_distribution)
{
    for (int steps : { 8, 12, 16, 32 })
        for (int hits = 0; hits <= steps; hits += 3)
            for (int rot : { 0, 3, -2 })
            {
                auto p = euclid(steps, hits, rot); int n = 0; for (bool b : p) n += b;
                CHECK(n == hits); CHECK((int) p.size() == steps);
            }
    auto p = euclid(16, 4, 0); CHECK(p[0] && p[4] && p[8] && p[12]);
    auto q = euclid(16, 4, 1); CHECK(q[1] && q[5] && q[9] && q[13] && ! q[0]);
}

DTEST(all_modes_plan_sorted_in_range_deterministic)
{
    for (int m = 0; m < (int) SeqMode::Count; ++m)
        for (int bars : { 1, 2, 4, 16, 32 })
            for (int div = 0; div < kNumDivisions; ++div)
                for (int feel = 0; feel < 3; ++feel)
                {
                    auto c = cfgFor((SeqMode) m, 11u + (uint32_t) m); c.bars = bars; c.division = div; c.feel = feel; c.swing = 0.3f; c.ratchetProb = 0.2f;
                    auto cx = ctx0();
                    auto a = planEvents(c, cx), b = planEvents(c, cx);
                    CHECK(sorted(a)); CHECK(a.size() == b.size());
                    const double total = bars * 4.0;
                    for (size_t i = 0; i < a.size(); ++i)
                    {
                        CHECK(a[i].position >= 0.0 && a[i].position < total);
                        CHECK(a[i].velocity > 0.0f && a[i].velocity <= 1.0f);
                        CHECK(a[i].slot >= 0 && a[i].slot < 3);
                        CHECK(a[i].lengthBeats > 0.0f && std::isfinite(a[i].lengthBeats));
                        if (i < b.size()) CHECK(a[i].position == b[i].position && a[i].modSeed == b[i].modSeed && a[i].velocity == b[i].velocity);
                    }
                }
}

DTEST(modes_produce_events_at_default_settings)
{
    for (int m = 0; m < (int) SeqMode::Count; ++m)
    {
        auto c = cfgFor((SeqMode) m); c.bars = 16; c.division = 2;
        auto e = planEvents(c, ctx0());
        std::printf("    mode %d: %zu events / 16 bars\n", m, e.size());
        CHECK_MSG(e.size() >= 3, "mode " + std::to_string(m));
        CHECK(e.size() < 16 * 24);
    }
}

DTEST(seed_changes_pattern)
{
    for (int m : { 0, 3, 4, 6 })
    {
        auto a = planEvents(cfgFor((SeqMode) m, 1), ctx0()), b = planEvents(cfgFor((SeqMode) m, 2), ctx0());
        bool same = a.size() == b.size(); if (same) for (size_t i = 0; i < a.size(); ++i) same &= a[i].position == b[i].position;
        CHECK(! same);
    }
}

DTEST(euclidean_mode_matches_pattern)
{
    auto c = cfgFor(SeqMode::Euclidean); c.euclidSteps = 16; c.euclidHits = 5; c.euclidRotate = 0; c.division = 2; c.ratchetProb = 0; c.bars = 2; c.swing = 0;
    auto cx = ctx0(); cx.negSpaceSensitivity = 0.0f;
    auto e = planEvents(c, cx); auto pat = euclid(16, 5, 0);
    std::set<int> got; for (auto& x : e) got.insert((int) std::lround(x.position / 0.25));
    std::set<int> want; for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 16; ++i) if (pat[(size_t) i]) want.insert(bar * 16 + i);
    CHECK(got == want);
}

DTEST(swing_delays_odd_steps_and_ratchets_subdivide)
{
    auto c = cfgFor(SeqMode::Euclidean); c.euclidSteps = 16; c.euclidHits = 16; c.division = 2; c.bars = 1; c.ratchetProb = 0; c.swing = 1.0f;
    auto cx = ctx0(); cx.negSpaceSensitivity = 0.0f;
    auto e = planEvents(c, cx);
    CHECK(e.size() == 16);
    for (size_t i = 0; i < e.size(); ++i) { const double want = i * 0.25 + ((i & 1) ? 0.125 : 0.0); CHECK(std::abs(e[i].position - want) < 1e-9); }
    c.swing = 0; c.ratchetProb = 1.0f; c.ratchetMax = 4; c.euclidHits = 4; c.bars = 2;
    auto r = planEvents(c, cx); CHECK(r.size() > 8);
    for (size_t i = 1; i < r.size(); ++i) CHECK(r[i].position > r[i - 1].position - 1e-9);
}

DTEST(negative_space_omits_more_when_sensitive)
{
    auto c = cfgFor(SeqMode::Euclidean); c.euclidHits = 12; c.bars = 8; c.ratchetProb = 0;
    auto lo = ctx0(); lo.negSpaceSensitivity = 0.0f; auto hi = ctx0(); hi.negSpaceSensitivity = 1.0f;
    const size_t nLo = planEvents(c, lo).size(), nHi = planEvents(c, hi).size();
    std::printf("    events at sensitivity 0: %zu, 1: %zu\n", nLo, nHi);
    CHECK(nHi < nLo);
}

DTEST(frozen_events_survive_replan)
{
    auto c = cfgFor(SeqMode::Sparse); c.bars = 4;
    auto base = planEvents(c, ctx0());
    CHECK(base.size() >= 2);
    c.events = base; c.events[0].frozen = true; c.events[1].locked = true;
    const auto f0 = c.events[0], f1 = c.events[1];
    c.seed = 999;
    auto again = planEvents(c, ctx0());
    int found = 0; for (auto& e : again) if ((e.position == f0.position && e.modSeed == f0.modSeed) || (e.position == f1.position && e.modSeed == f1.modSeed)) ++found;
    CHECK(found == 2);
    c.freezeAll = true; auto fz = planEvents(c, ctx0()); CHECK(fz.size() == base.size());
    for (size_t i = 0; i < fz.size(); ++i) CHECK(fz[i].position == base[i].position);
    // evolve leaves frozen alone, others change
    c.freezeAll = false; auto ev = evolveEvents(c, ctx0(), 5u);
    int still = 0; for (auto& e : ev) if (e.position == f0.position && e.modSeed == f0.modSeed) ++still;
    CHECK(still == 1);
}

DTEST(analysis_drives_groove_follower_into_gaps)
{
    // synthetic 8-bar "track": kicks on every beat (loud), silence elsewhere
    const double sr = 44100.0, bpm = 120.0; const int bars = 8; const int N = (int) (bars * 4 * 60.0 / bpm * sr);
    std::vector<float> x((size_t) N, 0.0f); const int beatS = (int) (60.0 / bpm * sr);
    for (int b = 0; b < bars * 4; ++b) for (int i = 0; i < 4000; ++i) { const int k = b * beatS + i; if (k < N) x[(size_t) k] = 0.9f * std::sin(6.2831853f * 60.0f * (float) i / (float) sr) * std::exp(-(float) i / 1200.0f); }
    auto an = analyzeTrack(x.data(), N, sr, bpm, 4);
    CHECK(an.valid);
    auto c = cfgFor(SeqMode::GrooveFollower); c.bars = bars; c.ratchetProb = 0; c.division = 2;
    auto cx = ctx0(); cx.analysis = &an; cx.negSpaceSensitivity = 0.2f; cx.bpm = bpm;
    auto e = planEvents(c, cx);
    CHECK(e.size() >= 8);
    int onKick = 0; for (auto& ev : e) if (std::abs(std::fmod(ev.position, 1.0)) < 1e-6) ++onKick;
    CHECK_MSG(onKick * 4 <= (int) e.size(), "events on kicks: " + std::to_string(onKick) + "/" + std::to_string(e.size()));
}

// ---- player ---------------------------------------------------------------------------------------------------
namespace
{
CandyDNA quickSound(Category c = Category::MicroDetails)
{
    CandyGenerator gen; CandyDNA d; GenSettings g; g.category = c;
    for (uint32_t s = 1; s < 80; ++s) if (gen.generateOne(g, s, 48000.0, d, (int) archetypesOf(c)[0])) break;
    return d;
}
void prepareEngine(CandyEngine& eng, const CandyDNA& d, double sr, int maxBlock)
{
    eng.prepare(sr, maxBlock, QualityMode::Standard);
    std::array<const CandyDNA*, kNumSoundSlots> arr {}; arr[0] = &d;
    eng.publishBank(buildSoundBank(arr, sr));
}
}

DTEST(player_emits_each_event_exactly_once_for_any_block_size)
{
    const auto snd = quickSound();
    const double sr = 48000.0, bpm = 120.0;                     // 24000 samples per beat
    auto c = cfgFor(SeqMode::Euclidean); c.euclidHits = 7; c.bars = 2; c.ratchetProb = 0; c.microtimingMs = 0;
    auto cx = ctx0(1); cx.negSpaceSensitivity = 0;
    auto pat = makePattern(c, cx, true);
    const size_t perLoop = pat->events.size(); CHECK(perLoop == 14);
    for (int block : { 32, 64, 100, 480, 1000, 4000 })
    {
        CandyEngine eng; prepareEngine(eng, snd, sr, 4096);
        SeqPlayer pl; pl.publish(pat);
        std::vector<float> L(4096), R(4096); int total = 0; double ppq = 0.0;
        for (int done = 0; done < 3 * 192000; done += block)
        {
            HostInfo h; h.bpm = bpm; h.ppq = ppq; h.playing = true; eng.setHost(h);
            total += pl.render(h, sr, block, eng);
            eng.process(L.data(), R.data(), block);
            ppq += block * bpm / 60.0 / sr;
        }
        CHECK_MSG((size_t) total == 3 * perLoop, "block " + std::to_string(block) + " total " + std::to_string(total));
    }
}

DTEST(player_events_start_sample_accurately_and_survive_tempo_change)
{
    const auto snd = quickSound(Category::MicroDetails);
    const double sr = 48000.0;
    SeqConfig c; c.mode = SeqMode::Sparse; c.bars = 1; c.enabled = true; c.microtimingMs = 0; c.lengthBeats = 0.25f;
    SeqEvent e; e.position = 1.0; e.velocity = 0.9f; e.lengthBeats = 0.25f; e.modSeed = 5; c.events = { e };
    auto pat = makePattern(c, ctx0(1), false); CHECK(pat->events.size() == 1);
    for (int block : { 64, 480, 997 })
    {
        CandyEngine eng; prepareEngine(eng, snd, sr, 4096); SeqPlayer pl; pl.publish(pat);
        const double bpm = 120.0; double ppq = 0.0; std::vector<float> L(4096), R(4096); long long pos = 0, first = -1;
        for (int done = 0; done < 48000 && first < 0; done += block)
        {
            HostInfo h; h.bpm = bpm; h.ppq = ppq; h.playing = true; eng.setHost(h);
            pl.render(h, sr, block, eng); eng.process(L.data(), R.data(), block);
            for (int i = 0; i < block && first < 0; ++i) if (std::abs(L[(size_t) i]) > 1e-7f || std::abs(R[(size_t) i]) > 1e-7f) first = pos + i;
            pos += block; ppq += block * bpm / 60.0 / sr;
        }
        CHECK_MSG(std::abs(first - 24000) <= 1, "block " + std::to_string(block) + " onset " + std::to_string(first));
    }
    // tempo change: 120 bpm for the first half beat, then 60 bpm -> the event at beat 1.0 lands later in samples
    {
        CandyEngine eng; prepareEngine(eng, snd, sr, 4096); SeqPlayer pl; pl.publish(pat);
        double ppq = 0.0; std::vector<float> L(4096), R(4096); long long pos = 0, first = -1; const int block = 256;
        for (int done = 0; done < 96000 && first < 0; done += block)
        {
            const double bpm = ppq < 0.5 ? 120.0 : 60.0;
            HostInfo h; h.bpm = bpm; h.ppq = ppq; h.playing = true; eng.setHost(h);
            pl.render(h, sr, block, eng); eng.process(L.data(), R.data(), block);
            for (int i = 0; i < block && first < 0; ++i) if (std::abs(L[(size_t) i]) > 1e-7f || std::abs(R[(size_t) i]) > 1e-7f) first = pos + i;
            pos += block; ppq += block * bpm / 60.0 / sr;
        }
        CHECK_MSG(first > 24000 + 4000 && first < 24000 + 20000, "tempo-change onset " + std::to_string(first));
    }
}

DTEST(player_ignores_stopped_transport_and_handles_jumps)
{
    const auto snd = quickSound(); const double sr = 44100.0;
    auto c = cfgFor(SeqMode::Euclidean); c.euclidHits = 8; c.bars = 1; c.ratchetProb = 0;
    auto cx = ctx0(1); cx.negSpaceSensitivity = 0;
    auto pat = makePattern(c, cx, true);
    CandyEngine eng; prepareEngine(eng, snd, sr, 1024); SeqPlayer pl; pl.publish(pat); std::vector<float> L(1024), R(1024);
    HostInfo h; h.bpm = 140; h.ppq = 0; h.playing = false;
    CHECK(pl.render(h, sr, 1024, eng) == 0);
    h.playing = true; int n = 0;
    for (double start : { 0.0, 7.0, 123.5, 0.0, 1000.25 }) { h.ppq = start; n += pl.render(h, sr, 1024, eng); eng.process(L.data(), R.data(), 1024); }
    CHECK(n >= 0);                                   // no crash / no runaway
    // probabilistic events: same loop index -> same decision
    SeqEvent e; e.probability = 0.5f; SeqPattern sp; sp.events = { e, e, e, e, e, e }; sp.cfg.seed = 3;
    for (size_t i = 0; i < 6; ++i) CHECK(SeqPlayer::eventFires(sp, i, 4) == SeqPlayer::eventFires(sp, i, 4));
    int fires = 0; for (int64_t k = 0; k < 400; ++k) fires += SeqPlayer::eventFires(sp, 2, k) ? 1 : 0;
    CHECK(fires > 140 && fires < 260);
}
