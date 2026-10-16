#pragma once
// CandyEngine: the shared audio engine of both plugins.
//  - SoundBank (8 slots) published lock-free from the message thread
//  - sample-accurate event queue
//  - voice pool with intelligent stealing
//  - 4 layers per voice (independent engine / envelope / modulation / insert / sends)
//  - global send effects + master section
#include "Core/Types.h"
#include "Core/CandyDNA.h"
#include "Core/Rng.h"
#include "DSP/Engine.h"
#include "DSP/Modulation/ModEngine.h"
#include "DSP/Effects/Effects.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace dali
{
// Trivially copyable snapshot of the audio-relevant part of a CandyDNA (no strings / heap).
struct Patch
{
    int numLayers = 1;
    std::array<LayerConfig, kMaxLayers> layers;
    std::array<ModRoute, kNumModRoutes> routes;
    ModulatorBank mods;
    FxConfig fx;
    float durationMs = 400.0f;
    uint32_t seed = 1;
    static Patch from(const CandyDNA& d);
};

struct SoundSlot
{
    bool valid = false;
    Patch patch;
    std::array<std::shared_ptr<SourceBuffer>, kMaxLayers> internalSrc;
};

struct SoundBankData
{
    std::array<SoundSlot, kNumSoundSlots> slots;
    FxConfig fx;                       // global effect settings (from the main sound)
    uint32_t version = 0;
};

// Builds a bank on a non-audio thread (synthesises the internal source material).
std::shared_ptr<SoundBankData> buildSoundBank(const std::array<const CandyDNA*, kNumSoundSlots>& dna, double sampleRate);

struct HostInfo
{
    double bpm = 120.0;
    double ppq = 0.0;       // position of the first sample of the block, in quarter notes
    bool   playing = false;
};

struct EventRequest
{
    int      sampleOffset = 0;     // within the current block
    int      slot = 0;
    float    velocity = 0.8f;
    float    pitchOffset = 0.0f;   // semitones
    float    midiNote = 60.0f;
    uint32_t eventSeed = 0;
    float    lengthScale = 1.0f;   // scales layer durations (sequencer event length)
};

class CandyEngine
{
public:
    CandyEngine();
    ~CandyEngine();

    void prepare(double sampleRate, int maxBlockSize, QualityMode q);
    void reset();

    // message thread
    void publishBank(std::shared_ptr<SoundBankData> bank);
    SourceHolder& externalSource() { return external_; }

    // audio thread --------------------------------------------------------------
    void setHost(const HostInfo& h) { host_ = h; }
    void setPerformance(float modWheel, float aftertouch, float follower, float transient, const float* macros4);
    void noteOffAll();
    bool queueEvent(const EventRequest& e);          // false if the queue is full
    // Renders into out (overwrites). Events queued before the call are started at their sample offsets.
    void process(float* outL, float* outR, int n);
    void processWithDry(float* outL, float* outR, int n);   // identical but keeps the buffers' content (adds)

    int  activeVoices() const;
    QualityMode quality() const { return quality_; }
    const MasterFx& master() const { return master_; }
    MasterFx& master() { return master_; }
    void setMasterOverride(bool on, float width, float gainDb) { ovr_ = on; ovrWidth_ = width; ovrGain_ = gainDb; }
    // Dry rendering: send effects (delay / reverb / mod-fx) are skipped; inserts and the master section stay active.
    void setBypassSends(bool b) { bypassSends_ = b; }
    // DRIFT 0..1: every new event nudges a slowly wandering offset of pitch, timbre parameters and length,
    // so repeated hits evolve instead of repeating.  0 = every hit identical in structure.
    void setDrift(float amount) { drift_ = std::clamp(amount, 0.0f, 1.0f); }
    // Conservative adaptive processing (psychoacoustic awareness): extra low cut and one dynamic-EQ style cut.
    void setAdaptive(float lowCutHz, bool eqOn, float eqHz, float eqGainDb, float eqQ, float widthScale, float trimDb)
    { adLowCut_ = lowCutHz; adEqOn_ = eqOn; adEqHz_ = eqHz; adEqDb_ = eqGainDb; adEqQ_ = eqQ; adWidth_ = widthScale; adTrim_ = trimDb; }
    uint64_t totalEvents() const { return totalEvents_; }

private:
    struct LayerRt
    {
        std::unique_ptr<IEngine> eng[kNumEngines];
        IEngine* active = nullptr;
        Envelope env;
        InsertFxUnit insert;
        enum State { Idle, Waiting, Playing } state = Idle;
        int waitSamples = 0; float elapsed = 0;
        Smoother gain, pan;
        bool audible = true;
    };
    struct Voice
    {
        std::array<LayerRt, kMaxLayers> layer;
        ModEngine mod;
        Patch patch;
        bool active = false; uint64_t startId = 0; float timeSec = 0; float velocity = 1, pitchOffset = 0, midiNote = 60, lengthScale = 1;
        uint32_t eventSeed = 0; int fadeOut = 0; bool fading = false; float peakEnv = 0;
        float maxLifeSec = 10.0f;
        // fixed-size render chunk: voice output never depends on the host block size
        float buf[8][kControlRate] {}; int bufPos = kControlRate; bool finished = false;
        float gDest[kNumDestinations] {};
    };

    void startVoice(const EventRequest& e);
    int  allocateVoice();
    void renderSegment(int offset, int n);
    void renderChunk(Voice& v, int hostOffset);
    void renderVoice(Voice& v, int offset, int n);

    double sr_ = 44100.0; int maxBlock_ = 512; QualityMode quality_ = QualityMode::Standard;
    std::vector<std::unique_ptr<Voice>> voices_;
    uint64_t voiceCounter_ = 0, totalEvents_ = 0;
    HostInfo host_;
    float modWheel_ = 0, aftertouch_ = 0, follower_ = 0, transient_ = 0; std::array<float, kNumMacros> macros_ { 0.5f, 0.5f, 0.5f, 0.5f };

    std::atomic<const SoundBankData*> bank_ { nullptr };
    std::mutex bankMutex_; std::vector<std::shared_ptr<SoundBankData>> bankGraveyard_;
    SourceHolder external_;

    static constexpr int kQueue = 256;
    std::array<EventRequest, kQueue> queue_; int queued_ = 0;

    // block buffers (preallocated)
    std::vector<float> dryL_, dryR_, sdL_, sdR_, srL_, srR_, smL_, smR_, fxL_, fxR_, tmpL_, tmpR_;
    DelayFx delay_; ReverbFx reverb_; ModFx modfx_; MasterFx master_;
    bool ovr_ = false; float ovrWidth_ = 1, ovrGain_ = 0;
    float gDest_[kNumDestinations] {};
    const SoundBankData* curBank_ = nullptr;
    int idleDelay_ = 0, idleReverb_ = 0, idleMod_ = 0;
    bool bypassSends_ = false;
    float drift_ = 0.0f, driftPitch_ = 0.0f, driftLen_ = 0.0f; std::array<float, kParamsPerLayer> driftP_ {}; Rng driftRng_ { 0xD21F7u };
    float adLowCut_ = 0, adEqHz_ = 1000, adEqDb_ = 0, adEqQ_ = 1, adWidth_ = 1, adTrim_ = 0; bool adEqOn_ = false;
    Biquad adEq_[2]; float adEqLastHz_ = -1, adEqLastDb_ = 1000;
};

} // namespace dali
