#include "Exporter.h"
#include "Core/Rng.h"
#include "Presets/Preset.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace dali
{
juce::String ExportResult::summary() const
{
    if (! ok) return "Export failed: " + error;
    return juce::String(files.size()) + " file(s) written, peak " + juce::String(peakDb, 1) + " dBFS";
}

bool Exporter::writeWav(const juce::File& dest, const juce::AudioBuffer<float>& src, double sr, int bitDepth, bool dither, juce::String* err)
{
    auto fail = [&](const juce::String& m) { if (err) *err = m; return false; };
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) return fail("Unsupported bit depth");
    if (src.getNumSamples() <= 0) return fail("Nothing to write");
    if (! dest.getParentDirectory().createDirectory().wasOk()) return fail("Cannot create " + dest.getParentDirectory().getFullPathName());
    juce::AudioBuffer<float> b(2, src.getNumSamples());
    for (int c = 0; c < 2; ++c) b.copyFrom(c, 0, src, juce::jmin(c, src.getNumChannels() - 1), 0, src.getNumSamples());
    // integer formats cannot exceed full scale: scale down instead of clipping
    float pk = 0; for (int c = 0; c < 2; ++c) pk = juce::jmax(pk, b.getMagnitude(c, 0, b.getNumSamples()));
    if (bitDepth != 32 && pk > 0.9999f) b.applyGain(0.9999f / pk);
    if (bitDepth == 16 && dither)
    {   // deterministic TPDF dither (+-1 LSB)
        Rng r(0xD17E5u); const float lsb = 1.0f / 32768.0f;
        for (int c = 0; c < 2; ++c) { auto* p = b.getWritePointer(c); for (int i = 0; i < b.getNumSamples(); ++i) p[i] += (r.uniform() - r.uniform()) * lsb; }
        for (int c = 0; c < 2; ++c) { auto* p = b.getWritePointer(c); for (int i = 0; i < b.getNumSamples(); ++i) p[i] = juce::jlimit(-0.99997f, 0.99997f, p[i]); }
    }
    dest.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(dest);
    if (stream->failedToOpen()) return fail("Cannot open " + dest.getFullPathName() + " for writing");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(stream.get(), sr, 2, bitDepth, {}, 0));
    if (w == nullptr) return fail("Could not create the WAV writer");
    stream.release();                                    // owned by the writer now
    if (! w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples())) return fail("Write error (disk full?)");
    w.reset();
    return dest.existsAsFile() && dest.getSize() > 44;
}

std::vector<OfflineRenderer::Ev> Exporter::eventsFrom(const SeqPattern& p, double bpm)
{
    std::vector<OfflineRenderer::Ev> ev;
    for (size_t i = 0; i < p.events.size(); ++i)
    {
        const auto& e = p.events[i];
        if (! SeqPlayer::eventFires(p, i, 0)) continue;
        const double beat = e.position + (double) e.microMs * 0.001 * bpm / 60.0;
        ev.push_back({ std::max(0.0, beat), e.velocity, e.slot, e.pitchOffset, e.lengthBeats / std::max(1.0e-3f, p.cfg.lengthBeats), e.modSeed });
    }
    return ev;
}

static juce::AudioBuffer<float> renderOnce(OfflineRenderer& r, const ExportSource& s, ExportKind kind, bool dry, int solo, const ExportFormat& f, const std::function<bool()>& cancelled)
{
    RenderOptions o; o.sampleRate = f.sampleRate; o.quality = f.quality; o.blockSize = 512; o.velocity = f.velocity; o.tailMs = f.tailMs;
    o.dry = dry; o.soloLayer = solo; o.bpm = s.bpm; o.trimSilence = (kind == ExportKind::OneShot) && f.trimSilence;
    if (kind == ExportKind::OneShot)
    {
        if (s.slots[0] == nullptr) return {};
        return r.renderOneShot(*s.slots[0], o);
    }
    if (s.pattern == nullptr) return {};
    auto ev = Exporter::eventsFrom(*s.pattern, s.bpm);
    return r.renderSequence(s.slots, ev, s.pattern->lengthBeats, o, kind == ExportKind::Loop, cancelled);
}

