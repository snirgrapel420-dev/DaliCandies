#include "TestFramework.h"
#include "Plugin/CandyProcessor.h"
#include <cstdlib>
#include <new>
#include <thread>

// ---- allocation tracking (real-time safety check): counts allocations made on a tracked thread -------------------------------
static thread_local bool gTrackAllocs = false;
static std::atomic<long> gAllocCount { 0 };
#if ! defined(__SANITIZE_ADDRESS__)      // under ASan the sanitizer owns operator new/delete; the allocation-free test is skipped there
void* operator new(std::size_t n) { if (gTrackAllocs) ++gAllocCount; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gTrackAllocs) ++gAllocCount; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

using namespace dali;

namespace
{
struct TestPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm(bpm); i.setPpqPosition(ppq); i.setIsPlaying(playing); i.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 }); return i; }
};

void setDataDirEnv(const juce::String& path)
{
#if JUCE_WINDOWS
    _putenv_s("DALICANDY_DATA_DIR", path.toRawUTF8());
#else
    setenv("DALICANDY_DATA_DIR", path.toRawUTF8(), 1);
#endif
}

void setupEnv()
{
    static juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dalicandy_test_data");
    static bool done = false; if (! done) { dir.deleteRecursively(); dir.createDirectory(); setDataDirEnv(dir.getFullPathName()); done = true; }
}
void setP(CandyProcessor& p, const char* id, float plain) { auto* q = p.apvts.getParameter(id); q->setValueNotifyingHost(q->convertTo0to1(plain)); }
float getP(CandyProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }
bool waitFor(const std::function<bool()>& cond, int timeoutMs = 90000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! cond()) { if (juce::Time::getMillisecondCounter() > end) return false; juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }
    return true;
}
struct Rendered { std::vector<float> L, R; bool finite = true; float peak = 0; };
Rendered run(CandyProcessor& p, int totalSamples, int block, const std::vector<std::pair<int, int>>& notes = {}, TestPlayHead* ph = nullptr, const float* inputNoise = nullptr)
{
    Rendered r; juce::AudioBuffer<float> buf(2, block); juce::Random rnd(5);
    if (ph) p.setPlayHead(ph);
    for (int pos = 0; pos < totalSamples; pos += block)
    {
        const int n = std::min(block, totalSamples - pos); buf.setSize(2, n, false, false, true); buf.clear();
        if (inputNoise) for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample(c, i, inputNoise[pos + i] * (c ? 0.9f : 1.0f));
        juce::MidiBuffer midi;
        for (auto& [at, note] : notes) if (at >= pos && at < pos + n) midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), at - pos);
        if (ph) ph->ppq = (double) pos / p.getSampleRate() * ph->bpm / 60.0;
        p.processBlock(buf, midi);
        for (int i = 0; i < n; ++i) for (int c = 0; c < 2; ++c) { const float v = buf.getSample(c, i); if (! std::isfinite(v)) r.finite = false; r.peak = std::max(r.peak, std::abs(v)); (c ? r.R : r.L).push_back(v); }
    }
    if (ph) p.setPlayHead(nullptr);
    return r;
}
void prepare(CandyProcessor& p, double sr, int block) { p.setRateAndBufferSizeDetails(sr, block); p.prepareToPlay(sr, block); }
}

