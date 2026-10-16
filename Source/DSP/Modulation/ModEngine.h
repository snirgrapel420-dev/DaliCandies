#pragma once
// Per-voice modulation: 4 LFO, 4 MSEG, 2 S&H, 2 Chaos, Random Walk, Step Modulator,
// Audio Envelope Follower, Transient Detector, MIDI sources, 4 Macros and a 32-route matrix.
// Source values: bipolar sources are -1..1, unipolar sources 0..1.  Route output is
// depth * shaped(source) in normalised units (a value of +1 spans the full parameter range).
#include "Core/Types.h"
#include "Core/CandyDNA.h"
#include "Core/Rng.h"
#include "Core/DspUtil.h"

namespace dali
{
class ModEngine
{
public:
    void prepare(double sampleRate);
    void start(const ModulatorBank& bank, const std::array<ModRoute, kNumModRoutes>& routes,
               uint32_t seed, float velocity, float midiNote);
    void setHost(float bpm, double ppq) { bpm_ = bpm > 1.0f ? bpm : 120.0f; ppq_ = ppq; }
    void setLive(float modWheel, float aftertouch, float follower, float transient)
    { modWheel_ = modWheel; aftertouch_ = aftertouch; follower_ = follower; transient_ = transient; }
    void setMacros(const float* m) { for (int i = 0; i < kNumMacros; ++i) macros_[(size_t) i] = m[i]; }

    // Advance by n samples; `timeSec` is time since trigger.  Recomputes destination offsets.
    void update(int n, float timeSec);

    float source(int s) const { return src_[(size_t) s]; }
    const float* destinations() const { return dest_.data(); }
    float dest(int d) const { return dest_[(size_t) d]; }

    // Make a route safe: clamps ranges and rejects feedback-prone combinations.
    // Returns false (and disables the route) if it was invalid.
    static bool validate(ModRoute& r);
    static bool isUnipolar(int src);

private:
    float lfoValue(int i, const LfoCfg& c, float inc);
    float msegValue(const MsegCfg& m, float t, int idx);
    static float shapeCurve(float v, int curve);

    double sr_ = 44100.0;
    ModulatorBank bank_;
    std::array<ModRoute, kNumModRoutes> routes_;
    Rng rng_;
    float bpm_ = 120.0f; double ppq_ = 0.0;
    float modWheel_ = 0, aftertouch_ = 0, follower_ = 0, transient_ = 0, velocity_ = 1, note_ = 0;
    std::array<float, kNumMacros> macros_ { 0.5f, 0.5f, 0.5f, 0.5f };
    std::array<float, kNumModSources> src_ {};
    std::array<float, kNumDestinations> dest_ {};

    // modulator state
    std::array<float, kNumLfos> lfoPh_ {}, lfoRnd_ {}, lfoRndPrev_ {}, lfoSmooth_ {};
    std::array<float, kNumSH> shVal_ {}, shOut_ {}, shPh_ {};
    std::array<float, kNumChaos> chX_ {}, chOut_ {}, chPh_ {}, chY_ {};
    float rwVal_ = 0, rwPh_ = 0, stepPh_ = 0, stepOut_ = 0; int stepIdx_ = 0;
    std::array<float, kNumModRoutes> routeSlew_ {}, routeHeld_ {};
    std::array<double, kNumModRoutes> routeGrid_ {};
    float elapsed_ = 0;
};

} // namespace dali
