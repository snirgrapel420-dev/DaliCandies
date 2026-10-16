#include "TestFramework.h"
#include "DSP/Engine.h"
#include "DSP/Modulation/ModEngine.h"
#include "DSP/Effects/Effects.h"
#include "DSP/CandyEngine.h"

using namespace dali;

namespace
{
struct Stats { double rms = 0; float peak = 0; bool finite = true; double energyTail = 0; };

LayerConfig makeLayer(EngineType t, uint32_t seed, float fill = -1.0f)
{
    LayerConfig l; l.active = true; l.engine = t; l.seed = seed; l.pitch = 60.0f; l.durationMs = 400.0f;
    Rng r(seed);
    for (auto& v : l.p) v = fill >= 0.0f ? fill : r.uniform();
    return l;
}

std::vector<float> renderEngine(EngineType t, const LayerConfig& cfg, double sr, int block, float seconds, QualityMode q = QualityMode::Standard, float vel = 0.9f)
{
    auto eng = createEngine(t);
    eng->prepare(sr, q);
    auto src = makeInternalSource(cfg, sr);
    eng->trigger(cfg, vel, cfg.seed, src.get());
    const int total = (int) (seconds * sr);
    std::vector<float> out; out.reserve((size_t) total * 2);
    std::vector<float> L((size_t) block), R((size_t) block);
    ControlFrame cf; for (int i = 0; i < kParamsPerLayer; ++i) cf.p[(size_t) i] = cfg.p[(size_t) i];
    cf.hz = midiToHz(cfg.pitch); cf.velocity = vel;
    int done = 0;
    while (done < total)
    {
        const int n = std::min(block, total - done);
        // process in control-rate chunks like the voice does (control frame refreshed per chunk)
        int o = 0;
        while (o < n)
        {
            const int c = std::min(kControlRate, n - o);
            cf.timeSec = (float) (done + o) / (float) sr; cf.progress = std::min(1.0f, cf.timeSec / 0.4f);
            eng->process(cf, L.data() + o, R.data() + o, c); o += c;
        }
        for (int i = 0; i < n; ++i) { out.push_back(L[(size_t) i]); out.push_back(R[(size_t) i]); }
        done += n;
    }
    return out;
}

Stats stats(const std::vector<float>& s)
{
    Stats st; double e = 0; double tail = 0; const size_t half = s.size() / 2;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const float v = s[i]; if (! std::isfinite(v)) st.finite = false;
        st.peak = std::max(st.peak, std::abs(v)); e += (double) v * v; if (i >= half) tail += (double) v * v;
    }
    st.rms = std::sqrt(e / std::max<size_t>(1, s.size())); st.energyTail = tail;
    return st;
}
} // namespace

DTEST(engines_produce_audio_all_sample_rates)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int e = 0; e < kNumEngines; ++e)
        {
            // several seeds with musically sensible randomised parameters
            int audible = 0;
            for (uint32_t seed = 1; seed <= 6; ++seed)
            {
                auto cfg = makeLayer((EngineType) e, seed * 17u);
                auto out = renderEngine((EngineType) e, cfg, sr, 512, 0.8f);
                auto st = stats(out);
                CHECK_MSG(st.finite, std::string(engineName((EngineType) e)) + " finite");
                CHECK_MSG(st.peak < 8.0f, std::string(engineName((EngineType) e)) + " bounded peak " + std::to_string(st.peak));
                if (st.rms > 1.0e-4) ++audible;
            }
            CHECK_MSG(audible >= 4, std::string(engineName((EngineType) e)) + " produces audible output at sr " + std::to_string((int) sr)
                                    + " (audible in " + std::to_string(audible) + "/6)");
        }
}

DTEST(engines_extreme_parameters_are_stable)
{
    for (int e = 0; e < kNumEngines; ++e)
        for (float fill : { 0.0f, 1.0f, 0.5f })
        {
            auto cfg = makeLayer((EngineType) e, 99, fill);
            for (double sr : { 44100.0, 96000.0 })
            {
                auto out = renderEngine((EngineType) e, cfg, sr, 64, 0.6f, QualityMode::High, 1.0f);
                auto st = stats(out);
                CHECK_MSG(st.finite, std::string(engineName((EngineType) e)) + " finite at fill " + std::to_string(fill));
                CHECK_MSG(st.peak < 12.0f, std::string(engineName((EngineType) e)) + " peak " + std::to_string(st.peak) + " fill " + std::to_string(fill));
            }
        }
}