DTEST(processor_defaults_match_specification)
{
    setupEnv();
    for (bool fx : { false, true })
    {
        CandyProcessor p(fx);
        CHECK(getP(p, pid::complexity) == 65 && getP(p, pid::motion) == 75 && getP(p, pid::space) == 45 && getP(p, pid::density) == 38);
        CHECK(getP(p, pid::chaos) == 40 && getP(p, pid::brightness) == 50 && getP(p, pid::musicality) == 65 && getP(p, pid::lengthMode) == 1);
        CHECK(p.getParameters().size() >= 40);
        CHECK(p.mainSound().activeLayerCount() >= 1 && p.presets().entries().size() >= 48);
        CHECK(p.getName().startsWithIgnoreCase("DaliCandy"));
        CHECK(p.acceptsMidi());                       // both variants take MIDI
        CHECK(p.getBusesLayout().getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
        if (fx) { CHECK(p.getBusCount(true) == 2); CHECK(! p.getBus(true, 1)->isEnabledByDefault()); } else CHECK(p.getBusCount(true) == 0);
    }
}

DTEST(processor_state_roundtrip_and_corrupt_data)
{
    setupEnv();
    CandyProcessor a(false); prepare(a, 48000.0, 512);
    setP(a, pid::complexity, 91); setP(a, pid::seqMode, 5); setP(a, pid::seqBars, 12); setP(a, pid::macro3, 20); setP(a, pid::scale, 3);
    a.editMain([](CandyDNA& d) { d.name = "Roundtrip Candy"; d.layers[0].gainDb = -9.5f; d.layers[0].p[3] = 0.321f; }, true);
    a.uiState = juce::JSON::parse("{\"page\":3}");
    a.presets().rescan();
    juce::MemoryBlock mb; a.getStateInformation(mb); CHECK(mb.getSize() > 1000);
    CandyProcessor b(false); prepare(b, 48000.0, 512); b.setStateInformation(mb.getData(), (int) mb.getSize());
    CHECK(getP(b, pid::complexity) == 91 && getP(b, pid::seqMode) == 5 && getP(b, pid::seqBars) == 12 && getP(b, pid::scale) == 3 && std::abs(getP(b, pid::macro3) - 20) < 0.01f);
    CHECK(b.mainSound().name == "Roundtrip Candy" && b.mainSound().layers[0].gainDb == -9.5f && b.mainSound().layers[0].p[3] == 0.321f);
    CHECK((int) b.uiState["page"] == 3);
    juce::MemoryBlock mb2; b.getStateInformation(mb2);
    auto xa = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()), xb = juce::AudioProcessor::getXmlFromBinary(mb2.getData(), (int) mb2.getSize());
    CHECK(xa && xb && xa->getChildByName("Preset")->getAllSubText() == xb->getChildByName("Preset")->getAllSubText());
    // garbage must not crash or damage the current state
    const auto before = b.mainSound().name;
    const char junk[] = "this is not a state"; b.setStateInformation(junk, sizeof(junk)); b.setStateInformation(nullptr, 0);
    juce::MemoryBlock trunc(mb.getData(), mb.getSize() / 3); b.setStateInformation(trunc.getData(), (int) trunc.getSize());
    CHECK(b.mainSound().name == before);
    // bad preset inside valid xml
    auto xml = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()); xml->getChildByName("Preset")->deleteAllTextElements(); xml->getChildByName("Preset")->addTextElement("{ broken");
    juce::MemoryBlock bad; juce::AudioProcessor::copyXmlToBinary(*xml, bad); b.setStateInformation(bad.getData(), (int) bad.getSize());
    CHECK(b.mainSound().name == before);
}

DTEST(instrument_midi_renders_finite_audio_all_rates_and_blocks)
{
    setupEnv();
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 7, 64, 480, 1024, 4096 })
        {
            if (block == 1 && sr > 50000.0) continue;                  // 1-sample blocks are slow; covered at 44.1/48
            CandyProcessor p(false); setP(p, pid::seqOn, 0); prepare(p, sr, block);
            const int total = block == 1 ? 6000 : (int) (sr * 0.4);
            auto r = run(p, total, block, { { 100, 60 }, { total / 2, 67 } });
            CHECK_MSG(r.finite, "sr " + std::to_string(sr) + " block " + std::to_string(block));
            CHECK_MSG(r.peak > 0.01f && r.peak <= 1.0f, "peak " + std::to_string(r.peak) + " sr " + std::to_string(sr) + " block " + std::to_string(block));
        }
}

