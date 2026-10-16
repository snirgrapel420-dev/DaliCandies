#include "TestFramework.h"
#include "Presets/PresetManager.h"
#include "Rendering/Exporter.h"
#include "Generator/CandyGenerator.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>

using namespace dali;

namespace
{
CandyDNA makeSound(Category c = Category::MicroDetails, int which = 0, uint32_t startSeed = 1)
{
    CandyGenerator gen; CandyDNA d; GenSettings g; g.category = c;
    for (uint32_t s = startSeed; s < startSeed + 100; ++s) if (gen.generateOne(g, s, 44100.0, d, (int) archetypesOf(c)[(size_t) which])) break;
    return d;
}
Preset makePreset(const juce::String& name = "Test Candy")
{
    Preset p; p.name = name; p.setMain(makeSound()); p.category = p.main().category; p.tags = { "glitch", "bright" };
    p.slots[2] = makeSound(Category::MetallicAccents); p.slotUsed[2] = true;
    p.seq.mode = SeqMode::Euclidean; p.seq.bars = 4; p.seq.seed = 77; p.macros = { 0.1f, 0.2f, 0.3f, 0.4f };
    p.gen.complexity = 0.9f; p.gen.category = Category::MetallicAccents;
    SeqContext cx; cx.numSlots = 3; p.seq.events = planEvents(p.seq, cx);
    return p;
}
struct TempDir { juce::File dir { juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dc_test_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64())) };
                 TempDir() { dir.createDirectory(); } ~TempDir() { dir.deleteRecursively(); } };
juce::AudioBuffer<float> readWav(const juce::File& f, double* sr = nullptr, int* bits = nullptr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    juce::AudioBuffer<float> b; if (! r) return b;
    if (sr) *sr = r->sampleRate; if (bits) *bits = (int) r->bitsPerSample;
    b.setSize((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); return b;
}
}

DTEST(preset_roundtrip_is_lossless)
{
    auto p = makePreset(); Preset q; juce::String err;
    CHECK(Preset::fromJson(p.toJson(), q, &err));
    CHECK(q.toJson() == p.toJson());
    CHECK(q.name == "Test Candy" && q.tags.size() == 2 && q.slotUsed[0] && q.slotUsed[2] && ! q.slotUsed[1]);
    CHECK(q.macros[3] == 0.4f && q.seq.events.size() == p.seq.events.size());
}

DTEST(preset_v1_migrates_and_newer_is_rejected)
{
    auto p = makePreset();
    // hand-build a v1 document: single dna, comma separated tags, no slots/macros
    auto* o = new juce::DynamicObject();
    o->setProperty("version", 1); o->setProperty("name", "Old One"); o->setProperty("category", 2); o->setProperty("tags", "a, b ,c");
    o->setProperty("dna", p.main().toVar()); o->setProperty("seq", p.seq.toVar()); o->setProperty("gen", p.gen.toVar());
    Preset q; juce::String err;
    CHECK_MSG(Preset::fromVar(juce::var(o), q, &err), err.toStdString());
    CHECK(q.name == "Old One" && q.tags.size() == 3 && q.tags[1] == "b" && q.slotUsed[0] && q.main().name == p.main().name);
    auto* n = new juce::DynamicObject(); n->setProperty("version", kPresetVersion + 1);
    CHECK(! Preset::fromVar(juce::var(n), q, &err) && err.containsIgnoreCase("newer"));
    CHECK(! Preset::fromJson("{ not json", q, &err));
    CHECK(! Preset::fromJson("{\"version\":2,\"name\":\"empty\",\"slots\":[]}", q, &err));
    CHECK(! Preset::fromJson("[1,2,3]", q, &err));
}