juce::AudioBuffer<float> Exporter::render(const ExportSource& s, ExportKind kind, ExportMix mix, int solo, const ExportFormat& f, const std::function<bool()>& cancelled)
{
    if (mix == ExportMix::FxOnly)
    {
        auto full = renderOnce(renderer_, s, kind, false, solo, f, cancelled), dry = renderOnce(renderer_, s, kind, true, solo, f, cancelled);
        if (full.getNumSamples() == 0) return full;
        for (int c = 0; c < full.getNumChannels(); ++c)
        {
            auto* a = full.getWritePointer(c); const auto* d = dry.getNumChannels() > c ? dry.getReadPointer(c) : nullptr;
            const int n = juce::jmin(full.getNumSamples(), dry.getNumSamples());
            if (d) for (int i = 0; i < n; ++i) a[i] -= d[i];
        }
        return full;
    }
    return renderOnce(renderer_, s, kind, mix == ExportMix::Dry, solo, f, cancelled);
}

static float finishBuffer(juce::AudioBuffer<float>& b, const ExportFormat& f)
{
    float pk = 0; for (int c = 0; c < b.getNumChannels(); ++c) pk = juce::jmax(pk, b.getMagnitude(c, 0, b.getNumSamples()));
    if (f.normalize && pk > 1.0e-6f) { b.applyGain(juce::Decibels::decibelsToGain(f.normalizeDb) / pk); pk = juce::Decibels::decibelsToGain(f.normalizeDb); }
    return juce::Decibels::gainToDecibels(pk, -120.0f);
}

ExportResult Exporter::exportFile(const juce::File& dest, const ExportSource& s, ExportKind kind, ExportMix mix, const ExportFormat& f, const std::function<bool()>& cancelled)
{
    ExportResult r;
    auto b = render(s, kind, mix, -1, f, cancelled);
    if (cancelled && cancelled()) { r.error = "Cancelled"; return r; }
    if (b.getNumSamples() == 0) { r.error = "Nothing to render (no sound or pattern)"; return r; }
    r.peakDb = finishBuffer(b, f);
    if (! writeWav(dest, b, f.sampleRate, f.bitDepth, f.dither, &r.error)) return r;
    r.files.add(dest); r.ok = true; return r;
}

ExportResult Exporter::exportLayers(const juce::File& dir, const juce::String& base, const ExportSource& s, ExportKind kind, const ExportFormat& f, const std::function<bool()>& cancelled)
{
    ExportResult r;
    if (s.slots[0] == nullptr) { r.error = "No sound"; return r; }
    float peak = -120.0f;
    for (int li = 0; li < kMaxLayers; ++li)
    {
        const auto& l = s.slots[0]->layers[(size_t) li]; if (! l.active) continue;
        auto b = render(s, kind, ExportMix::Full, li, f, cancelled);
        if (cancelled && cancelled()) { r.error = "Cancelled"; return r; }
        if (b.getNumSamples() == 0) continue;
        // stems keep their relative level (no per-stem normalisation)
        ExportFormat nf = f; nf.normalize = false; peak = juce::jmax(peak, finishBuffer(b, nf));
        const auto file = dir.getChildFile(sanitizeFileName(base) + "_L" + juce::String(li + 1) + "_" + engineName(l.engine) + ".wav");
        if (! writeWav(file, b, f.sampleRate, f.bitDepth, f.dither, &r.error)) return r;
        r.files.add(file);
    }
    r.ok = r.files.size() > 0; if (! r.ok && r.error.isEmpty()) r.error = "No audible layers"; r.peakDb = peak; return r;
}

ExportResult Exporter::exportDryWet(const juce::File& dir, const juce::String& base, const ExportSource& s, ExportKind kind, const ExportFormat& f, const std::function<bool()>& cancelled)
{
    ExportResult r; float peak = -120.0f;
    const std::pair<ExportMix, const char*> mixes[] = { { ExportMix::Dry, "_dry" }, { ExportMix::Full, "_full" }, { ExportMix::FxOnly, "_fx" } };
    ExportFormat nf = f; nf.normalize = false;
    for (auto& [m, suffix] : mixes)
    {
        auto b = render(s, kind, m, -1, nf, cancelled);
        if (cancelled && cancelled()) { r.error = "Cancelled"; return r; }
        if (b.getNumSamples() == 0) { r.error = "Nothing to render"; return r; }
        peak = juce::jmax(peak, finishBuffer(b, nf));
        const auto file = dir.getChildFile(sanitizeFileName(base) + suffix + ".wav");
        if (! writeWav(file, b, f.sampleRate, f.bitDepth, f.dither, &r.error)) return r;
        r.files.add(file);
    }
    r.ok = true; r.peakDb = peak; return r;
}