DTEST(instrument_output_is_independent_of_host_block_size)
{
    setupEnv();
    std::vector<Rendered> outs;
    for (int block : { 32, 100, 512, 2048 })
    {
        CandyProcessor p(false); setP(p, pid::seqOn, 0); setP(p, pid::wet, 100); prepare(p, 48000.0, block);
        outs.push_back(run(p, 48000, block, { { 777, 60 }, { 5000, 64 }, { 20001, 72 } }));
    }
    for (size_t k = 1; k < outs.size(); ++k)
    {
        double maxd = 0; for (size_t i = 0; i < outs[0].L.size(); ++i) maxd = std::max(maxd, (double) std::abs(outs[0].L[i] - outs[k].L[i]));
        CHECK_MSG(maxd < 1e-5, "block variant " + std::to_string(k) + " diff " + std::to_string(maxd));
    }
}

DTEST(sequencer_is_host_synced_and_block_size_independent)
{
    setupEnv();
    std::vector<Rendered> outs;
    for (int block : { 64, 441, 1024 })
    {
        CandyProcessor p(false); setP(p, pid::seqOn, 1); setP(p, pid::seqMode, 5); setP(p, pid::euHits, 6); setP(p, pid::seqBars, 2); setP(p, pid::seqRatchetProb, 0); setP(p, pid::negSpace, 0);
        prepare(p, 48000.0, block); p.regeneratePattern(false);
        TestPlayHead ph; ph.bpm = 130.0;
        outs.push_back(run(p, 48000 * 4, block, {}, &ph));
        CHECK(outs.back().finite); CHECK(outs.back().peak > 0.02f);
    }
    for (size_t k = 1; k < outs.size(); ++k)
    {
        double maxd = 0; for (size_t i = 0; i < outs[0].L.size(); ++i) maxd = std::max(maxd, (double) std::abs(outs[0].L[i] - outs[k].L[i]));
        CHECK_MSG(maxd < 1e-4, "sequencer block variant " + std::to_string(k) + " diff " + std::to_string(maxd));
    }
    // stopped transport -> silence (free run off)
    CandyProcessor p(false); prepare(p, 48000.0, 512); TestPlayHead ph; ph.playing = false; auto r = run(p, 24000, 512, {}, &ph); CHECK(r.peak == 0.0f);
    // free run produces sound without a host transport
    CandyProcessor q(false); setP(q, pid::seqFreeRun, 1); setP(q, pid::seqMode, 5); setP(q, pid::euHits, 8); prepare(q, 48000.0, 512); auto fr = run(q, 48000 * 2, 512); CHECK(fr.peak > 0.02f);
}

DTEST(fx_dry_wet_sidechain_and_passthrough)
{
    setupEnv();
    std::vector<float> noise(48000); juce::Random rnd(11); for (auto& v : noise) v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.3f;
    CandyProcessor p(true); setP(p, pid::seqOn, 0); setP(p, pid::dry, 100); setP(p, pid::wet, 0); prepare(p, 48000.0, 256);
    auto r = run(p, 48000, 256, {}, nullptr, noise.data());
    double md = 0; for (int i = 0; i < 48000; ++i) md = std::max(md, (double) std::abs(r.L[(size_t) i] - noise[(size_t) i])); CHECK(md < 1e-6);
    CandyProcessor q(true); setP(q, pid::seqOn, 0); setP(q, pid::dry, 0); setP(q, pid::wet, 100); prepare(q, 48000.0, 256);
    auto s = run(q, 48000, 256, {}, nullptr, noise.data()); CHECK(s.peak == 0.0f);                  // no events -> candy bus is silent, input removed
    // live analyzer follows the input
    CHECK(q.live().rms() > 0.05f && q.live().peak() > 0.1f);
    // sidechain: with the sidechain bus enabled the analysis follows it
    CandyProcessor sc(true);
    juce::AudioProcessor::BusesLayout lay = sc.getBusesLayout(); lay.inputBuses.set(1, juce::AudioChannelSet::stereo());
    CHECK(sc.setBusesLayout(lay)); setP(sc, pid::anaSource, 1); prepare(sc, 48000.0, 256);
    CHECK(sc.getTotalNumInputChannels() == 4);
    juce::AudioBuffer<float> buf(4, 256); juce::MidiBuffer midi;
    for (int blk = 0; blk < 40; ++blk) { buf.clear(); for (int i = 0; i < 256; ++i) { buf.setSample(2, i, 0.5f * std::sin(0.2f * (float) (blk * 256 + i))); buf.setSample(3, i, 0.5f * std::sin(0.2f * (float) (blk * 256 + i))); } sc.processBlock(buf, midi); }
    CHECK(sc.live().rms() > 0.1f);                              // main input was silent: energy comes from the sidechain
    // unsupported layouts are refused
    juce::AudioProcessor::BusesLayout bad = sc.getBusesLayout(); bad.outputBuses.set(0, juce::AudioChannelSet::mono()); CHECK(! sc.checkBusesLayoutSupported(bad));
}