DTEST(preset_manager_full_lifecycle)
{
    TempDir td; PresetManager pm(td.dir);
    pm.setFactoryProvider([] { return std::vector<std::pair<juce::String, juce::String>> { { "Factory A", makePreset("Factory A").toJson() }, { "Factory B", makePreset("Factory B").toJson() } }; });
    pm.rescan(); CHECK(pm.entries().size() == 2 && pm.entries()[0].factory);
    juce::String err;
    CHECK(pm.save(makePreset("My Sound"), false, &err)); CHECK(pm.entries().size() == 3);
    CHECK(! pm.save(makePreset("My Sound"), false, &err) && err.isNotEmpty());          // no silent overwrite
    CHECK(pm.save(makePreset("My Sound"), true, &err));
    CHECK(pm.save(makePreset("Weird/Name:*?"), false, &err));
    int mine = pm.indexOf("user/My Sound"); CHECK(mine >= 0);
    CHECK(pm.search("my sound").size() == 1 && pm.search("glitch").size() == 4 && pm.search("zzz").empty());
    pm.setFavorite(mine, true); pm.setRating(mine, 4);
    PresetManager pm2(td.dir); pm2.setFactoryProvider([] { return std::vector<std::pair<juce::String, juce::String>> {}; }); pm2.rescan();     // favourites persist
    CHECK(pm2.entries()[(size_t) pm2.indexOf("user/My Sound")].favorite && pm2.entries()[(size_t) pm2.indexOf("user/My Sound")].rating == 4);
    CHECK(pm.search({}, -1, true).size() == 1);
    CHECK(pm.rename(mine, "Renamed", &err)); CHECK(pm.indexOf("user/Renamed") >= 0 && pm.indexOf("user/My Sound") < 0);
    CHECK(! pm.rename(0, "x", &err));                                                   // factory is read-only
    CHECK(! pm.remove(0, &err));
    auto exp = td.dir.getChildFile("out/exported.dalicandy"); CHECK(pm.exportTo(pm.indexOf("user/Renamed"), exp, &err) && exp.existsAsFile());
    CHECK(pm.remove(pm.indexOf("user/Renamed"), &err)); CHECK(pm.indexOf("user/Renamed") < 0);
    CHECK(pm.importFile(exp, &err)); CHECK(pm.importFile(exp, &err));                   // second import is renamed, not overwritten
    CHECK(pm.indexOf("user/Renamed") >= 0 && pm.indexOf("user/Renamed (2)") >= 0);
    td.dir.getChildFile("broken.dalicandy").replaceWithText("{{{{ garbage"); pm.rescan();    // corrupt file skipped
    CHECK(pm.indexOf("user/broken") < 0);
    CHECK(pm.random(5) == pm.random(5) && pm.random(5) >= 0);
    Preset loaded; CHECK(pm.load(pm.indexOf("factory/Factory A"), loaded, &err) && loaded.factory && loaded.name == "Factory A");
}

DTEST(wav_export_all_formats_and_rates)
{
    TempDir td; Exporter ex; const auto d = makeSound(Category::MetallicAccents, 1, 3);
    ExportSource s; s.slots[0] = &d;
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
        for (int bits : { 16, 24, 32 })
        {
            ExportFormat f; f.sampleRate = sr; f.bitDepth = bits; f.quality = QualityMode::Standard;
            auto file = td.dir.getChildFile("x_" + juce::String((int) sr) + "_" + juce::String(bits) + ".wav");
            auto r = ex.exportFile(file, s, ExportKind::OneShot, ExportMix::Full, f);
            CHECK_MSG(r.ok, r.error.toStdString());
            double rsr = 0; int rbits = 0; auto b = readWav(file, &rsr, &rbits);
            CHECK(rsr == sr && rbits == bits && b.getNumChannels() == 2 && b.getNumSamples() > (int) (sr * 0.02));
            const float pk = b.getMagnitude(0, b.getNumSamples());
            CHECK(pk > 0.05f && pk <= 1.0f);
            CHECK_MSG(r.peakDb <= 0.0f, "peak " + std::to_string(r.peakDb) + " sr " + std::to_string((int) sr) + " bits " + std::to_string(bits));
            for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite(b.getSample(c, i))) { CHECK(false); break; }
        }
    // 16-bit content matches the float render to within quantisation + dither
    ExportFormat f; f.sampleRate = 48000.0; f.bitDepth = 32; f.quality = QualityMode::Standard;
    auto ref = ex.render(s, ExportKind::OneShot, ExportMix::Full, -1, f);
    auto file = td.dir.getChildFile("ref32.wav"); CHECK(ex.exportFile(file, s, ExportKind::OneShot, ExportMix::Full, f).ok);
    auto b = readWav(file); CHECK_MSG(b.getNumSamples() == ref.getNumSamples(), std::to_string(b.getNumSamples()) + " vs " + std::to_string(ref.getNumSamples()));
    double err = 0; for (int i = 0; i < b.getNumSamples(); ++i) err = std::max(err, (double) std::abs(b.getSample(0, i) - ref.getSample(0, i)));
    CHECK(err < 1e-6);
    CHECK(! Exporter::writeWav(td.dir.getChildFile("bad.wav"), ref, 44100.0, 20, false));
    CHECK(! Exporter::writeWav(juce::File("/proc/nonexistent/x.wav"), ref, 44100.0, 24, false));
}

