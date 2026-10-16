#include "ModEngine.h"

namespace dali
{
void ModEngine::prepare(double sampleRate) { sr_ = sampleRate; }

bool ModEngine::isUnipolar(int s)
{
    switch (s)
    {
        case kSrcMseg1: case kSrcMseg2: case kSrcMseg3: case kSrcMseg4:
        case kSrcStep: case kSrcEnvFollower: case kSrcTransient: case kSrcVelocity:
        case kSrcModWheel: case kSrcAftertouch:
        case kSrcMacro1: case kSrcMacro2: case kSrcMacro3: case kSrcMacro4: return true;
        default: return false;
    }
}

bool ModEngine::validate(ModRoute& r)
{
    r.source = std::clamp(r.source, 0, kNumModSources - 1);
    r.dest   = std::clamp(r.dest, 0, kNumDestinations - 1);
    r.depth  = std::clamp(r.depth, -1.0f, 1.0f);
    r.curve  = std::clamp(r.curve, 0, 3);
    r.slewMs = std::clamp(r.slewMs, 0.0f, 2000.0f);
    r.syncDiv = std::clamp(r.syncDiv, 1, 32);
    if (! r.enabled) return true;

    // Level-following sources must never drive level / send destinations: in the instrument they follow the
    // plugin's own output, which would create a feedback loop.
    const bool followerSrc = (r.source == kSrcEnvFollower || r.source == kSrcTransient);
    bool levelDest = (r.dest == kGdMasterGain);
    if (r.dest >= kLayerDestBase && r.dest < 96)
    {
        const int k = (r.dest - kLayerDestBase) % 8;
        levelDest = levelDest || k == kLdGain || k == kLdSendDelay || k == kLdSendReverb || k == kLdSendMod;
    }
    if (r.dest == kGdDelayFeedback || r.dest == kGdReverbSize) levelDest = levelDest || followerSrc;
    if (followerSrc && levelDest) { r.enabled = false; return false; }
    return true;
}

void ModEngine::start(const ModulatorBank& bank, const std::array<ModRoute, kNumModRoutes>& routes,
                      uint32_t seed, float velocity, float midiNote)
{
    bank_ = bank; routes_ = routes; macros_ = bank.macros;
    for (auto& r : routes_) validate(r);
    rng_.reseed(seed ^ 0x6D6F64u);
    velocity_ = velocity; note_ = std::clamp((midiNote - 60.0f) / 24.0f, -1.0f, 1.0f);
    elapsed_ = 0;
    for (int i = 0; i < kNumLfos; ++i)
    {
        lfoPh_[(size_t) i] = bank_.lfo[(size_t) i].phase; lfoRnd_[(size_t) i] = rng_.bipolar(); lfoRndPrev_[(size_t) i] = rng_.bipolar(); lfoSmooth_[(size_t) i] = 0;
    }
    for (int i = 0; i < kNumSH; ++i) { shVal_[(size_t) i] = rng_.bipolar(); shOut_[(size_t) i] = shVal_[(size_t) i]; shPh_[(size_t) i] = 0; }
    for (int i = 0; i < kNumChaos; ++i) { chX_[(size_t) i] = 0.2f + 0.6f * rng_.uniform(); chY_[(size_t) i] = 0.1f * rng_.uniform(); chOut_[(size_t) i] = 0; chPh_[(size_t) i] = 0; }
    rwVal_ = 0; rwPh_ = 0; stepPh_ = 0; stepIdx_ = 0; stepOut_ = bank_.step.steps[0];
    for (int i = 0; i < kNumModRoutes; ++i) { routeSlew_[(size_t) i] = 0; routeHeld_[(size_t) i] = 0; routeGrid_[(size_t) i] = -1.0; }
    src_.fill(0.0f); dest_.fill(0.0f);
}

float ModEngine::lfoValue(int i, const LfoCfg& c, float inc)
{
    auto& ph = lfoPh_[(size_t) i];
    const float prev = ph;
    ph += inc; if (ph >= 1.0f) ph -= std::floor(ph);
    const bool wrapped = ph < prev;
    switch (c.shape)
    {
        case 0: return std::sin(kTwoPi * ph);
        case 1: return 4.0f * std::abs(ph - 0.5f) - 1.0f;
        case 2: return 2.0f * ph - 1.0f;
        case 3: return ph < 0.5f ? 1.0f : -1.0f;
        case 4: if (wrapped) lfoRnd_[(size_t) i] = rng_.bipolar(); return lfoRnd_[(size_t) i];
        case 5:
        {
            if (wrapped) { lfoRndPrev_[(size_t) i] = lfoRnd_[(size_t) i]; lfoRnd_[(size_t) i] = rng_.bipolar(); }
            const float t = ph * ph * (3.0f - 2.0f * ph);
            return lerp(lfoRndPrev_[(size_t) i], lfoRnd_[(size_t) i], t);
        }
        default:
        {
            const float x = ph * (float) kCurvePoints; const int a = (int) x & (kCurvePoints - 1), b = (a + 1) & (kCurvePoints - 1);
            return lerp(c.curve[(size_t) a], c.curve[(size_t) b], x - std::floor(x)) * 2.0f - 1.0f;
        }
    }
}

float ModEngine::msegValue(const MsegCfg& m, float t, int)
{
    const float len = std::max(5.0f, m.lengthMs) * 0.001f;
    float u = t / len;
    if (m.loop) u -= std::floor(u); else u = std::min(u, 1.0f);
    const int n = std::clamp(m.numPoints, 2, kMsegPoints);
    if (u <= m.t[0]) return m.v[0];
    for (int k = 1; k < n; ++k)
    {
        if (u <= m.t[(size_t) k] || k == n - 1)
        {
            const float t0 = m.t[(size_t) k - 1], t1 = std::max(t0 + 1.0e-5f, m.t[(size_t) k]);
            float f = std::clamp((u - t0) / (t1 - t0), 0.0f, 1.0f);
            const float c = m.c[(size_t) k];                       // -1..1 : concave .. convex
            f = std::pow(f, std::exp(c * 1.6f));
            return lerp(m.v[(size_t) k - 1], m.v[(size_t) k], f);
        }
    }
    return m.v[(size_t) n - 1];
}

float ModEngine::shapeCurve(float v, int curve)
{
    const float a = std::abs(v), s = v < 0 ? -1.0f : 1.0f;
    switch (curve)
    {
        case 1: return s * a * a;                                   // exponential
        case 2: return s * std::sqrt(a);                            // logarithmic
        case 3: return s * (a * a * (3.0f - 2.0f * a));             // S-curve
        default: return v;
    }
}

void ModEngine::update(int n, float timeSec)
{
    const float dt = (float) n / (float) sr_;
    elapsed_ = timeSec;
    const float beatHz = bpm_ / 60.0f;

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto& c = bank_.lfo[(size_t) i];
        const float hz = c.sync ? beatHz * (float) c.syncDiv * 0.25f : c.rateHz;
        src_[(size_t) (kSrcLfo1 + i)] = lfoValue(i, c, hz * dt);
    }
    for (int i = 0; i < kNumMsegs; ++i) src_[(size_t) (kSrcMseg1 + i)] = msegValue(bank_.mseg[(size_t) i], timeSec, i);
    for (int i = 0; i < kNumSH; ++i)
    {
        const auto& c = bank_.sh[(size_t) i];
        const float hz = c.sync ? beatHz * (float) c.syncDiv * 0.25f : c.rateHz;
        shPh_[(size_t) i] += hz * dt;
        if (shPh_[(size_t) i] >= 1.0f) { shPh_[(size_t) i] -= std::floor(shPh_[(size_t) i]); shVal_[(size_t) i] = rng_.bipolar(); }
        const float a = c.slewMs <= 0.0f ? 1.0f : 1.0f - std::exp(-dt / (0.001f * c.slewMs));
        shOut_[(size_t) i] += a * (shVal_[(size_t) i] - shOut_[(size_t) i]);
        src_[(size_t) (kSrcSH1 + i)] = shOut_[(size_t) i];
    }
    for (int i = 0; i < kNumChaos; ++i)
    {
        const auto& c = bank_.chaos[(size_t) i];
        chPh_[(size_t) i] += c.rateHz * dt;
        while (chPh_[(size_t) i] >= 1.0f)
        {
            chPh_[(size_t) i] -= 1.0f;
            auto& x = chX_[(size_t) i];
            if (c.type == 0) { x = std::clamp(std::clamp(c.r, 3.5f, 3.99f) * x * (1.0f - x), 0.001f, 0.999f); }
            else   // Henon-style map
            {
                auto& y = chY_[(size_t) i]; const float nx = 1.0f - 1.4f * (x - 0.5f) * (x - 0.5f) * 4.0f + y; y = 0.3f * (x - 0.5f);
                x = std::clamp(0.5f + 0.35f * nx, 0.001f, 0.999f);
            }
        }
        const float target = (chX_[(size_t) i] - 0.5f) * 2.0f;
        chOut_[(size_t) i] += std::min(1.0f, 12.0f * dt * std::max(1.0f, c.rateHz * 0.25f)) * (target - chOut_[(size_t) i]);
        src_[(size_t) (kSrcChaos1 + i)] = chOut_[(size_t) i];
    }
    rwPh_ += bank_.rw.rateHz * dt;
    while (rwPh_ >= 1.0f) { rwPh_ -= 1.0f; rwVal_ += rng_.bipolar() * bank_.rw.step - 0.04f * rwVal_; rwVal_ = std::clamp(rwVal_, -1.0f, 1.0f); }
    src_[kSrcRandomWalk] = rwVal_;
    {
        const auto& s = bank_.step;
        const float hz = s.sync ? beatHz * (float) s.syncDiv * 0.25f : s.rateHz;
        stepPh_ += hz * dt;
        while (stepPh_ >= 1.0f) { stepPh_ -= 1.0f; stepIdx_ = (stepIdx_ + 1) % std::max(1, s.length); }
        const float target = s.steps[(size_t) stepIdx_];
        const float a = s.slewMs <= 0.0f ? 1.0f : 1.0f - std::exp(-dt / (0.001f * s.slewMs));
        stepOut_ += a * (target - stepOut_);
        src_[kSrcStep] = stepOut_;
    }
    src_[kSrcEnvFollower] = follower_; src_[kSrcTransient] = transient_;
    src_[kSrcVelocity] = velocity_; src_[kSrcNote] = note_;
    src_[kSrcModWheel] = modWheel_; src_[kSrcAftertouch] = aftertouch_;
    for (int i = 0; i < kNumMacros; ++i) src_[(size_t) (kSrcMacro1 + i)] = macros_[(size_t) i];

    // ---- matrix
    dest_.fill(0.0f);
    for (int i = 0; i < kNumModRoutes; ++i)
    {
        const auto& r = routes_[(size_t) i];
        if (! r.enabled || r.depth == 0.0f) continue;
        float v = src_[(size_t) r.source];
        if (r.sync)
        {
            const double cell = std::floor(ppq_ * (double) r.syncDiv * 0.25 + 1.0e-9);
            if (cell != routeGrid_[(size_t) i]) { routeGrid_[(size_t) i] = cell; routeHeld_[(size_t) i] = v; }
            v = routeHeld_[(size_t) i];
        }
        v = shapeCurve(v, r.curve);
        if (r.slewMs > 0.0f)
        {
            const float a = 1.0f - std::exp(-dt / (0.001f * r.slewMs));
            routeSlew_[(size_t) i] += a * (v - routeSlew_[(size_t) i]); v = routeSlew_[(size_t) i];
        }
        dest_[(size_t) r.dest] += r.depth * v;
    }
    for (auto& d : dest_) d = std::clamp(d, -2.0f, 2.0f);
}
} // namespace dali