DTEST(unified_plugin_is_instrument_and_effect_in_one)
{
    setupEnv();
    std::vector<float> noise(48000); juce::Random rnd(21); for (auto& v : noise) v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.25f;
    // effect use: input connected -> dry passes through AND MIDI notes add candy on top
    CandyProcessor p(true); setP(p, pid::seqOn, 0); setP(p, pid::dry, 100); setP(p, pid::wet, 100); prepare(p, 48000.0, 256);
    CHECK(p.acceptsMidi() && p.getBusCount(true) == 2 && p.getBusCount(false) == 1);
    auto both = run(p, 48000, 256, { { 2000, 60 }, { 20000, 67 } }, nullptr, noise.data());
    double extra = 0; for (int i = 2000; i < 12000; ++i) extra = std::max(extra, (double) std::abs(both.L[(size_t) i] - noise[(size_t) i]));
    CHECK(extra > 0.02);                                            // candy is audible on top of the passed-through input
    double before = 0; for (int i = 0; i < 1900; ++i) before = std::max(before, (double) std::abs(both.L[(size_t) i] - noise[(size_t) i]));
    CHECK(before < 1e-6);                                            // before the first note the input is untouched
    // instrument use: the main input is switched off (hosts do this on instrument tracks) -> no pass-through, MIDI still plays
    CandyProcessor q(true); setP(q, pid::seqOn, 0);
    juce::AudioProcessor::BusesLayout lay = q.getBusesLayout(); lay.inputBuses.set(0, juce::AudioChannelSet::disabled());
    CHECK(q.checkBusesLayoutSupported(lay)); CHECK(q.setBusesLayout(lay)); prepare(q, 48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256); float peak = 0; bool silentBefore = true;
    for (int blk = 0; blk < 120; ++blk)
    {
        juce::MidiBuffer midi; if (blk == 10) midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 110), 5);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample(c, i, 0.5f);      // garbage a host might leave in the buffer
        q.processBlock(buf, midi);
        for (int i = 0; i < 256; ++i) { const float v = std::abs(buf.getSample(0, i)); if (blk < 10 && v > 0.0f) silentBefore = false; peak = std::max(peak, v); }
    }
    CHECK(silentBefore); CHECK(peak > 0.01f);
    // mono / surround main inputs are refused, disabled and stereo are accepted
    juce::AudioProcessor::BusesLayout mono = p.getBusesLayout(); mono.inputBuses.set(0, juce::AudioChannelSet::mono()); CHECK(! p.checkBusesLayoutSupported(mono));
}

