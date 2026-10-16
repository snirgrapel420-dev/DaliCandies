#pragma once
// Audio / MIDI export.  Everything renders offline at the *requested* sample rate (no resampling), so
// 44.1 / 48 / 88.2 / 96 kHz files contain natively rendered audio.  Message / worker thread only.
#include "OfflineRenderer.h"
#include "Sequencer/Sequencer.h"

namespace dali
{
enum class ExportKind { OneShot = 0, Loop, MultiBar };
enum class ExportMix  { Full = 0, Dry, FxOnly };       // FxOnly = Full - Dry (sample difference; approximate when the output protector engages)

struct ExportFormat
{
    double sampleRate = 44100.0;
    int    bitDepth = 24;                  // 16, 24 or 32 (float)
    bool   dither = true;                  // TPDF, 16 bit only
    bool   normalize = false;              // scale to normalizeDb peak
    float  normalizeDb = -1.0f;
    bool   trimSilence = true;             // one-shots only
    QualityMode quality = QualityMode::High;
    float  velocity = 0.9f;
    float  tailMs = -1.0f;                 // < 0 automatic
    float  fitMs = 0.0f;                   // > 0: one-shots are cut / padded to exactly this length (tempo-synced dishes)
};

struct ExportSource
{
    std::array<const CandyDNA*, kNumSoundSlots> slots {};     // slot 0 is the main sound for one-shots
    const SeqPattern* pattern = nullptr;                      // needed for Loop / MultiBar / MIDI
    double bpm = 120.0;
};

struct ExportResult
{
    bool ok = false; juce::String error; juce::Array<juce::File> files; float peakDb = -120.0f;
    juce::String summary() const;
};

class Exporter
{
public:
    // Renders to memory (also used for drag and drop and the audition buffer).
    juce::AudioBuffer<float> render(const ExportSource& s, ExportKind kind, ExportMix mix, int soloLayer, const ExportFormat& f,
                                    const std::function<bool()>& cancelled = {});

    ExportResult exportFile(const juce::File& dest, const ExportSource& s, ExportKind kind, ExportMix mix, const ExportFormat& f,
                            const std::function<bool()>& cancelled = {});
    // one file per active layer of slot 0:  <base>_L1_<engine>.wav ...
    ExportResult exportLayers(const juce::File& dir, const juce::String& base, const ExportSource& s, ExportKind kind, const ExportFormat& f,
                              const std::function<bool()>& cancelled = {});
    // <base>_dry.wav, <base>_full.wav and <base>_fx.wav
    ExportResult exportDryWet(const juce::File& dir, const juce::String& base, const ExportSource& s, ExportKind kind, const ExportFormat& f,
                              const std::function<bool()>& cancelled = {});
    // each DNA rendered as a one-shot into dir/<index>_<name>.wav
    ExportResult exportBatch(const juce::File& dir, const std::vector<CandyDNA>& sounds, const ExportFormat& f,
                             const std::function<bool()>& cancelled = {}, const std::function<void(float)>& progress = {});
    // Standard MIDI file of the pattern (one loop pass; probabilistic events use loop index 0)
    static ExportResult exportMidi(const juce::File& dest, const ExportSource& s, bool includeMuted = false);

    static bool writeWav(const juce::File& dest, const juce::AudioBuffer<float>& b, double sr, int bitDepth, bool dither, juce::String* err = nullptr);
    static std::vector<OfflineRenderer::Ev> eventsFrom(const SeqPattern& p, double bpm);
private:
    OfflineRenderer renderer_;
};

// Real temporary files for drag-and-drop to a DAW / file manager.  Cleans up after itself.
class TempExportFiles
{
public:
    TempExportFiles();
    ~TempExportFiles();
    juce::File create(const juce::String& baseName, const juce::AudioBuffer<float>& buf, double sr, int bitDepth, juce::String* err = nullptr);
    juce::File createMidi(const juce::String& baseName, const ExportSource& s);
    const juce::File& directory() const { return dir_; }
    static void purgeStale();                       // removes leftovers of crashed sessions older than a day
private:
    juce::File dir_; int counter_ = 0;
};
} // namespace dali
