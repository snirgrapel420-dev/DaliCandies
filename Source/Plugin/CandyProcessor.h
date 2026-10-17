#pragma once
// DaliCandy audio processor shared by the Instrument (MIDI synth) and the FX (audio effect with analysis).
//
// Threading model
//   audio thread  : processBlock only.  Reads immutable published objects (sound bank, pattern, adaptive params,
//                   capture buffer) through atomic pointers, never allocates, never locks.
//   message thread: all model edits (Preset, parameters, taste) and publication of new immutable objects.
//   worker thread : generation, mutation, analysis, preview rendering, export.  Results come back through a
//                   mailbox that is drained on the message thread (AsyncUpdater).
#include "Params.h"
#include "FactoryBank.h"
#include "DSP/CandyEngine.h"
#include "Generator/CandyGenerator.h"
#include "Sequencer/Sequencer.h"
#include "Analysis/LiveAnalyzer.h"
#include "Analysis/Masking.h"
#include "Rendering/Exporter.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>

namespace dali
{
struct CandidateView
{
    CandyDNA dna;
    std::vector<float> wave;          // min/max pairs for the card thumbnail
};

struct CaptureBuffer
{
    std::vector<float> mono;
    int capacity = 0;
    std::atomic<int> written { 0 };
    std::atomic<int> state { 0 };     // 0 idle, 1 armed (waiting for the bar line), 2 recording, 3 finished
    double sampleRate = 44100.0, bpm = 120.0; int bars = 8; int beatsPerBar = 4;
};

enum ChangeFlag : uint32_t
{
    kChSound = 1, kChCandidates = 2, kChPattern = 4, kChAnalysis = 8, kChPreset = 16, kChStatus = 32, kChTaste = 64, kChParamsLoaded = 128
};

class CandyProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster,
                       private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    explicit CandyProcessor(bool isFx = DALI_IS_FX != 0);
    ~CandyProcessor() override;