DTEST(realtime_safety_no_allocations_in_process_block)
{
    setupEnv();
#if defined(__SANITIZE_ADDRESS__)
    std::printf("    (skipped under AddressSanitizer: allocation counting is covered by the normal build)\n"); return;
#endif
    { gAllocCount = 0; gTrackAllocs = true; static int* volatile sink; sink = new int[4]; gTrackAllocs = false; delete[] sink; CHECK(gAllocCount.load() == 1); }      // the tracker really works
    for (bool fx : { false, true })
    {
        CandyProcessor p(fx); setP(p, pid::seqOn, 1); setP(p, pid::seqMode, 3); setP(p, pid::density, 90); setP(p, pid::seqFreeRun, 1); prepare(p, 48000.0, 256);
        p.regeneratePattern(true);
        std::vector<float> noise(256 * 400); juce::Random rnd(3); for (auto& v : noise) v = rnd.nextFloat() * 0.4f - 0.2f;
        juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer midi;
        TestPlayHead ph; p.setPlayHead(&ph);
        for (int blk = 0; blk < 8; ++blk) { buf.clear(); p.processBlock(buf, midi); }          // warm-up
        gAllocCount = 0; gTrackAllocs = true;
        for (int blk = 0; blk < 400; ++blk)
        {
            for (int i = 0; i < 256; ++i) { buf.setSample(0, i, noise[(size_t) (blk * 256 + i)]); buf.setSample(1, i, noise[(size_t) (blk * 256 + i)]); }
            midi.clear(); if (blk % 7 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + blk % 12, (juce::uint8) 90), 10);
            ph.ppq = blk * 256.0 / 48000.0 * 2.0;
            p.processBlock(buf, midi);
        }
        gTrackAllocs = false; p.setPlayHead(nullptr);
        CHECK_MSG(gAllocCount.load() == 0, std::string(fx ? "fx" : "instrument") + " allocations in processBlock: " + std::to_string(gAllocCount.load()));
    }
}

DTEST(generation_selection_dna_operations_via_processor)
{
    setupEnv();
    CandyProcessor p(false); prepare(p, 44100.0, 512);
    p.generate();
    CHECK(waitFor([&] { return p.candidates().size() >= 8; }));
    auto cands = p.candidates(); CHECK(cands.size() >= 8);
    for (auto& c : cands) { CHECK(c.wave.size() == 192); CHECK(c.dna.score > 0.0f); }
    const auto idBefore = p.mainSound().id;
    p.selectCandidate(2); CHECK(p.mainSound().id == cands[2].dna.id && p.mainSound().id != idBefore);
    waitFor([&] { return ! p.busy(); }, 2000);
    p.mutateMain(0.4f); CHECK(waitFor([&] { return p.mainSound().parentA == cands[2].dna.id; }));
    const auto mutated = p.mainSound().id;
    p.evolveMain(0.4f); CHECK(waitFor([&] { return p.mainSound().id != mutated; }));
    const auto evolved = p.mainSound().id;
    p.breedMainWith(cands[5].dna); CHECK(waitFor([&] { return p.mainSound().parentB == cands[5].dna.id; }));
    p.recall(1); CHECK(p.mainSound().id != 0);
    p.setMainSound(cands[3].dna); p.regenerateLayer(0); CHECK(waitFor([&] { return p.mainSound().id != cands[3].dna.id; }));
    const auto before = p.taste().ratingCount(); p.rate(+1); p.rate(-1); CHECK(p.taste().ratingCount() == before + 2);
    p.candidateToSlot(4, 3); CHECK(p.presetSnapshot().slotUsed[3]);
    // cancel
    p.generate(); p.cancelWork(); waitFor([&] { return ! p.busy(); }, 30000);
    // audio still fine after all that
    setP(p, pid::seqOn, 0); auto r = run(p, 22050, 512, { { 10, 60 } }); CHECK(r.finite && r.peak > 0.01f);
}

