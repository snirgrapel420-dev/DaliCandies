// ENGINE C - Granular: evolving textures, fragments, reverse grains, sound clouds.
// Fixed grain pool, deterministic scheduling, internal synthesis source or imported audio.
#include "DSP/Engine.h"

namespace dali
{
namespace
{
constexpr int kMaxGrainsHigh = 24, kMaxGrainsEco = 10;

struct Grain
{
    bool   active = false;
    double pos = 0;       // read position in source samples
    double inc = 1;       // signed increment (negative = reverse)
    int    age = 0, len = 1;
    float  gl = 0.7f, gr = 0.7f, amp = 1.0f;
};

class Granular final : public IEngine
{
public:
    EngineType type() const override { return EngineType::Granular; }

    void prepare(double s, QualityMode q) override
    {
        sr = s; quality = q;
        maxGrains = (q == QualityMode::Eco) ? kMaxGrainsEco : kMaxGrainsHigh;
        reset();
    }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer* src) override
    {
        reset();
        rng.reseed(seed);
        vel = velocity;
        src_ = (src != nullptr && src->size() > 256) ? src : nullptr;
        scanPos = 0.0;
        nextGrainIn = 0.0;
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        const auto& p = cf.p;
        const float grainMs   = expMap(p[2], 6.0f, 260.0f);
        const float rate      = expMap(p[3], 3.0f, 220.0f);                 // grains per second
        const float gPitch    = (p[4] - 0.5f) * 48.0f;                       // semitones
        const float pSpread   = p[5] * 12.0f;
        const float pos0      = p[6], posSpread = p[7] * 0.5f;
        const float revProb   = p[8], stereo = p[9], envShape = p[10];
        const bool  freeze    = p[11] > 0.5f;
        const float scan      = (p[12] - 0.5f) * 2.0f * 0.5f;                // source-lengths per second
        const float jitter    = p[13], levelVar = p[15];
        if (src_ == nullptr) { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); return; }
        const int   len       = src_->size();
        const float* src      = src_->data.data();
        const double srcRatio = src_->sampleRate / sr;
        const float overlap   = std::max(1.0f, rate * grainMs * 0.001f);
        const float norm      = 1.0f / std::sqrt(overlap) * 1.1f;

        for (int i = 0; i < n; ++i)
        {
            if (! freeze) scanPos += (double) scan / sr;
            nextGrainIn -= 1.0;
            if (nextGrainIn <= 0.0)
            {
                const double base = sr / (double) rate;
                nextGrainIn += base * (1.0 + (double) jitter * (double) rng.bipolar() * 0.9);
                spawn(len, srcRatio, grainMs, gPitch, pSpread, pos0, posSpread, revProb, stereo, levelVar, scanPos);
            }
            float sl = 0, sr_ = 0;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float u = (float) g.age / (float) g.len;
                // window: blend hann <-> triangle <-> trapezoid
                const float hann = 0.5f - 0.5f * std::cos(kTwoPi * u);
                const float tri  = 1.0f - std::abs(2.0f * u - 1.0f);
                const float trap = std::min(1.0f, std::min(u, 1.0f - u) * 6.0f);
                const float w = envShape < 0.5f ? lerp(hann, tri, envShape * 2.0f) : lerp(tri, trap, (envShape - 0.5f) * 2.0f);
                // linear interpolation read
                double rp = g.pos;
                rp = std::clamp(rp, 0.0, (double) (len - 2));
                const int i0 = (int) rp; const float fr = (float) (rp - (double) i0);
                const float s = (src[i0] + (src[i0 + 1] - src[i0]) * fr) * w * g.amp;
                sl += s * g.gl; sr_ += s * g.gr;
                g.pos += g.inc; if (++g.age >= g.len) g.active = false;
            }
            L[i] = sanitize(sl * norm * (0.3f + 0.7f * vel));
            R[i] = sanitize(sr_ * norm * (0.3f + 0.7f * vel));
        }
    }

private:
    void reset()
    {
        for (auto& g : grains) g.active = false;
        grains.assign((size_t) kMaxGrainsHigh, Grain());
        nextGrainIn = 0; scanPos = 0;
    }

    void spawn(int len, double srcRatio, float grainMs, float gPitch, float pSpread, float pos0, float posSpread,
               float revProb, float stereo, float levelVar, double scanOffset)
    {
        Grain* slot = nullptr;
        for (int i = 0; i < maxGrains; ++i) if (! grains[(size_t) i].active) { slot = &grains[(size_t) i]; break; }
        if (! slot)
        {   // voice stealing: replace the grain that is furthest along
            int best = 0; float bestU = -1.0f;
            for (int i = 0; i < maxGrains; ++i) { const float u = (float) grains[(size_t) i].age / (float) grains[(size_t) i].len; if (u > bestU) { bestU = u; best = i; } }
            slot = &grains[(size_t) best];
        }
        const float semis = gPitch + pSpread * rng.bipolar();
        const double ratio = std::exp2((double) semis / 12.0) * srcRatio;
        const int glen = std::max(16, (int) (grainMs * 0.001f * (float) sr * rng.range(0.85f, 1.15f)));
        float pos = pos0 + (float) (scanOffset - std::floor(scanOffset)) + posSpread * rng.bipolar();
        pos = pos - std::floor(pos);
        const bool rev = rng.uniform() < revProb;
        const double span = (double) glen * ratio;
        double start = (double) pos * (double) (len - 2);
        if (rev) { start = std::min(start + span, (double) (len - 2)); }
        else     { start = std::min(start, std::max(0.0, (double) (len - 2) - span)); }
        slot->active = true; slot->age = 0; slot->len = glen; slot->pos = start; slot->inc = rev ? -ratio : ratio;
        const float pan = stereo * rng.bipolar();
        slot->gl = panGainL(pan); slot->gr = panGainR(pan);
        slot->amp = 1.0f - levelVar * 0.7f * rng.uniform();
    }

    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    const SourceBuffer* src_ = nullptr;
    std::vector<Grain> grains; int maxGrains = kMaxGrainsHigh;
    Rng rng; double nextGrainIn = 0, scanPos = 0; float vel = 1.0f;
};
} // namespace

std::unique_ptr<IEngine> createGranular() { return std::make_unique<Granular>(); }
} // namespace dali