DTEST(engines_are_deterministic)
{
    for (int e = 0; e < kNumEngines; ++e)
    {
        auto cfg = makeLayer((EngineType) e, 4242);
        auto a = renderEngine((EngineType) e, cfg, 48000.0, 128, 0.5f);
        auto b = renderEngine((EngineType) e, cfg, 48000.0, 128, 0.5f);
        CHECK_MSG(a == b, std::string(engineName((EngineType) e)) + " identical on repeat");
        auto c = renderEngine((EngineType) e, makeLayer((EngineType) e, 4243), 48000.0, 128, 0.5f);
        CHECK_MSG(a != c, std::string(engineName((EngineType) e)) + " differs for another seed");
    }
}

DTEST(engines_independent_of_block_size)
{
    // Output must not depend on the host buffer size (engines are fed in fixed control chunks).
    for (int e = 0; e < kNumEngines; ++e)
    {
        auto cfg = makeLayer((EngineType) e, 777);
        auto a = renderEngine((EngineType) e, cfg, 48000.0, 16, 0.4f);
        auto b = renderEngine((EngineType) e, cfg, 48000.0, 480, 0.4f);
        double dAB = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) dAB = std::max(dAB, (double) std::abs(a[i] - b[i]));
        CHECK_MSG(a.size() == b.size() && dAB == 0.0, std::string(engineName((EngineType) e)) + " block 16 vs 480 maxdiff " + std::to_string(dAB));
    }
}

DTEST(silence_in_silence_out_for_zero_velocity_micro)
{
    auto cfg = makeLayer(EngineType::MicroSynth, 5, 0.5f);
    auto out = renderEngine(EngineType::MicroSynth, cfg, 48000.0, 256, 0.2f, QualityMode::Standard, 0.0f);
    auto st = stats(out);
    CHECK(st.finite);
    CHECK(st.peak < 1.0f);
}

DTEST(spectral_engine_is_aligned_after_priming)
{
    // First audible samples must appear within a few ms of the trigger (latency managed by priming).
    auto cfg = makeLayer(EngineType::Spectral, 31, 0.5f);
    cfg.p[0] = 1.0f; cfg.p[9] = 0.0f; cfg.p[4] = 1.0f; cfg.p[1] = 0.0f; cfg.p[2] = 0.0f; cfg.p[3] = 0.5f; cfg.p[5] = 0.0f; cfg.p[7] = 0.0f; cfg.p[8] = 0.5f; cfg.p[12] = 0.5f;
    auto out = renderEngine(EngineType::Spectral, cfg, 48000.0, 256, 0.3f);
    size_t first = out.size();
    for (size_t i = 0; i < out.size(); ++i) if (std::abs(out[i]) > 1.0e-3f) { first = i / 2; break; }
    CHECK_MSG(first < (size_t) (0.005 * 48000.0), "first audible sample index " + std::to_string(first));
}

DTEST(pitch_tracks_for_micro_synth)
{
    // Sine, no FM / filter effects: measure the frequency by zero crossings.
    LayerConfig cfg = makeLayer(EngineType::MicroSynth, 3, 0.0f);
    cfg.p[0] = 0.0f; cfg.p[8] = 1.0f; cfg.p[9] = 0.0f; cfg.p[10] = 0.0f; cfg.p[6] = 0.5f; cfg.p[13] = 0.5f;
    for (float note : { 48.0f, 60.0f, 72.0f, 84.0f })
    {
        cfg.pitch = note;
        auto out = renderEngine(EngineType::MicroSynth, cfg, 48000.0, 256, 0.5f);
        int zc = 0; for (size_t i = 2 * 2000; i + 2 < out.size(); i += 2) if (out[i - 2] <= 0.0f && out[i] > 0.0f) ++zc;
        const double dur = (double) (out.size() / 2 - 2000) / 48000.0;
        const double hz = zc / dur, expect = midiToHz(note);
        CHECK_MSG(std::abs(hz - expect) / expect < 0.03, "note " + std::to_string(note) + " measured " + std::to_string(hz) + " expected " + std::to_string(expect));
    }
}