DTEST(capture_analysis_suggestions_and_placement)
{
    setupEnv();
    CandyProcessor p(true); setP(p, pid::capBars, 0); setP(p, pid::seqOn, 0); prepare(p, 44100.0, 512);
    // synthetic 4 bar loop at 120 bpm: kick on every beat, hat on off-beats
    const double sr = 44100.0; const int bars = 4, N = (int) (bars * 4 * 0.5 * sr); std::vector<float> x((size_t) N, 0.0f);
    juce::Random rnd(2);
    for (int b = 0; b < bars * 4; ++b) { const int k = (int) (b * 0.5 * sr); for (int i = 0; i < 6000 && k + i < N; ++i) x[(size_t) (k + i)] += 0.8f * std::sin(6.28318f * 55.0f * (float) i / (float) sr) * std::exp(-(float) i / 1500.0f);
                                          const int h = k + (int) (0.25 * sr); for (int i = 0; i < 1500 && h + i < N; ++i) x[(size_t) (h + i)] += 0.2f * (rnd.nextFloat() * 2 - 1) * std::exp(-(float) i / 300.0f); }
    p.startCapture(); CHECK(p.captureState() == 1);
    TestPlayHead ph; ph.bpm = 120.0; juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; p.setPlayHead(&ph);
    int pos = 0; int guard = 0;
    while (p.captureState() != 3 && guard++ < 2000)
    {
        for (int i = 0; i < 512; ++i) { const float v = pos + i < N ? x[(size_t) (pos + i)] : 0.0f; buf.setSample(0, i, v); buf.setSample(1, i, v); }
        ph.ppq = (double) pos / sr * 2.0; p.processBlock(buf, midi); pos += 512;
    }
    p.setPlayHead(nullptr);
    CHECK(p.captureState() == 3);
    CHECK(waitFor([&] { return p.analysis() != nullptr; }));
    auto a = p.analysis(); CHECK(a && a->valid && a->bars == 4 && ! a->suggestions.empty());
    int onKick = 0; for (auto& s : a->suggestions) if (s.step % 8 == 0) ++onKick;
    CHECK_MSG(onKick * 3 <= (int) a->suggestions.size(), "suggestions on kicks " + std::to_string(onKick) + "/" + std::to_string(a->suggestions.size()));
    CHECK(p.masking().overall >= 0.0f);
    // user edits survive regeneration
    p.editAnalysis([](TrackAnalysis& t) { t.suggestions[0].locked = true; t.suggestions[1].rejected = true; });
    const int lockedStep = p.analysis()->suggestions[0].step, rejectedStep = p.analysis()->suggestions[1].step;
    p.regenerateSuggestions(); auto b = p.analysis(); bool lockKept = false, rejAgain = false;
    for (auto& s : b->suggestions) { if (s.step == lockedStep && s.locked) lockKept = true; if (s.step == rejectedStep && ! s.rejected) rejAgain = true; }
    CHECK(lockKept); CHECK(! rejAgain);
    p.applySuggestionsToSequence();
    auto pat = p.pattern(); CHECK(pat && pat->events.size() >= 2 && pat->cfg.bars == 4);
    for (auto& e : pat->events) CHECK(e.frozen && std::abs(std::fmod(e.position, 0.25)) < 1e-9);
    // WAV import path
    juce::TemporaryFile tf(".wav"); juce::AudioBuffer<float> wb(1, N); for (int i = 0; i < N; ++i) wb.setSample(0, i, x[(size_t) i]);
    CHECK(Exporter::writeWav(tf.getFile(), wb, sr, 16, true));
    const auto prev = p.analysis(); p.importAudioFile(tf.getFile());
    CHECK(waitFor([&] { auto c = p.analysis(); return c && c != prev && ! p.busy(); }, 60000));
    CHECK(p.analysis()->valid && p.analysis()->bars == 4);
}