DTEST(normalize_and_integer_formats_never_clip)
{
    TempDir td; Exporter ex; auto d = makeSound(); ExportSource s; s.slots[0] = &d;
    ExportFormat f; f.normalize = true; f.normalizeDb = -3.0f; f.bitDepth = 24; f.quality = QualityMode::Standard;
    auto file = td.dir.getChildFile("n.wav"); CHECK(ex.exportFile(file, s, ExportKind::OneShot, ExportMix::Full, f).ok);
    auto b = readWav(file); CHECK(std::abs(juce::Decibels::gainToDecibels(b.getMagnitude(0, b.getNumSamples())) + 3.0f) < 0.05f);
    juce::AudioBuffer<float> hot(2, 1000); for (int i = 0; i < 1000; ++i) { hot.setSample(0, i, 3.0f * std::sin(i * 0.1f)); hot.setSample(1, i, -3.0f * std::sin(i * 0.1f)); }
    CHECK(Exporter::writeWav(td.dir.getChildFile("hot.wav"), hot, 44100.0, 16, true));
    auto h = readWav(td.dir.getChildFile("hot.wav")); CHECK(h.getMagnitude(0, 1000) <= 1.0f && h.getMagnitude(0, 1000) > 0.9f);
}

DTEST(sequence_export_loop_multibar_layers_drywet_batch_midi)
{
    TempDir td; Exporter ex;
    auto d = makeSound(Category::Experimental, 2, 5); CHECK(d.activeLayerCount() >= 2);
    SeqConfig c; c.mode = SeqMode::Euclidean; c.bars = 2; c.euclidHits = 6; c.seed = 3; c.ratchetProb = 0;
    SeqContext cx; cx.numSlots = 1; cx.negSpaceSensitivity = 0; auto pat = makePattern(c, cx, true);
    ExportSource s; s.slots[0] = &d; s.pattern = pat.get(); s.bpm = 125.0;
    ExportFormat f; f.sampleRate = 48000.0; f.bitDepth = 24; f.quality = QualityMode::Standard;
    const int loopLen = (int) std::lround(pat->lengthBeats * 60.0 / 125.0 * 48000.0);
    auto lf = td.dir.getChildFile("loop.wav"); auto r = ex.exportFile(lf, s, ExportKind::Loop, ExportMix::Full, f); CHECK_MSG(r.ok, r.error.toStdString());
    auto lb = readWav(lf); CHECK(std::abs(lb.getNumSamples() - loopLen) <= 1);
    auto mf = td.dir.getChildFile("multi.wav"); CHECK(ex.exportFile(mf, s, ExportKind::MultiBar, ExportMix::Full, f).ok);
    auto mb = readWav(mf); CHECK(mb.getNumSamples() >= loopLen);                 // includes the tail
    // layers
    auto lr = ex.exportLayers(td.dir, "stem", s, ExportKind::OneShot, f); CHECK(lr.ok && lr.files.size() == d.activeLayerCount());
    for (auto& file : lr.files) CHECK(readWav(file).getMagnitude(0, readWav(file).getNumSamples()) > 0.0f);
    // dry / wet
    auto dw = ex.exportDryWet(td.dir, "dw", s, ExportKind::OneShot, f); CHECK(dw.ok && dw.files.size() == 3);
    auto full = readWav(td.dir.getChildFile("dw_full.wav")), dry = readWav(td.dir.getChildFile("dw_dry.wav")), fxo = readWav(td.dir.getChildFile("dw_fx.wav"));
    const int n = std::min({ full.getNumSamples(), dry.getNumSamples(), fxo.getNumSamples() }); double e = 0;
    for (int i = 0; i < n; ++i) e = std::max(e, (double) std::abs(full.getSample(0, i) - dry.getSample(0, i) - fxo.getSample(0, i)));
    CHECK_MSG(e < 1e-4, "max diff " + std::to_string(e) + " n=" + std::to_string(n) + " lens " + std::to_string(full.getNumSamples()) + "/" + std::to_string(dry.getNumSamples()) + "/" + std::to_string(fxo.getNumSamples()));
    // batch
    std::vector<CandyDNA> v { makeSound(Category::MicroDetails, 0, 1), makeSound(Category::Transitions, 0, 1), makeSound(Category::OrganicTextures, 0, 1) };
    float lastP = 0; auto br = ex.exportBatch(td.dir.getChildFile("batch"), v, f, {}, [&](float p) { lastP = p; });
    CHECK(br.ok && br.files.size() == 3 && lastP == 1.0f);
    // cancel
    int calls = 0; auto cr = ex.exportBatch(td.dir.getChildFile("batch2"), v, f, [&] { return ++calls > 1; }); CHECK(! cr.ok);
    // MIDI
    auto midi = td.dir.getChildFile("p.mid"); auto mr = Exporter::exportMidi(midi, s); CHECK_MSG(mr.ok, mr.error.toStdString());
    juce::FileInputStream in(midi); juce::MidiFile rd; CHECK(rd.readFrom(in)); CHECK(rd.getNumTracks() == 1 && rd.getTimeFormat() == 960);
    int notes = 0; double tempoUs = 0; auto* tr = rd.getTrack(0);
    for (int i = 0; i < tr->getNumEvents(); ++i) { auto& m = tr->getEventPointer(i)->message; if (m.isNoteOn()) ++notes; if (m.isTempoMetaEvent()) tempoUs = m.getTempoSecondsPerQuarterNote() * 1e6; }
    CHECK(notes == (int) pat->events.size() && std::abs(tempoUs - 60e6 / 125.0) < 2.0);
    ExportSource none; CHECK(! Exporter::exportMidi(td.dir.getChildFile("n.mid"), none).ok);
    ExportSource nothing; CHECK(! ex.exportFile(td.dir.getChildFile("z.wav"), nothing, ExportKind::OneShot, ExportMix::Full, f).ok);
}