    //== AudioProcessor ===========================================================
    const juce::String getName() const override { return isFx_ ? "DaliCandy" : "DaliCandy (instrument only)"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override { return 6.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return DALI_WITH_EDITOR != 0; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isFx() const { return isFx_; }          // true = unified plug-in (instrument + effect); false only for the instrument-only test/legacy build
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;                     // on-screen keyboard of the Kitchen (notes are merged into the host MIDI)

    //== model (message thread) ====================================================
    Preset  presetSnapshot() const;                       // copy of the current sound set + sequence
    CandyDNA mainSound() const;
    SeqConfig seqConfig() const;
    GenSettings genSettings() { return genSettingsFrom(apvts); }
    GenSettings kitchenSettings(int* gapBand = nullptr);       // generation settings for the Kitchen: families, spice, synced length, LISTEN steering
    double hostBpm() const { return hostBpm_.load(); }          // tempo of the host (Analysis BPM when the host gives none)
    double syncWindowMs() const;                                // length of the synced window in ms (0 when Length is Free)
    void editMain(const std::function<void(CandyDNA&)>& fn, bool rebuildNow = false);       // schedules a (coalesced) bank rebuild
    void setMainSound(const CandyDNA& d, bool toHistory = true);
    void setSlotSound(int slot, const CandyDNA* d);          // nullptr clears the slot
    void setMainSlot(int slot);
    int  mainSlot() const;
    void loadPreset(const Preset& p);
    void setPresetInfo(const juce::String& name, const juce::StringArray& tags, const juce::String& author);
    Preset  currentAsPreset();                              // includes the current parameter values
    PresetManager& presets() { return presets_; }
    TasteModel& taste() { return taste_; }
    CandyHistory& history() { return history_; }

    //== generation & DNA operations (async; results arrive via change messages) =======
    void generate();                                         // fresh dishes (or variations of the current one when Keep Skeleton is on)
    void roll();                                             // ignores the family choice: random family / fusion / settings, then picks one at random
    void rollWith(uint32_t seed);
    void listen();                                           // manual: analyse now                                           // analyse what the plug-in has been hearing (no arming); no audio -> asks for a WAV
    void listenFromFile(const juce::File& f);                // LISTEN with a bounced track: analyse, cook plates for its gaps, build the pattern
    int  heardBars() const { const double sp = 4.0 * 60.0 / std::max(20.0, ringBpm_.load()) * sr_; return sp > 0 ? (int) std::floor((double) std::min<int64_t>(ringWritten_.load(), (int64_t) ring_.size()) / sp) : 0; }
    bool takeListenNeedsFile() { return listenNeedsFile_.exchange(false); }
    float inputLevel() const { return inputLevel_.load(std::memory_order_relaxed); }   // 0..1 peak of the audio reaching the plug-in
    double heardSeconds() const { return sr_ > 0 ? (double) std::min<int64_t>(ringWritten_.load(), (int64_t) ring_.size()) / sr_ : 0.0; }
    void platesToPalette();                                  // the candidate plates become the sound slots the sequence plays from
    void buildPattern(bool newSeed);                         // context-aware pattern from the analysed track (rhythm-only plan when nothing is analysed)                                           // capture the track, analyse it, place events in its gaps and aim a new generation at them
    void cancelWork();
    bool busy() const { return busyJobs_.load() > 0; }
    float progress() const { return progress_.load(); }
    juce::String statusText() const;
    std::vector<CandidateView> candidates() const;
    void selectCandidate(int index);
    void candidateToSlot(int index, int slot);
    void mutateMain(float amount);
    void evolveMain(float amount);
    void breedMainWith(const CandyDNA& other);
    void regenerateLayer(int layer);
    void recall(size_t stepsBack);
    void rate(int signal);                                   // +1 like, +2 favourite, -1 dislike (applies to the main sound)
    void audition(int slot = 0, float velocity = 0.9f);      // plays the sound through the engine
    uint32_t seed() const { return genCounter_; }

    //== sequencer ===================================================================
    void editSeq(const std::function<void(SeqConfig&)>& fn);
    void regeneratePattern(bool newSeed);
    void evolvePattern();
    void toggleEventFrozen(size_t index);
    std::shared_ptr<const SeqPattern> pattern() const;
    double playheadBeats() const { return uiPlayhead_.load(); }
    bool   hostPlaying() const { return uiPlaying_.load(); }

    //== analysis ====================================================================
    void startCapture();                                     // uses the Capture Length parameter
    void cancelCapture();
    int  captureState() const;
    float captureProgress() const;
    void importAudioFile(const juce::File& f);
    std::shared_ptr<const TrackAnalysis> analysis() const;
    void editAnalysis(const std::function<void(TrackAnalysis&)>& fn);
    void regenerateSuggestions();
    void applySuggestionsToSequence(bool enable = true);
    MaskingReport masking() const;
    const LiveAnalyzer& live() const { return live_; }

    //== export ======================================================================
    enum class ExportWhat { OneShot, Loop, MultiBar, Layers, DryWet, Candidates, Midi };
    void exportAsync(ExportWhat what, const juce::File& target, const ExportFormat& fmt, ExportMix mix = ExportMix::Full);
    juce::File makeDragFile(ExportWhat what, const ExportFormat& fmt);        // renders synchronously into a temp file
    juce::String lastExportMessage() const;
    juce::AudioBuffer<float> renderPreview(const ExportFormat& fmt, ExportKind kind, ExportMix mix, int soloLayer);   // message thread helper

    //== meters ======================================================================
    float outputPeak() const { return outPeak_.load(); }
    float cpuLoad() const { return cpu_.load(); }
    int   activeVoices() const { return voices_.load(); }
    uint64_t protectCount() const { return protect_.load(); }
    void  resetStats();

    uint32_t takeUiFlags() { return uiFlags_.exchange(0); }   // ChangeFlag bits accumulated since the last call (editor polls this)

    juce::var uiState;                                       // persisted GUI state (page, filters...)

private:
    // ---- internals --------------------------------------------------------------
    struct MailItem { std::function<void()> apply; };
    void postToMessageThread(std::function<void()> fn);
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void parameterChanged(const juce::String& id, float v) override;
    void flagChange(uint32_t f) { pendingFlags_ |= f; }

    void rebuildBankNow();
    void scheduleBankRebuild();
    void rebuildPatternNow(bool replan);
    SeqContext seqContext() const;
    void pushHistory(const CandyDNA& d);
    void commitMain(const CandyDNA& d, bool toHistory);
    void syncParamsToPreset();
    void applyParamsFromPreset(const Preset& p);
    void beginAnalysis(std::vector<float> mono, double sr, double bpm, int beats);
    void updateAdaptive();
    ExportSource exportSource(const Preset& p, const SeqPattern* pat) const;
    void runJob(const juce::String& label, std::function<void()> job);

    const bool isFx_;
    // model
    mutable juce::CriticalSection lock_;
    Preset preset_; CandyHistory history_; TasteModel taste_; PresetManager presets_;
    std::vector<CandidateView> candidates_; std::shared_ptr<const TrackAnalysis> analysis_; MaskingReport masking_;
    std::shared_ptr<SeqPattern> pattern_;
    uint32_t genCounter_ = 1; juce::String status_ { "Ready" }, lastExport_;
    uint32_t pendingFlags_ = 0; std::atomic<bool> seqDirty_ { false }, bankDirty_ { false }, qualityDirty_ { false }; double bankDirtyAt_ = 0, seqDirtyAt_ = 0; uint32_t patternSeedCounter_ = 1;

    // audio-side objects
    CandyEngine engine_; SeqPlayer seq_; LiveAnalyzer live_;
    double sr_ = 44100.0; int maxBlock_ = 512; bool prepared_ = false;
    std::vector<float> candyL_, candyR_, scL_, scR_;
    float lastWet_ = 1.0f, lastDry_ = 1.0f, lastGain_ = 1.0f;
    double freePpq_ = 0.0; bool wasPlaying_ = false; float modWheel_ = 0, aftertouch_ = 0;
    std::atomic<int> auditionSlot_ { -1 }; std::atomic<float> auditionVel_ { 0.9f };
    uint32_t noteCounter_ = 0;
    struct Adaptive { std::atomic<float> lowCut { 0 }, eqHz { 1000 }, eqDb { 0 }, eqQ { 1 }, width { 1 }, trim { 0 }; std::atomic<bool> eqOn { false }; } adaptive_;

    // capture
    std::shared_ptr<CaptureBuffer> capture_; std::atomic<CaptureBuffer*> captureRaw_ { nullptr }; std::vector<std::shared_ptr<CaptureBuffer>> captureGrave_;
    bool captureHandled_ = true;

    // meters
    std::atomic<float> outPeak_ { 0 }, cpu_ { 0 }, progress_ { 0 }; std::atomic<int> voices_ { 0 }; std::atomic<uint64_t> protect_ { 0 };
    std::atomic<double> uiPlayhead_ { 0 }; std::atomic<bool> uiPlaying_ { false };

    // workers
    std::atomic<int> busyJobs_ { 0 }; std::atomic<bool> cancel_ { false }; std::atomic<uint32_t> bankGeneration_ { 0 };
    juce::CriticalSection mailLock_; std::deque<MailItem> mail_;
    juce::ThreadPool pool_ { juce::ThreadPoolOptions {}.withNumberOfThreads(1).withThreadName("DaliCandy worker") };
    TempExportFiles dragFiles_;
    std::atomic<bool> alive_ { true }; std::atomic<uint32_t> uiFlags_ { 0 };
    std::atomic<double> hostBpm_ { 120.0 }; std::atomic<bool> rollPending_ { false }, listenPending_ { false }, buildAfterCook_ { false }; uint32_t rollPick_ = 0; int lastListenPct_ = -2;
    std::vector<float> ring_; std::atomic<int64_t> ringWritten_ { 0 }; std::atomic<double> ringEndPpq_ { -1.0 }, ringBpm_ { 120.0 }; std::atomic<float> inputLevel_ { 0.0f }; std::atomic<bool> listenNeedsFile_ { false }, hostBpmKnown_ { false }, autoApply_ { false }, autoRunning_ { false }; std::atomic<double> analysisStartBeats_ { 0.0 }; int64_t lastAutoAt_ = -1; int autoRuns_ = 0;
    bool startListenFromRing(bool manual);                    // snapshot the ring buffer and analyse it; false = not enough / silent (status says why)
    void autoListenTick();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CandyProcessor)
};
} // namespace dali