DTEST(hilbert_frequency_shift_moves_a_sine)
{
    Hilbert h; const double sr = 48000.0; const float f = 1000.0f, shift = 300.0f;
    std::vector<float> y; float ph = 0, sp = 0;
    for (int i = 0; i < 48000; ++i)
    {
        const float x = std::sin(kTwoPi * f * (float) i / (float) sr);
        float re, im; h.process(x, re, im); sp += shift / (float) sr; sp -= std::floor(sp);
        y.push_back(re * std::cos(kTwoPi * sp) - im * std::sin(kTwoPi * sp)); (void) ph;
    }
    int zc = 0; for (size_t i = 12000; i + 1 < y.size(); ++i) if (y[i - 1] <= 0.0f && y[i] > 0.0f) ++zc;
    const double hz = zc / ((double) (y.size() - 12000) / sr);
    CHECK_MSG(std::abs(hz - 1300.0) < 20.0 || std::abs(hz - 700.0) < 20.0, "shifted to " + std::to_string(hz));
    CHECK_MSG(std::abs(hz - 1300.0) < 20.0, "upward shift expected 1300 Hz, got " + std::to_string(hz));
}

DTEST(filter_resonance_is_stable)
{
    Svf f; f.setSampleRate(48000.0);
    FastNoise n; n.seed(5);
    for (float q : { 0.5f, 5.0f, 40.0f, 1000.0f })
        for (float fc : { 10.0f, 200.0f, 5000.0f, 22000.0f, 47000.0f })
        {
            f.reset(); f.set(fc, q); float peak = 0; bool ok = true;
            for (int i = 0; i < 48000; ++i) { float y = f.tick(n.white(), i % 4); if (! std::isfinite(y)) ok = false; peak = std::max(peak, std::abs(y)); }
            CHECK_MSG(ok && peak < 500.0f, "svf q=" + std::to_string(q) + " fc=" + std::to_string(fc) + " peak " + std::to_string(peak));
        }
}

DTEST(effects_are_stable_with_extreme_settings)
{
    const double sr = 48000.0; const int N = 48000;
    FastNoise n; n.seed(11);
    std::vector<float> inL(N), inR(N), oL(N), oR(N);
    for (int i = 0; i < N; ++i) { inL[(size_t) i] = i < 4000 ? n.white() * 0.5f : 0.0f; inR[(size_t) i] = inL[(size_t) i]; }

    DelayFx d; d.prepare(sr); d.setParams(9000, 5.0f /*clamped*/, 1.0f, true); d.process(inL.data(), inR.data(), oL.data(), oR.data(), N);
    float pk = 0; for (float v : oL) { CHECK(std::isfinite(v)); pk = std::max(pk, std::abs(v)); } CHECK_MSG(pk < 4.0f, "delay peak " + std::to_string(pk));

    ReverbFx r; r.prepare(sr); r.setParams(1.0f, 0.0f); r.process(inL.data(), inR.data(), oL.data(), oR.data(), N);
    pk = 0; for (float v : oL) { CHECK(std::isfinite(v)); pk = std::max(pk, std::abs(v)); } CHECK_MSG(pk < 4.0f, "reverb peak " + std::to_string(pk));
    // reverb tail must decay
    std::vector<float> z(N, 0.0f); float lastPk = 0; for (int rep = 0; rep < 12; ++rep) { r.process(z.data(), z.data(), oL.data(), oR.data(), N); }
    for (float v : oL) lastPk = std::max(lastPk, std::abs(v)); CHECK_MSG(lastPk < 1.0e-3f, "reverb tail decays, residual " + std::to_string(lastPk));

    for (int type = 0; type < 3; ++type)
    {
        ModFx m; m.prepare(sr); m.setParams(type, 12.0f, 1.0f, 0.88f); m.process(inL.data(), inR.data(), oL.data(), oR.data(), N);
        pk = 0; for (float v : oL) { CHECK(std::isfinite(v)); pk = std::max(pk, std::abs(v)); } CHECK_MSG(pk < 6.0f, "modfx type " + std::to_string(type) + " peak " + std::to_string(pk));
    }
    for (int t = 1; t < (int) InsertFx::Count; ++t)
        for (float a : { 0.0f, 0.5f, 1.0f }) for (float b : { 0.0f, 1.0f })
        {
            InsertFxUnit u; u.prepare(sr); u.start((InsertFx) t, 1);
            std::vector<float> L = inL, R = inR; u.process(L.data(), R.data(), N, a, b, 440.0f, 120.0f);
            pk = 0; bool fin = true; for (float v : L) { fin = fin && std::isfinite(v); pk = std::max(pk, std::abs(v)); }
            CHECK_MSG(fin && pk < 20.0f, std::string("insert ") + insertName((InsertFx) t) + " a=" + std::to_string(a) + " b=" + std::to_string(b) + " peak " + std::to_string(pk));
        }
}