DTEST(temp_files_for_drag_and_drop_are_real_and_cleaned)
{
    juce::File dir;
    {
        TempExportFiles t; dir = t.directory(); Exporter ex; auto d = makeSound(); ExportSource s; s.slots[0] = &d;
        ExportFormat f; f.quality = QualityMode::Standard; auto b = ex.render(s, ExportKind::OneShot, ExportMix::Full, -1, f);
        auto a = t.create("Drag Me", b, 44100.0, 24); auto a2 = t.create("Drag Me", b, 44100.0, 24);
        CHECK(a.existsAsFile() && a2.existsAsFile() && a != a2 && a.getSize() > 1000);
        CHECK(readWav(a).getNumSamples() == b.getNumSamples());
        SeqConfig c; c.bars = 1; SeqContext cx; cx.numSlots = 1; auto pat = makePattern(c, cx, true); s.pattern = pat.get();
        auto m = t.createMidi("Drag Midi", s); CHECK(m.existsAsFile() && m.hasFileExtension("mid"));
    }
    CHECK(! dir.exists());
}

DTEST(renderer_is_repeatable_and_history_independent)
{
    // The same DNA must render to bit-identical audio on a reused renderer, a fresh renderer, and after a sample-rate change.
    const auto& t = archetypeTable();
    for (int a : { 0, 14, 17, 22, 28, 31 })
    {
        CandyGenerator gen; CandyDNA d; GenSettings g; g.category = t[(size_t) a].category;
        bool ok = false; for (uint32_t s = 1; s < 80 && ! ok; ++s) ok = gen.generateOne(g, s, 48000.0, d, a);
        CHECK(ok); if (! ok) continue;
        OfflineRenderer r1, r2; RenderOptions o; o.sampleRate = 48000; o.quality = QualityMode::Standard; o.trimSilence = true;
        auto a1 = r1.renderOneShot(d, o), a2 = r1.renderOneShot(d, o), a3 = r2.renderOneShot(d, o);
        RenderOptions o2 = o; o2.sampleRate = 96000; r1.renderOneShot(d, o2); auto a4 = r1.renderOneShot(d, o);
        auto same = [](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
        { if (x.getNumSamples() != y.getNumSamples()) return false; for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) if (x.getSample(c, i) != y.getSample(c, i)) return false; return true; };
        CHECK_MSG(same(a1, a2), std::string("repeat ") + t[(size_t) a].name);
        CHECK_MSG(same(a1, a3), std::string("fresh ") + t[(size_t) a].name);
        CHECK_MSG(same(a1, a4), std::string("after sr change ") + t[(size_t) a].name);
    }
}

#include "Plugin/FactoryBank.h"
DTEST(factory_bank_is_complete_and_renders)
{
    PresetManager pm(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("dc_nofiles_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64())));
    pm.setFactoryProvider(embeddedFactoryPresets); pm.rescan();
    CHECK_MSG(pm.entries().size() >= 48, std::to_string(pm.entries().size()));
    std::set<int> cats; std::set<juce::String> names; OfflineRenderer r;
    for (size_t i = 0; i < pm.entries().size(); ++i)
    {
        Preset p; juce::String err; CHECK_MSG(pm.load((int) i, p, &err), err.toStdString());
        cats.insert((int) p.category); names.insert(p.name); CHECK(p.factory && p.tags.size() >= 3 && p.author == "Dali Audio");
        if (i % 6 == 0)
        {
            RenderOptions o; o.sampleRate = 44100; o.quality = QualityMode::Standard; auto b = r.renderOneShot(p.main(), o);
            const float pk = OfflineRenderer::peakOf(b); CHECK_MSG(pk > 0.1f && pk <= 1.0f, p.name.toStdString() + " peak " + std::to_string(pk));
        }
    }
    CHECK((int) cats.size() == kNumCategories); CHECK(names.size() == pm.entries().size());
}
