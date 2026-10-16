// ENGINE B - Resonator: metallic ticks, hollow resonances, springs, alien percussion.
// Modal bank of damped resonators (coupled-form), inharmonic partial sets,
// excitation noise, tuned comb with feedback, ring modulation, frequency shifting.
#include "DSP/Engine.h"

namespace dali
{
namespace
{
constexpr int kMaxModes = 16;

class Resonator final : public IEngine
{
public:
    EngineType type() const override { return EngineType::Resonator; }

    void prepare(double s, QualityMode q) override
    {
        sr = s; quality = q;
        comb.prepare((int) (sr / 20.0) + 8);       // lowest comb pitch 20 Hz
        dc.setup(sr);
        reset();
    }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer*) override
    {
        reset();
        Rng r(seed);
        for (int i = 0; i < kMaxModes; ++i) { jitter[i] = r.bipolar(); }
        noise.seed(seed * 747796405u + 1u);
        vel = velocity;
        excitePos = 0;
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        const auto& p = cf.p;
        const float f0 = std::clamp(cf.hz, 20.0f, (float) sr * 0.30f);
        const int   modes = 4 + (int) std::round(p[12] * 12.0f);
        const float inh = p[1];
        const float slope = lerp(2.2f, 0.25f, p[2]);                       // brightness -> amplitude roll-off
        const float t60 = expMap(p[3], 0.02f, 3.0f);                        // damping -> decay time (low partials)
        const int   exciteLen = std::max(2, (int) (expMap(p[6], 0.4f, 30.0f) * 0.001f * (float) sr));
        // exciter colour / level are defined at 44.1 kHz and mapped so the sound is the same at every sample rate
        const float srRatio = 44100.0f / (float) sr;
        const float exciteColor = 1.0f - std::pow(1.0f - linMap(p[5], 0.05f, 0.95f), srRatio);
        const float noiseNorm = std::sqrt(srRatio);
        const float fb = p[4] * 0.93f, combMix = p[11];
        const float ringRatio = expMap(p[7], 0.25f, 8.0f), ringAmt = p[8];
        const float shiftHz = (p[9] - 0.5f) * 1000.0f, shiftMix = p[10];
        const float spread = p[13], pos = linMap(p[14], 0.07f, 0.5f), spring = p[15];

        // structure morph: harmonic -> bar -> bell -> membrane
        static const float sets[4][8] = {
            { 1, 2, 3, 4, 5, 6, 7, 8 },
            { 1, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.46f, 30.8f },
            { 1, 2.0f, 2.4f, 3.0f, 4.07f, 4.16f, 5.43f, 6.8f },
            { 1, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.5f }
        };
        const float sp = p[0] * 3.0f; const int si = std::min(2, (int) sp); const float sf = sp - (float) si;
        const float modeNorm = 1.0f / std::sqrt(std::max(1.0f, (float) modes / 6.0f));      // keep level stable as the mode count grows
        const bool rebuild = (++blockCount % 2) == 1 || !built;

        if (rebuild)
        {
            for (int m = 0; m < modes; ++m)
            {
                const int mi = m & 7; const int oct = m >> 3;
                float ratio = lerp(sets[si][mi], sets[si + 1][mi], sf) * (float) (oct + 1);
                ratio *= 1.0f + inh * 0.06f * jitter[m];
                // spring: dispersion chirp that decays with time
                const float chirp = 1.0f + spring * 0.35f * (float) (m + 1) * 0.08f * std::exp(-cf.timeSec * 18.0f);
                float f = f0 * ratio * chirp;
                if (f > std::min((float) sr * 0.45f, 19800.0f)) { a1[m] = 0; a2[m] = 0; gain[m] = 0; continue; }       // no ultrasonic partials: same level at every sample rate
                const float tau = t60 / (1.0f + 0.55f * (float) m * (1.0f - p[2] * 0.6f));          // upper partials die faster
                const float rr = std::exp(-6.9f / (tau * (float) sr));
                a1[m] = 2.0f * rr * std::cos(kTwoPi * f / (float) sr);
                a2[m] = -rr * rr;
                const float posAmp = std::abs(std::sin(kPi * pos * (float) (m + 1))) + 0.15f;
                // impulse response of the coupled form peaks at 1/sin(w): normalise so every mode strikes at the same level
                gain[m] = std::pow((float) (m + 1), -slope) * posAmp * std::sin(kTwoPi * f / (float) sr) * 0.45f * modeNorm;
                pans[m] = (m & 1 ? 1.0f : -1.0f) * spread * (0.4f + 0.6f * ((float) m / (float) kMaxModes));
            }
            for (int m = modes; m < kMaxModes; ++m) { a1[m] = a2[m] = gain[m] = 0; }
            built = true;
        }

        const float combDelay = (float) sr / std::max(20.0f, f0);
        const float ringHz = f0 * ringRatio;
        for (int i = 0; i < n; ++i)
        {
            // excitation: short noise burst through one-pole colour filter + initial click
            float ex = 0.0f;
            if (excitePos < exciteLen)
            {
                const float w = 1.0f - (float) excitePos / (float) exciteLen;
                lpState += exciteColor * (noise.white() - lpState);
                ex = lpState * w * w * noiseNorm + (excitePos == 0 ? 0.8f : 0.0f);
                ++excitePos;
            }
            ex *= (0.3f + 0.7f * vel);

            float sl = 0, sr_ = 0;
            for (int m = 0; m < modes; ++m)
            {
                const float y = a1[m] * y1[m] + a2[m] * y2[m] + ex * gain[m];
                y2[m] = y1[m]; y1[m] = killDenormal(y);
                const float gl = 0.5f * (1.0f - pans[m]), gr = 0.5f * (1.0f + pans[m]);
                sl += y * gl * 2.0f; sr_ += y * gr * 2.0f;
            }
            float mono = 0.5f * (sl + sr_);

            // tuned comb
            if (combMix > 0.001f)
            {
                const float d = comb.read(combDelay);
                const float c = mono + fb * d;
                comb.push(killDenormal(sanitize(c)));
                sl = lerp(sl, sl + d * 0.5f, combMix); sr_ = lerp(sr_, sr_ + d * 0.5f, combMix);
            }
            else comb.push(0.0f);

            // ring mod
            if (ringAmt > 0.001f)
            {
                ringPh += ringHz / (float) sr; if (ringPh >= 1.0f) ringPh -= 1.0f;
                const float m = std::sin(kTwoPi * ringPh);
                sl = lerp(sl, sl * m, ringAmt); sr_ = lerp(sr_, sr_ * m, ringAmt);
            }
            // frequency shift (Hilbert)
            if (shiftMix > 0.001f)
            {
                float re, im; hil.process(0.5f * (sl + sr_), re, im);
                shPh += shiftHz / (float) sr; shPh -= std::floor(shPh);
                const float sh = re * std::cos(kTwoPi * shPh) - im * std::sin(kTwoPi * shPh);
                sl = lerp(sl, sh, shiftMix); sr_ = lerp(sr_, sh, shiftMix);
            }
            L[i] = sanitize(dc.process(sl)) * 0.35f;
            R[i] = sanitize(sr_) * 0.35f;
        }
    }

private:
    void reset()
    {
        for (int m = 0; m < kMaxModes; ++m) { y1[m] = y2[m] = 0; a1[m] = a2[m] = gain[m] = pans[m] = 0; jitter[m] = 0; }
        comb.clear(); hil.reset(); dc.reset(); lpState = 0; ringPh = shPh = 0; built = false; blockCount = 0; excitePos = 1 << 30;
    }
    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    float a1[kMaxModes], a2[kMaxModes], gain[kMaxModes], pans[kMaxModes], y1[kMaxModes], y2[kMaxModes], jitter[kMaxModes];
    DelayLine comb; Hilbert hil; DcBlocker dc; FastNoise noise;
    float lpState = 0, ringPh = 0, shPh = 0, vel = 1.0f; int excitePos = 1 << 30; bool built = false; int blockCount = 0;
};
} // namespace

std::unique_ptr<IEngine> createResonator() { return std::make_unique<Resonator>(); }
} // namespace dali