DTEST(mod_engine_routes_and_validation)
{
    ModulatorBank bank; std::array<ModRoute, kNumModRoutes> routes;
    bank.lfo[0].shape = 0; bank.lfo[0].rateHz = 4.0f; bank.macros = { 0.25f, 0.5f, 0.75f, 1.0f };
    routes[0] = { kSrcLfo1, layerParamDest(0, 3), 0.5f, 0, 0.0f, false, 4, true };
    routes[1] = { kSrcMacro4, layerParamDest(1, 2), -0.5f, 0, 0.0f, false, 4, true };
    routes[2] = { kSrcEnvFollower, layerCtlDest(0, kLdGain), 1.0f, 0, 0.0f, false, 4, true };   // unsafe: must be rejected
    routes[3] = { kSrcVelocity, kGdWidth, 0.3f, 0, 0.0f, false, 4, true };
    ModEngine m; m.prepare(48000.0); m.start(bank, routes, 7, 0.8f, 60.0f); m.setMacros(bank.macros.data());
    float maxA = 0, minA = 0;
    for (int i = 0; i < 3000; ++i) { m.update(16, (float) i * 16.0f / 48000.0f); const float d = m.dest(layerParamDest(0, 3)); maxA = std::max(maxA, d); minA = std::min(minA, d); }
    CHECK_MSG(maxA > 0.45f && minA < -0.45f, "LFO route reaches +/-0.5: " + std::to_string(minA) + " .. " + std::to_string(maxA));
    CHECK(std::abs(m.dest(layerParamDest(1, 2)) + 0.5f) < 1.0e-5f);
    CHECK(std::abs(m.dest(layerCtlDest(0, kLdGain))) < 1.0e-6f);              // feedback-prone route was rejected
    CHECK(std::abs(m.dest(kGdWidth) - 0.24f) < 1.0e-4f);
    ModRoute bad { kSrcEnvFollower, kGdMasterGain, 1.0f, 0, 0, false, 4, true };
    CHECK(! ModEngine::validate(bad)); CHECK(! bad.enabled);
    ModRoute ok { kSrcLfo2, kGdMasterGain, 1.0f, 0, 0, false, 4, true };
    CHECK(ModEngine::validate(ok)); CHECK(ok.enabled);
}

DTEST(mod_engine_all_sources_finite_and_bounded)
{
    ModulatorBank bank; std::array<ModRoute, kNumModRoutes> routes;
    for (int i = 0; i < kNumLfos; ++i) { bank.lfo[(size_t) i].shape = i + 3; bank.lfo[(size_t) i].rateHz = 3.0f + i; for (int k = 0; k < kCurvePoints; ++k) bank.lfo[(size_t) i].curve[(size_t) k] = (float) (k & 3) / 3.0f; }
    bank.lfo[3].shape = 6;
    for (int i = 0; i < kNumMsegs; ++i) { auto& s = bank.mseg[(size_t) i]; s.numPoints = 4; s.t = { 0, 0.2f, 0.6f, 1.0f }; s.v = { 0, 1, 0.3f, 0 }; s.c = { 0, 0.5f, -0.5f, 0 }; s.loop = (i & 1); s.lengthMs = 200.0f; }
    for (int i = 0; i < kNumModRoutes; ++i) routes[(size_t) i] = { i % kNumModSources, i % 64, 1.0f, i % 4, 5.0f * (float) (i % 3), (i % 5) == 0, 4, true };
    ModEngine m; m.prepare(44100.0); m.start(bank, routes, 99, 1.0f, 72.0f); m.setHost(140.0f, 0.0);
    for (int i = 0; i < 4000; ++i)
    {
        m.setHost(140.0f, (double) i * 16.0 / 44100.0 * 140.0 / 60.0); m.update(16, (float) i * 16.0f / 44100.0f);
        for (int s = 0; s < kNumModSources; ++s) { const float v = m.source(s); CHECK(std::isfinite(v)); if (std::abs(v) > 1.0001f) { CHECK_MSG(false, std::string("source out of range ") + modSourceName(s)); return; } }
        for (int d = 0; d < kNumDestinations; ++d) if (! std::isfinite(m.dest(d)) || std::abs(m.dest(d)) > 2.0001f) { CHECK_MSG(false, "dest out of range"); return; }
    }
}