ExportResult Exporter::exportBatch(const juce::File& dir, const std::vector<CandyDNA>& sounds, const ExportFormat& f, const std::function<bool()>& cancelled, const std::function<void(float)>& progress)
{
    ExportResult r; float peak = -120.0f;
    for (size_t i = 0; i < sounds.size(); ++i)
    {
        if (cancelled && cancelled()) { r.error = "Cancelled"; return r; }
        ExportSource s; s.slots[0] = &sounds[i];
        auto b = render(s, ExportKind::OneShot, ExportMix::Full, -1, f, cancelled);
        if (b.getNumSamples() == 0) continue;
        peak = juce::jmax(peak, finishBuffer(b, f));
        const auto file = dir.getChildFile(juce::String(i + 1).paddedLeft('0', 3) + "_" + sanitizeFileName(sounds[i].name) + ".wav");
        if (! writeWav(file, b, f.sampleRate, f.bitDepth, f.dither, &r.error)) return r;
        r.files.add(file);
        if (progress) progress((float) (i + 1) / (float) sounds.size());
    }
    r.ok = r.files.size() > 0; if (! r.ok && r.error.isEmpty()) r.error = "Nothing to export"; r.peakDb = peak; return r;
}

ExportResult Exporter::exportMidi(const juce::File& dest, const ExportSource& s, bool includeMuted)
{
    ExportResult r;
    if (s.pattern == nullptr || s.pattern->events.empty()) { r.error = "No pattern events to export"; return r; }
    constexpr int ppq = 960;
    juce::MidiMessageSequence track;
    track.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60000000.0 / juce::jlimit(20.0, 400.0, s.bpm))), 0);
    track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    track.addEvent(juce::MidiMessage::textMetaEvent(3, "DaliCandy"), 0);
    const auto& p = *s.pattern;
    for (size_t i = 0; i < p.events.size(); ++i)
    {
        const auto& e = p.events[i];
        if (e.muted && ! includeMuted) continue;
        if (! includeMuted && ! SeqPlayer::eventFires(p, i, 0)) continue;
        const CandyDNA* d = s.slots[(size_t) std::clamp(e.slot, 0, kNumSoundSlots - 1)]; if (d == nullptr) d = s.slots[0];
        const float base = d != nullptr ? d->layers[0].pitch : 60.0f;
        const int note = juce::jlimit(0, 127, (int) std::lround(base + e.pitchOffset));
        const double t0 = e.position * ppq + (double) e.microMs * 0.001 * s.bpm / 60.0 * ppq;
        const double t1 = t0 + std::max(0.02, (double) e.lengthBeats) * ppq;
        const auto vel = (juce::uint8) juce::jlimit(1, 127, (int) std::lround(e.velocity * 127.0f));
        track.addEvent(juce::MidiMessage::noteOn(1, note, vel), std::max(0.0, t0));
        track.addEvent(juce::MidiMessage::noteOff(1, note, (juce::uint8) 0), std::max(1.0, t1));
    }
    track.updateMatchedPairs();
    track.addEvent(juce::MidiMessage::endOfTrack(), p.lengthBeats * ppq);
    juce::MidiFile mf; mf.setTicksPerQuarterNote(ppq); mf.addTrack(track);
    dest.getParentDirectory().createDirectory(); dest.deleteFile();
    juce::FileOutputStream out(dest);
    if (out.failedToOpen() || ! mf.writeTo(out)) { r.error = "Could not write " + dest.getFullPathName(); return r; }
    out.flush(); r.files.add(dest); r.ok = true; return r;
}

// ------------------------------------------------------------------------------------------------------------
TempExportFiles::TempExportFiles()
{
    purgeStale();
    dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("DaliCandy_drag").getChildFile(juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    dir_.createDirectory();
}
TempExportFiles::~TempExportFiles() { dir_.deleteRecursively(); }
void TempExportFiles::purgeStale()
{
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("DaliCandy_drag");
    if (! root.isDirectory()) return;
    for (auto& d : root.findChildFiles(juce::File::findDirectories, false))
        if (d.getLastModificationTime() < juce::Time::getCurrentTime() - juce::RelativeTime::days(1)) d.deleteRecursively();
}
juce::File TempExportFiles::create(const juce::String& base, const juce::AudioBuffer<float>& b, double sr, int bitDepth, juce::String* err)
{
    const auto f = dir_.getChildFile(sanitizeFileName(base) + (counter_++ ? "_" + juce::String(counter_) : juce::String()) + ".wav");
    return Exporter::writeWav(f, b, sr, bitDepth, true, err) ? f : juce::File();
}
juce::File TempExportFiles::createMidi(const juce::String& base, const ExportSource& s)
{
    const auto f = dir_.getChildFile(sanitizeFileName(base) + (counter_++ ? "_" + juce::String(counter_) : juce::String()) + ".mid");
    return Exporter::exportMidi(f, s).ok ? f : juce::File();
}
} // namespace dali