DTEST(concurrent_edits_while_audio_runs)
{
    setupEnv();
    CandyProcessor p(false); setP(p, pid::seqFreeRun, 1); setP(p, pid::seqMode, 3); prepare(p, 48000.0, 256);
    std::atomic<bool> stop { false }; std::atomic<int> bad { 0 }; std::atomic<long> blocks { 0 };
    std::thread audio([&]
    {
        juce::AudioBuffer<float> buf(2, 256); juce::MidiBuffer midi; int n = 0;
        while (! stop) { buf.clear(); midi.clear(); if (n++ % 5 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + n % 20, (juce::uint8) 100), 3);
                         p.processBlock(buf, midi);
                         for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample(c, i); if (! std::isfinite(v) || std::abs(v) > 4.0f) ++bad; }
                         ++blocks; std::this_thread::sleep_for(std::chrono::microseconds(500)); }
    });
    juce::Random rnd(9);
    for (int i = 0; i < 60; ++i)
    {
        p.editMain([&](CandyDNA& d) { d.layers[0].p[(size_t) rnd.nextInt(16)] = rnd.nextFloat(); d.layers[0].pitch = 50.0f + rnd.nextFloat() * 30.0f; }, i % 3 == 0);
        setP(p, pid::seqMode, (float) rnd.nextInt(8)); setP(p, pid::seqBars, (float) (1 + rnd.nextInt(16))); setP(p, pid::complexity, rnd.nextFloat() * 100.0f);
        if (i % 7 == 0) p.regeneratePattern(true);
        if (i % 11 == 0) { Preset pr; if (p.presets().load(rnd.nextInt((int) p.presets().entries().size()), pr)) p.loadPreset(pr); }
        if (i % 13 == 0) { juce::MemoryBlock mb; p.getStateInformation(mb); p.setStateInformation(mb.getData(), (int) mb.getSize()); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(15);
    }
    stop = true; audio.join();
    CHECK(bad.load() == 0); CHECK(blocks.load() > 50);
}

DTEST(export_through_processor)
{
    setupEnv();
    CandyProcessor p(false); prepare(p, 48000.0, 512);
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dc_proc_export"); dir.deleteRecursively(); dir.createDirectory();
    ExportFormat f; f.sampleRate = 48000.0; f.bitDepth = 24; f.quality = QualityMode::Standard;
    p.exportAsync(CandyProcessor::ExportWhat::OneShot, dir.getChildFile("one.wav"), f); CHECK(waitFor([&] { return dir.getChildFile("one.wav").existsAsFile() && ! p.busy(); }, 60000));
    p.exportAsync(CandyProcessor::ExportWhat::Loop, dir.getChildFile("loop.wav"), f); CHECK(waitFor([&] { return dir.getChildFile("loop.wav").existsAsFile() && ! p.busy(); }, 60000));
    p.exportAsync(CandyProcessor::ExportWhat::Midi, dir.getChildFile("p.mid"), f); CHECK(waitFor([&] { return dir.getChildFile("p.mid").existsAsFile() && ! p.busy(); }, 60000));
    p.exportAsync(CandyProcessor::ExportWhat::DryWet, dir, f); CHECK(waitFor([&] { return ! p.busy() && dir.findChildFiles(juce::File::findFiles, false, "*_fx.wav").size() == 1; }, 60000));
    CHECK(p.lastExportMessage().isNotEmpty());
    auto drag = p.makeDragFile(CandyProcessor::ExportWhat::OneShot, f); CHECK(drag.existsAsFile() && drag.hasFileExtension("wav"));
    auto dragLoop = p.makeDragFile(CandyProcessor::ExportWhat::Loop, f); CHECK(dragLoop.existsAsFile());
    dir.deleteRecursively();
}

DTEST(processor_cpu_load_is_reasonable)
{
    setupEnv();
    CandyProcessor p(true); setP(p, pid::seqMode, 3); setP(p, pid::density, 100); setP(p, pid::seqDiv, 3); setP(p, pid::seqProb, 100); setP(p, pid::seqFreeRun, 1); setP(p, pid::negSpace, 0);
    prepare(p, 48000.0, 512); p.regeneratePattern(true);
    juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; juce::Random rnd(1);
    double worstLoad = 0, sum = 0; int count = 0;
    for (int blk = 0; blk < 800; ++blk)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample(c, i, rnd.nextFloat() * 0.2f - 0.1f);
        const auto t0 = juce::Time::getHighResolutionTicks(); p.processBlock(buf, midi);
        const double load = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) / (512.0 / 48000.0);
        worstLoad = std::max(worstLoad, load); sum += load; ++count;
    }
    std::printf("    processBlock load at 48k/512: mean %.1f%%, worst %.1f%% (single core, dense pattern, High quality off)\n", 100.0 * sum / count, 100.0 * worstLoad);
    CHECK(sum / count < 0.35);
}