//==============================================================================
// Whole-engine behaviour
//==============================================================================
namespace
{
CandyDNA makeSimpleDna(uint32_t seed, int layers)
{
    CandyDNA d; d.seed = seed; d.numLayers = layers; d.name = "test";
    for (int l = 0; l < layers; ++l)
    {
        auto& L = d.layers[(size_t) l]; L = makeLayer((EngineType) ((l + (int) seed) % kNumEngines), seed * 31u + (uint32_t) l);
        L.active = true; L.pitch = 60.0f + (float) l * 7.0f; L.startOffsetMs = 5.0f * (float) l; L.sendDelay = 0.3f; L.sendReverb = 0.4f; L.sendMod = 0.2f;
        L.insert = (InsertFx) (1 + (l + (int) seed) % 8);
    }
    d.routes[0] = { kSrcLfo1, layerParamDest(0, 8), 0.4f, 0, 0, false, 4, true };
    d.routes[1] = { kSrcMseg1, layerCtlDest(0, kLdPan), 0.5f, 3, 10, false, 4, true };
    d.mods.mseg[0].numPoints = 3; d.mods.mseg[0].t = { 0, 0.5f, 1 }; d.mods.mseg[0].v = { 0, 1, 0 };
    return d;
}

std::vector<float> renderEvents(const CandyDNA& dna, double sr, int block, const std::vector<std::pair<int, float>>& events, int totalSamples, QualityMode q = QualityMode::Standard)
{
    CandyEngine eng; eng.prepare(sr, 2048, q);
    std::array<const CandyDNA*, kNumSoundSlots> arr {}; arr[0] = &dna;
    eng.publishBank(buildSoundBank(arr, sr));
    HostInfo h; h.bpm = 140.0; eng.setHost(h);
    std::vector<float> out; out.reserve((size_t) totalSamples * 2);
    std::vector<float> L((size_t) block), R((size_t) block);
    int pos = 0;
    while (pos < totalSamples)
    {
        const int n = std::min(block, totalSamples - pos);
        h.ppq = (double) pos / sr * 140.0 / 60.0; eng.setHost(h);
        for (auto& e : events) if (e.first >= pos && e.first < pos + n) { EventRequest r; r.sampleOffset = e.first - pos; r.velocity = e.second; r.slot = 0; r.eventSeed = (uint32_t) e.first; eng.queueEvent(r); }
        eng.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) { out.push_back(L[(size_t) i]); out.push_back(R[(size_t) i]); }
        pos += n;
    }
    return out;
}
}

DTEST(candy_engine_output_independent_of_host_block_size)
{
    for (uint32_t seed : { 1u, 2u, 3u, 4u })
    {
        auto dna = makeSimpleDna(seed, 3);
        std::vector<std::pair<int, float>> ev = { { 100, 0.9f }, { 5000, 0.5f }, { 5003, 1.0f }, { 20011, 0.7f } };
        auto ref = renderEvents(dna, 48000.0, 512, ev, 48000);
        for (int block : { 1, 7, 64, 100, 333, 1024, 2048 })
        {
            auto o = renderEvents(dna, 48000.0, block, ev, 48000);
            double md = 0; for (size_t i = 0; i < ref.size(); ++i) md = std::max(md, (double) std::abs(ref[i] - o[i]));
            CHECK_MSG(md < 1.0e-5, "seed " + std::to_string(seed) + " block " + std::to_string(block) + " maxdiff " + std::to_string(md));
        }
        auto st = stats(ref); CHECK(st.finite); CHECK_MSG(st.rms > 1.0e-5, "audible " + std::to_string(st.rms));
    }
}

DTEST(candy_engine_events_start_sample_accurately)
{
    CandyDNA d; d.numLayers = 1; auto& L = d.layers[0]; L = makeLayer(EngineType::MicroSynth, 3, 0.0f); L.p[0] = 0.0f; L.p[8] = 1.0f; L.p[9] = 0.0f; L.p[6] = 0.5f; L.p[13] = 0.5f;
    L.attackMs = 0.0f; L.gainDb = 0.0f; L.durationMs = 200.0f; d.fx.width = 1.0f; d.fx.lowCutHz = 10.0f;
    for (int offset : { 0, 1, 15, 16, 17, 300, 511 })
    {
        auto out = renderEvents(d, 48000.0, 512, { { offset + 1000, 1.0f } }, 4000);
        int first = -1; for (size_t i = 0; i < out.size(); i += 2) if (std::abs(out[i]) > 1.0e-4f) { first = (int) (i / 2); break; }
        CHECK_MSG(first >= offset + 1000 && first <= offset + 1000 + 40, "offset " + std::to_string(offset + 1000) + " first audible " + std::to_string(first));
    }
}

