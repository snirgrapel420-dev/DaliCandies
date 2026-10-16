// ENGINE F - Noise & Motion: alien breaths, organic chirps, psychedelic sweeps, moving noise.
// White/pink/filtered noise, formant filters, random walks, chaotic (logistic map) modulation,
// resonant sweeps, breath-like envelopes and decorrelated stereo movement.
#include "DSP/Engine.h"

namespace dali
{
namespace
{
class NoiseMotion final : public IEngine
{
public:
    EngineType type() const override { return EngineType::NoiseMotion; }

    void prepare(double s, QualityMode q) override
    {
        sr = s; quality = q;
        for (auto& f : bp) f.setSampleRate(sr);
        for (auto& f : tonal) f.setSampleRate(sr);
        for (auto& v : formant) { for (auto& f : v) f.setSampleRate(sr); }
        reset();
    }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer*) override
    {
        reset();
        rng.reseed(seed);
        nz[0].seed(seed * 2654435761u + 3u); nz[1].seed(seed * 40503u + 977u);
        rw = 0; chaosX = 0.1f + 0.8f * rng.uniform(); chaosSm = 0; org = 0; orgTarget = 0;
        breathPh = rng.uniform(); sweepPh = rng.uniform(); panPh = rng.uniform();
        vel = velocity;
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        const auto& p = cf.p;
        static const float vow[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2700 } };
        const float colour = p[0];
        const float centre0 = expMap(p[1], 80.0f, 12000.0f);
        const float q = 0.6f + p[2] * p[2] * 24.0f;
        const float sweepAmt = (p[3] - 0.5f) * 2.0f, sweepRate = expMap(p[4], 0.2f, 8.0f);
        const float vp = p[5] * 4.0f; const int vi = std::min(3, (int) vp); const float vf = vp - (float) vi;
        const float fmix = p[6];
        const float rwAmt = p[7], chaosAmt = p[8];
        const float breathDepth = clamp01(p[9] * 1.3f), breathPow = 0.8f + p[9] * 4.0f, breathRate = expMap(p[10], 0.3f, 6.0f);
        const float moveAmt = p[11], moveRate = expMap(p[12], 0.1f, 5.0f);
        const float organic = p[13], chirp = p[14], tonalAmt = p[15];
        const float prog = cf.progress;

        for (int i = 0; i < n; ++i)
        {
            // slow modulators
            sweepPh += sweepRate / (float) sr; sweepPh -= std::floor(sweepPh);
            breathPh += breathRate / (float) sr; breathPh -= std::floor(breathPh);
            panPh += moveRate / (float) sr; panPh -= std::floor(panPh);
            if (--chaosCount <= 0) { chaosCount = (int) (sr / 45.0); chaosX = 3.86f * chaosX * (1.0f - chaosX); chaosX = std::clamp(chaosX, 0.001f, 0.999f); }
            chaosSm += 0.0015f * ((chaosX - 0.5f) * 2.0f - chaosSm);
            if (--orgCount <= 0) { orgCount = (int) (sr / 6.0); orgTarget = rng.bipolar(); }
            org += 0.0004f * (orgTarget - org);
            rw += 0.00025f * rwAmt * (rng.bipolar() * 6.0f) - 0.00008f * rw; rw = std::clamp(rw, -1.5f, 1.5f);

            if ((i & (kControlRate - 1)) == 0)
            {
                float oct = sweepAmt * 4.0f * (prog - 0.5f)
                          + std::sin(kTwoPi * sweepPh) * std::abs(sweepAmt) * 1.2f
                          + rw * rwAmt * 1.5f + chaosSm * chaosAmt * 1.2f + org * organic * 0.7f;
                const float centre = std::clamp(centre0 * std::exp2(oct), 40.0f, 16000.0f);
                for (int c = 0; c < 2; ++c) bp[c].set(centre, q);
                float f[3];
                for (int k = 0; k < 3; ++k) f[k] = lerp(vow[vi][k], vow[vi + 1][k], vf) * std::exp2(oct * 0.25f);
                for (int c = 0; c < 2; ++c) for (int k = 0; k < 3; ++k) formant[c][k].set(f[k], 9.0f);
                const float cf0 = std::clamp(centre * std::exp2(chirp * 3.0f * (1.0f - prog) * (1.0f - prog)), 60.0f, 15000.0f);
                for (int c = 0; c < 2; ++c) tonal[c].set(cf0, 4.0f + tonalAmt * 70.0f);
                // amplitude shaping
                const float br = std::pow(0.5f - 0.5f * std::cos(kTwoPi * breathPh), breathPow);
                ampTarget = lerp(1.0f, br, breathDepth) * (1.0f + 0.5f * chaosSm * chaosAmt) * (1.0f + 0.25f * org * organic);
                ampTarget = std::max(0.0f, ampTarget);
            }
            amp += 0.02f * (ampTarget - amp);

            const float pan = moveAmt * std::sin(kTwoPi * panPh);
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float w = nz[c].white();
                const float pk = pink[c].process(w);
                float x = lerp(w, pk * 2.0f, colour);
                float y = bp[c].tick(x, 1) * (1.0f + 0.4f * std::sqrt(q));
                if (fmix > 0.002f)
                {
                    float fsum = 0.0f;
                    for (int k = 0; k < 3; ++k) fsum += formant[c][k].tick(x, 1) * (k == 0 ? 1.0f : (k == 1 ? 0.7f : 0.4f));
                    y = lerp(y, fsum * 1.5f, fmix);
                }
                if (tonalAmt > 0.002f || chirp > 0.002f)
                {
                    const float t = tonal[c].tick(x * 0.5f, 1) * (0.6f + 0.4f * std::sqrt(4.0f + tonalAmt * 70.0f));
                    y += t * std::max(tonalAmt, chirp) * 0.9f;
                }
                out[c] = sanitize(y * amp);
            }
            L[i] = out[0] * panGainL(pan) * 1.2f * (0.3f + 0.7f * vel) * 0.5f;
            R[i] = out[1] * panGainR(pan) * 1.2f * (0.3f + 0.7f * vel) * 0.5f;
        }
    }

private:
    void reset()
    {
        for (auto& f : bp) { f.reset(); }
        for (auto& f : tonal) { f.reset(); }
        for (auto& v : formant) { for (auto& f : v) f.reset(); }
        pink[0] = pink[1] = PinkNoise(); rw = chaosX = chaosSm = org = orgTarget = 0; chaosX = 0.3f; chaosCount = orgCount = 0; amp = 0; ampTarget = 1;
        breathPh = sweepPh = panPh = 0;
    }
    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    Svf bp[2], tonal[2], formant[2][3]; FastNoise nz[2]; PinkNoise pink[2]; Rng rng;
    float rw = 0, chaosX = 0.3f, chaosSm = 0, org = 0, orgTarget = 0, amp = 0, ampTarget = 1, vel = 1.0f, breathPh = 0, sweepPh = 0, panPh = 0;
    int chaosCount = 0, orgCount = 0;
};
} // namespace

std::unique_ptr<IEngine> createNoiseMotion() { return std::make_unique<NoiseMotion>(); }
} // namespace dali
