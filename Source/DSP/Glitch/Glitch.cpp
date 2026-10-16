// ENGINE D - Glitch: stutters, retriggers, buffer slices, reverse fragments, micro-loops.
// Plays slices of a source buffer (internal burst material or captured audio) with
// probability triggering, rhythmic gating, bit / sample-rate reduction and crossfaded edges.
#include "DSP/Engine.h"

namespace dali
{
namespace
{
class Glitch final : public IEngine
{
public:
    EngineType type() const override { return EngineType::Glitch; }

    void prepare(double s, QualityMode q) override { sr = s; quality = q; reset(); }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer* src) override
    {
        reset();
        rng.reseed(seed);
        src_ = (src != nullptr && src->size() > 64) ? src : nullptr;
        vel = velocity;
        remaining = 0;
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        if (src_ == nullptr) { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); return; }
        const auto& p = cf.p;
        const int   srcN = src_->size();
        const double ratioBase = src_->sampleRate / sr;
        const float sliceMs = expMap(p[0], 6.0f, 240.0f);
        const float revProb = p[1], stutter = p[2], retrig = p[3];
        const float bits = 16.0f - p[4] * 14.0f;
        const float step = std::pow(2.0f, 1.0f - bits);
        const int   hold = 1 + (int) (p[5] * p[5] * 31.0f * (float) (sr / 44100.0));   // same decimation frequency at every sample rate
        const bool  gateOn = p[6] > 0.05f; const float duty = linMap(p[7], 0.1f, 0.9f);
        const float prob = linMap(p[8], 0.15f, 1.0f);
        const float loopMs = expMap(p[9], 1.5f, 32.0f);
        const bool  loopOn = p[9] > 0.12f;
        const int   xf = std::max(8, (int) (linMap(p[13], 1.5f, 20.0f) * 0.001f * (float) sr));
        const float drift = p[14] * 0.25f, artifact = p[15];
        const float bpm = std::max(30.0f, cf.bpm);
        const float gateHz = (bpm / 60.0f) * 2.0f * std::exp2(std::floor(p[6] * 4.0f));      // 8th .. 128th-ish

        for (int i = 0; i < n; ++i)
        {
            if (remaining <= 0) newSlice(srcN, ratioBase, sliceMs, revProb, stutter, retrig, prob, loopMs, loopOn, drift);

            float x = 0.0f;
            if (! rest)
            {
                const double rp = std::clamp(pos, 0.0, (double) (srcN - 2));
                const int i0 = (int) rp; const float fr = (float) (rp - (double) i0);
                x = src_->data[(size_t) i0] + (src_->data[(size_t) (i0 + 1)] - src_->data[(size_t) i0]) * fr;
                const int done = sliceLen - remaining;
                float e = 1.0f;
                if (done < xf) e = std::sin(0.5f * kPi * (float) done / (float) xf);
                if (remaining < xf) e = std::min(e, std::sin(0.5f * kPi * (float) remaining / (float) xf));
                x *= e;
                // advance with loop handling
                pos += rate;
                if (loopOn)
                {
                    const double d = (pos - loopStart) * dirSign;
                    if (d >= loopSamples) pos = loopStart;
                }
            }
            --remaining;

            if (gateOn)
            {
                gatePh += gateHz / (float) sr; gatePh -= std::floor(gatePh);
                const float target = gatePh < duty ? 1.0f : 0.0f;
                gateEnv += (target - gateEnv) * 0.02f;
                x *= gateEnv;
            }
            // bit / rate reduction
            if (--holdCount <= 0) { holdCount = hold; held = std::floor(x / step + 0.5f) * step; }
            x = lerp(x, held, artifact);
            x = sanitize(x) * (0.35f + 0.65f * vel);
            L[i] = x * panL; R[i] = x * panR;
        }
    }

private:
    void reset()
    {
        remaining = 0; pos = 0; rate = 1; gatePh = 0; gateEnv = 1; holdCount = 0; held = 0; rest = true; repeatsLeft = 0;
        panL = panR = 0.7f; sliceLen = 1; dirSign = 1; loopStart = 0; loopSamples = 1;
    }

    void newSlice(int srcN, double ratioBase, float sliceMs, float revProb, float stutter, float retrig,
                  float prob, float loopMs, bool loopOn, float drift)
    {
        if (repeatsLeft <= 0)
        {
            // choose a fresh slice from the source
            const int sl = std::max(32, (int) (sliceMs * 0.001f * (float) src_->sampleRate));
            const int grid = std::max(1, srcN / sl - 1);
            curStart = (double) (rng.rangeInt(0, grid - 1) * sl);
            curLen = (double) sl; curReverse = rng.uniform() < revProb;
            repeatsLeft = 1 + (int) std::round(retrig * 7.0f);
            repeatIndex = 0;
            curPlay = rng.uniform() < prob;
            const float pan = rng.bipolar() * 0.45f; panL = panGainL(pan) * 1.2f; panR = panGainR(pan) * 1.2f;
        }
        const double shrink = std::pow(1.0 - 0.4 * (double) stutter, (double) repeatIndex);
        double len = std::max(0.003 * src_->sampleRate, curLen * shrink);
        ++repeatIndex; --repeatsLeft;
        rest = ! curPlay;
        rate = ratioBase * (1.0 + (double) drift * (double) rng.bipolar());
        dirSign = curReverse ? -1.0 : 1.0;
        sliceLen = std::max(16, (int) (len / ratioBase));
        remaining = sliceLen;
        const double a = curStart, b = std::min((double) (srcN - 2), curStart + len);
        pos = curReverse ? b : a; rate *= dirSign;
        loopStart = pos;
        loopSamples = loopOn ? std::max(8.0, (double) loopMs * 0.001 * src_->sampleRate) : 1.0e12;
        startPos = pos;
    }

    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    const SourceBuffer* src_ = nullptr; Rng rng; float vel = 1.0f;
    int remaining = 0, sliceLen = 1, repeatsLeft = 0, repeatIndex = 0, holdCount = 0;
    double pos = 0, rate = 1, curStart = 0, curLen = 1, loopStart = 0, loopSamples = 1, startPos = 0, dirSign = 1;
    bool rest = true, curReverse = false, curPlay = true;
    float gatePh = 0, gateEnv = 1, held = 0, panL = 0.7f, panR = 0.7f;
};
} // namespace

std::unique_ptr<IEngine> createGlitch() { return std::make_unique<Glitch>(); }
} // namespace dali