DTEST(candy_engine_voice_limit_and_stealing)
{
    CandyDNA d = makeSimpleDna(5, 4);
    for (auto& l : d.layers) { l.durationMs = 5000.0f; l.sustain = 1.0f; l.releaseMs = 2000.0f; }
    CandyEngine eng; eng.prepare(48000.0, 512, QualityMode::Standard);
    std::array<const CandyDNA*, kNumSoundSlots> arr {}; arr[0] = &d; eng.publishBank(buildSoundBank(arr, 48000.0));
    std::vector<float> L(512), R(512); bool finite = true; int maxActive = 0; float peak = 0;
    for (int blk = 0; blk < 200; ++blk)
    {
        for (int k = 0; k < 4; ++k) { EventRequest r; r.sampleOffset = k * 100; r.velocity = 1.0f; r.eventSeed = (uint32_t) (blk * 4 + k); eng.queueEvent(r); }
        eng.process(L.data(), R.data(), 512);
        for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite(L[(size_t) i]) && std::isfinite(R[(size_t) i]); peak = std::max(peak, std::abs(L[(size_t) i])); }
        maxActive = std::max(maxActive, eng.activeVoices());
    }
    CHECK(finite); CHECK_MSG(maxActive <= kMaxVoices + 2, "active voices " + std::to_string(maxActive));
    CHECK_MSG(peak <= 1.0001f, "output protected, peak " + std::to_string(peak));
}

DTEST(candy_engine_silence_and_tail_decay)
{
    CandyEngine eng; eng.prepare(48000.0, 512, QualityMode::Standard);
    std::vector<float> L(512), R(512);
    for (int i = 0; i < 20; ++i) { eng.process(L.data(), R.data(), 512); for (int k = 0; k < 512; ++k) CHECK(L[(size_t) k] == 0.0f && R[(size_t) k] == 0.0f); }
    auto d = makeSimpleDna(2, 2); std::array<const CandyDNA*, kNumSoundSlots> arr {}; arr[0] = &d; eng.publishBank(buildSoundBank(arr, 48000.0));
    EventRequest r; r.velocity = 1.0f; eng.queueEvent(r);
    float late = 0; for (int blk = 0; blk < 800; ++blk) { eng.process(L.data(), R.data(), 512); if (blk > 700) for (int k = 0; k < 512; ++k) late = std::max(late, std::abs(L[(size_t) k])); }
    CHECK_MSG(late < 1.0e-3f, "tail decays, residual " + std::to_string(late));
    CHECK(eng.activeVoices() == 0);
}

DTEST(candy_engine_sample_rates_and_quality_modes)
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (auto q : { QualityMode::Eco, QualityMode::Standard, QualityMode::High })
        {
            auto dna = makeSimpleDna(7, 4);
            auto out = renderEvents(dna, sr, 256, { { 0, 1.0f }, { (int) (sr * 0.25), 0.8f } }, (int) (sr * 1.0), q);
            auto st = stats(out);
            CHECK_MSG(st.finite && st.rms > 1.0e-5 && st.peak <= 1.0001f, "sr " + std::to_string((int) sr) + " q " + std::to_string((int) q) + " rms " + std::to_string(st.rms) + " peak " + std::to_string(st.peak));
        }
}

DTEST(candy_engine_mute_solo_layers)
{
    auto d = makeSimpleDna(9, 3);
    for (auto& l : d.layers) l.startOffsetMs = 0.0f;
    auto all = stats(renderEvents(d, 48000.0, 256, { { 0, 1.0f } }, 24000));
    d.layers[0].mute = d.layers[1].mute = d.layers[2].mute = true;
    auto none = stats(renderEvents(d, 48000.0, 256, { { 0, 1.0f } }, 24000));
    CHECK_MSG(none.rms < 1.0e-6 && all.rms > 1.0e-4, "mute all -> silence");
    d.layers[0].mute = d.layers[1].mute = d.layers[2].mute = false; d.layers[1].solo = true;
    auto solo = stats(renderEvents(d, 48000.0, 256, { { 0, 1.0f } }, 24000));
    CHECK_MSG(solo.rms > 1.0e-5 && solo.rms != all.rms, "solo plays only layer 2");
}
