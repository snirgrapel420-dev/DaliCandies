// ENGINE A - Micro Synth: bleeps, blips, droplets, lasers, clicks, chirps.
// Band-limited (PolyBLEP) oscillators, two-operator FM, AM, ring modulation,
// pitch envelope, multimode resonant filter, noise, waveshaper.
#include "DSP/Engine.h"

namespace dali
{
namespace
{
class MicroSynth final : public IEngine
{
public:
    EngineType type() const override { return EngineType::MicroSynth; }

    void prepare(double s, QualityMode q) override
    {
        sr = s; quality = q;
        filt.setSampleRate(sr); dc.setup(sr); os.prepare(sr);
        gainSm.setup(sr, 3.0f);
        reset();
    }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer*) override
    {
        reset();
        noise.seed(seed * 2654435761u + 17u);
        Rng r(seed);
        ph1 = r.uniform(); ph2 = 0.0f; ph3 = r.uniform();
        vibPhase = r.uniform();
        vel = velocity;
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        const auto& p = cf.p;
        // ---- parameter mapping (once per control block)
        const float wavePos   = p[0] * 3.0f;
        const float pw        = linMap(p[1], 0.08f, 0.5f);
        static const float ratios[12] = { 0.5f, 1.0f, 1.414f, 1.5f, 2.0f, 2.756f, 3.0f, 4.0f, 5.404f, 7.0f, 9.2f, 11.0f };
        const float fp = p[2] * 11.0f; const int ri = (int) fp;
        const float ratio = lerp(ratios[ri], ratios[std::min(ri + 1, 11)], fp - (float) ri);
        const float fmAmt     = p[3] * p[3] * 9.0f;
        const float amAmt     = p[4];
        const float ringAmt   = p[5];
        const float pitchEnv  = (p[6] - 0.5f) * 96.0f;                // +/-48 semitones
        const float pitchDecay= expMap(p[7], 4.0f, 700.0f) * 0.001f;
        const float pitchCoef = std::exp(-1.0f / (pitchDecay * (float) sr));
        const float cutoffBase= expMap(p[8], 40.0f, 18000.0f) * (0.6f + 0.4f * vel);
        const float q         = 0.6f + p[9] * p[9] * 18.0f;
        const int   ftype     = std::min(3, (int) (p[10] * 4.0f));
        const float noiseAmt  = p[11] * p[11];
        const float drive     = 1.0f + p[12] * 9.0f;
        const float fenv      = (p[13] - 0.5f) * 2.0f * 5.0f;          // octaves
        const float detune    = std::pow(2.0f, p[14] * 0.35f / 12.0f);  // up to 35 cents
        const float vibDepth  = p[15] * 0.6f;                           // semitones
        const bool  useOs     = (quality == QualityMode::High);
        const float ampScale  = 0.25f + 0.75f * vel;

        const float baseHz = std::max(8.0f, cf.hz);
        for (int i = 0; i < n; ++i)
        {
            // pitch envelope (exponential decay toward 0)
            pitchEnvState = (i == 0 && first) ? 1.0f : pitchEnvState;
            first = false;
            const float semis = pitchEnv * pitchEnvState + vibDepth * std::sin(kTwoPi * vibPhase);
            pitchEnvState = killDenormal(pitchEnvState * pitchCoef);
            vibPhase += 6.0f / (float) sr; if (vibPhase >= 1.0f) vibPhase -= 1.0f;

            float f = baseHz * std::exp2(semis * (1.0f / 12.0f));
            f = std::clamp(f, 5.0f, (float) sr * 0.45f);
            const float dt = f / (float) sr;

            // modulator
            ph2 += dt * ratio; if (ph2 >= 1.0f) ph2 -= 1.0f;
            const float mod = std::sin(kTwoPi * ph2);

            auto osc = [&](float phase, float inc) {
                phase += fmAmt * mod * 0.15915f * (1.0f - 0.7f * cf.progress); // PM in cycles
                phase -= std::floor(phase);
                const float sine = std::sin(kTwoPi * phase);
                const float tri  = 4.0f * std::abs(phase - 0.5f) - 1.0f;
                float saw = 2.0f * phase - 1.0f; saw -= polyBlep(phase, inc);
                float pulse = (phase < pw ? 1.0f : -1.0f);
                pulse += polyBlep(phase, inc);
                pulse -= polyBlep(std::fmod(phase + 1.0f - pw, 1.0f), inc);
                const float w[4] = { sine, tri, saw * 0.8f, pulse * 0.6f };
                const int wi = std::min(2, (int) wavePos); const float wf = wavePos - (float) wi;
                return lerp(w[wi], w[wi + 1], wf);
            };

            ph1 += dt;  if (ph1 >= 1.0f) ph1 -= 1.0f;
            ph3 += dt * detune; if (ph3 >= 1.0f) ph3 -= 1.0f;
            float x = osc(ph1, dt);
            if (p[14] > 0.02f) x = 0.6f * x + 0.4f * osc(ph3, dt * detune);

            // AM / ring
            if (amAmt > 0.001f)  x *= (1.0f - amAmt) + amAmt * (0.5f + 0.5f * mod);
            if (ringAmt > 0.001f) x = lerp(x, x * mod, ringAmt);
            if (noiseAmt > 0.0005f) x = x * (1.0f - 0.5f * noiseAmt) + noise.white() * noiseAmt * 0.8f;

            // filter with envelope-following cutoff
            const float cutoff = cutoffBase * std::exp2(fenv * pitchEnvState);
            if ((i & (kControlRate - 1)) == 0) filt.set(cutoff, q);
            x = filt.tick(x, ftype);

            // waveshaper (soft saturation), optionally oversampled
            if (drive > 1.05f)
            {
                const float g = drive;
                x = useOs ? os.process(x, [g](float v) { return fastTanh(v * g) / std::sqrt(g); })
                          : fastTanh(x * g) / std::sqrt(g);
            }
            x = dc.process(x) * ampScale;
            x = sanitize(x);
            L[i] = R[i] = x;
        }
    }

private:
    void reset()
    {
        ph1 = ph2 = ph3 = 0; vibPhase = 0; pitchEnvState = 1.0f; first = true;
        filt.reset(); dc.reset(); os.reset();
    }
    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    float ph1 = 0, ph2 = 0, ph3 = 0, vibPhase = 0, pitchEnvState = 1.0f, vel = 1.0f; bool first = true;
    Svf filt; DcBlocker dc; FastNoise noise; Os2 os; Smoother gainSm;
};
} // namespace

std::unique_ptr<IEngine> createMicroSynth() { return std::make_unique<MicroSynth>(); }
} // namespace dali
