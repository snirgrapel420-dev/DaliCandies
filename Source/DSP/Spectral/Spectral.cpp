// ENGINE E - Spectral: freezes, smears, spectral filtering, bin modulation, randomisation, pitch transformation.
// STFT with sqrt-Hann analysis/synthesis windows, 75% overlap-add.  Latency is managed by priming the
// pipeline at trigger time so audible output is aligned with the event.
#include "DSP/Engine.h"
#include <juce_dsp/juce_dsp.h>

namespace dali
{
namespace
{
class Spectral final : public IEngine
{
public:
    EngineType type() const override { return EngineType::Spectral; }

    void prepare(double s, QualityMode q) override
    {
        sr = s; quality = q;
        order = (q == QualityMode::Eco) ? 9 : 10;
        N = 1 << order; hop = N / 4; mask = N - 1; nb = N / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT>(order);
        frame.assign((size_t) (2 * N), 0.0f); inRing.assign((size_t) N, 0.0f); accum.assign((size_t) N, 0.0f); queue.assign((size_t) hop, 0.0f);
        win.resize((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = std::sqrt(0.5f - 0.5f * std::cos(kTwoPi * (float) i / (float) N));
        mag.assign((size_t) nb, 0.0f); ph.assign((size_t) nb, 0.0f); outMag.assign((size_t) nb, 0.0f); outPh.assign((size_t) nb, 0.0f);
        prevPh.assign((size_t) nb, 0.0f); smearMag.assign((size_t) nb, 0.0f); frozenMag.assign((size_t) nb, 0.0f);
        rndGain.assign((size_t) nb, 1.0f); rndTarget.assign((size_t) nb, 1.0f); tmp.assign((size_t) nb, 0.0f);
        // calibrate the inverse transform scaling empirically (independent of JUCE's convention)
        std::fill(frame.begin(), frame.end(), 0.0f); frame[0] = 1.0f;
        fft->performRealOnlyForwardTransform(frame.data());
        fft->performRealOnlyInverseTransform(frame.data());
        invScale = std::abs(frame[0]) > 1.0e-9f ? 1.0f / frame[0] : 1.0f;
        reset();
    }

    void trigger(const LayerConfig& cfg, float velocity, uint32_t seed, const SourceBuffer* src) override
    {
        reset();
        rng.reseed(seed); noise.seed(seed * 1664525u + 1013904223u);
        vel = velocity;
        src_ = (wantsExternalSource(cfg) && src != nullptr && src->size() > 256) ? src : nullptr;
        for (int k = 0; k < nb; ++k) { rndTarget[(size_t) k] = rng.uniform() * 2.0f; rndGain[(size_t) k] = 1.0f; }
        // prime: run a full window of frames so output starts fully reconstructed
        ControlFrame prime; prime.p = {}; prime.hz = midiToHz(cfg.pitch);
        for (int k = 0; k < kParamsPerLayer; ++k) prime.p[(size_t) k] = cfg.p[(size_t) k];
        for (int f = 0; f < N / hop; ++f) { fillInput(prime); processFrame(prime); }
        qPos = 0; qCount = hop;   // the last primed frame produced a ready block
    }

    void process(const ControlFrame& cf, float* L, float* R, int n) override
    {
        const float g = (0.3f + 0.7f * vel) * linMap(cf.p[15], 0.25f, 1.0f);
        for (int i = 0; i < n; ++i)
        {
            if (qPos >= qCount) { fillInput(cf); processFrame(cf); qPos = 0; qCount = hop; }
            const float x = sanitize(queue[(size_t) qPos++] * g);
            // slight decorrelated width using two taps of the ring (cheap pseudo-stereo)
            L[i] = x; R[i] = x;
        }
        // stereo spread: derive R from a short-delayed copy to avoid a mono sound
        for (int i = 0; i < n; ++i)
        {
            const float d = widthDelay.read(23.0f); widthDelay.push(L[i]);
            R[i] = 0.7f * L[i] - 0.3f * d; L[i] = 0.7f * L[i] + 0.3f * d;
        }
    }

private:
    void reset()
    {
        std::fill(inRing.begin(), inRing.end(), 0.0f); std::fill(accum.begin(), accum.end(), 0.0f); std::fill(queue.begin(), queue.end(), 0.0f);
        std::fill(prevPh.begin(), prevPh.end(), 0.0f); std::fill(smearMag.begin(), smearMag.end(), 0.0f); std::fill(frozenMag.begin(), frozenMag.end(), 0.0f);
        iw = 0; ow = 0; qPos = 0; qCount = 0; srcT = 0; tFrames = 0; captured = false;
        saw1 = saw2 = saw3 = 0; lp = 0; srcPos = 0.0;
        if (widthDelay.buf.empty()) widthDelay.prepare(64); else widthDelay.clear();
    }

    // generate `hop` source samples
    void fillInput(const ControlFrame& cf)
    {
        const auto& p = cf.p;
        const float hz = std::clamp(cf.hz, 20.0f, 6000.0f);
        const float noiseMix = (src_ != nullptr) ? 0.0f : p[9];
        const float lpA = 1.0f - std::exp(-kTwoPi * expMap(p[11], 250.0f, 14000.0f) / (float) sr);
        const float inc = hz / (float) sr;
        for (int i = 0; i < hop; ++i)
        {
            float x;
            if (src_ != nullptr)
            {
                const int sz = src_->size();
                const double step = src_->sampleRate / sr;
                srcPos += step; if (srcPos >= (double) (sz - 2)) srcPos = std::fmod(srcPos, (double) (sz - 2));
                const int i0 = (int) srcPos; const float fr = (float) (srcPos - (double) i0);
                x = src_->data[(size_t) i0] + (src_->data[(size_t) (i0 + 1)] - src_->data[(size_t) i0]) * fr;
            }
            else
            {
                saw1 += inc; saw1 -= std::floor(saw1); saw2 += inc * 1.006f; saw2 -= std::floor(saw2); saw3 += inc * 0.5f; saw3 -= std::floor(saw3);
                const float s1 = 2.0f * saw1 - 1.0f - polyBlep(saw1, inc) * 2.0f;
                const float s2 = 2.0f * saw2 - 1.0f - polyBlep(saw2, inc * 1.006f) * 2.0f;
                const float s3 = 2.0f * saw3 - 1.0f - polyBlep(saw3, inc * 0.5f) * 2.0f;
                const float tonal = 0.45f * s1 + 0.35f * s2 + 0.3f * s3;
                x = lerp(tonal, noise.white(), noiseMix);
                lp += lpA * (x - lp); x = lerp(lp, x, 0.15f);
            }
            inRing[(size_t) iw] = x; iw = (iw + 1) & mask;
        }
    }

    void processFrame(const ControlFrame& cf)
    {
        const auto& p = cf.p;
        // ---- analysis
        for (int i = 0; i < N; ++i) frame[(size_t) i] = inRing[(size_t) ((iw + i) & mask)] * win[(size_t) i];
        std::fill(frame.begin() + N, frame.end(), 0.0f);
        fft->performRealOnlyForwardTransform(frame.data());
        for (int k = 0; k < nb; ++k)
        {
            const float re = frame[(size_t) (2 * k)], im = frame[(size_t) (2 * k + 1)];
            mag[(size_t) k] = std::sqrt(re * re + im * im); ph[(size_t) k] = std::atan2(im, re);
        }
        // ---- parameters
        const float wet = p[0], freeze = p[1], smear = p[2];
        const float fcBin = expMap(p[3], 60.0f, 15000.0f) / ((float) sr / (float) N);
        const float widthOct = linMap(p[4], 0.15f, 4.0f); const bool filterOn = p[4] < 0.97f;
        const float modDepth = p[5], modRate = expMap(p[6], 0.2f, 14.0f), randAmt = p[7];
        const float shiftRatio = std::exp2((p[8] - 0.5f) * 24.0f / 12.0f);
        const float tilt = (p[12] - 0.5f) * 3.0f, holdT = p[13] * 0.35f, phRand = p[14];
        const bool shifted = std::abs(p[8] - 0.5f) > 0.02f;
        const float decay = 0.55f + 0.44f * smear;
        const float tSec = cf.timeSec;

        if (! captured && tSec >= holdT) { frozenMag = mag; captured = true; }
        if ((tFrames & 3) == 0) for (int k = 0; k < nb; ++k) rndTarget[(size_t) k] = rng.uniform() * 2.0f;
        ++tFrames;

        // ---- spectral processing on magnitudes
        for (int k = 0; k < nb; ++k)
        {
            float m = mag[(size_t) k];
            const float f = std::max(1.0f, (float) k) * (float) sr / (float) N;
            if (freeze > 0.001f && captured) m = lerp(m, frozenMag[(size_t) k], freeze);
            if (smear > 0.001f)
            {
                smearMag[(size_t) k] = std::max(m, smearMag[(size_t) k] * decay);
                m = lerp(m, smearMag[(size_t) k], smear);
            }
            if (filterOn)
            {
                const float o = std::log2(std::max(1.0f, (float) k) / std::max(1.0f, fcBin)) / widthOct;
                m *= std::exp(-0.5f * o * o) * 0.9f + 0.1f;
            }
            if (std::abs(tilt) > 0.01f) m *= std::clamp(std::pow(f * 0.001f, tilt * 0.6f), 0.05f, 20.0f);
            if (modDepth > 0.001f) m *= std::max(0.0f, 1.0f + modDepth * std::sin(kTwoPi * (modRate * tSec + (float) k * 0.071f)));
            if (randAmt > 0.001f)
            {
                rndGain[(size_t) k] += 0.35f * (rndTarget[(size_t) k] - rndGain[(size_t) k]);
                m *= lerp(1.0f, rndGain[(size_t) k], randAmt);
            }
            outMag[(size_t) k] = m;
        }
        // pitch transform by bin remapping
        if (shifted)
        {
            std::fill(tmp.begin(), tmp.end(), 0.0f);
            for (int k = 1; k < nb; ++k)
            {
                const float srcBin = (float) k / shiftRatio;
                if (srcBin >= (float) (nb - 1)) break;
                const int a = (int) srcBin; const float fr = srcBin - (float) a;
                tmp[(size_t) k] = outMag[(size_t) a] * (1.0f - fr) + outMag[(size_t) (a + 1)] * fr;
            }
            outMag = tmp;
        }
        // ---- phases
        const bool advance = (freeze > 0.35f && captured) || shifted || smear > 0.5f;
        for (int k = 0; k < nb; ++k)
        {
            if (advance)
            {
                const float adv = kTwoPi * (float) k * (float) hop / (float) N;
                float v = prevPh[(size_t) k] + adv + phRand * kPi * rng.bipolar();
                v = std::fmod(v, kTwoPi);
                outPh[(size_t) k] = v; prevPh[(size_t) k] = v;
            }
            else { outPh[(size_t) k] = lerp(ph[(size_t) k], ph[(size_t) k] + phRand * kPi * rng.bipolar(), 1.0f); prevPh[(size_t) k] = ph[(size_t) k]; }
            // wet/dry: blend toward the untouched analysis magnitude
            outMag[(size_t) k] = lerp(mag[(size_t) k], outMag[(size_t) k], wet);
        }
        // ---- level management: never let the spectral processing add more than +6 dB over the source frame
        {
            double eIn = 0.0, eOut = 0.0;
            for (int k = 0; k < nb; ++k) { eIn += (double) mag[(size_t) k] * mag[(size_t) k]; eOut += (double) outMag[(size_t) k] * outMag[(size_t) k]; }
            if (eOut > 4.0 * eIn + 1.0e-12)
            {
                const float g = (float) std::sqrt((4.0 * eIn + 1.0e-12) / eOut);
                for (int k = 0; k < nb; ++k) outMag[(size_t) k] *= g;
            }
        }
        // ---- synthesis
        for (int k = 0; k < nb; ++k)
        {
            const float re = outMag[(size_t) k] * std::cos(outPh[(size_t) k]), im = outMag[(size_t) k] * std::sin(outPh[(size_t) k]);
            frame[(size_t) (2 * k)] = re; frame[(size_t) (2 * k + 1)] = (k == 0 || k == nb - 1) ? 0.0f : im;
            if (k > 0 && k < nb - 1) { frame[(size_t) (2 * (N - k))] = re; frame[(size_t) (2 * (N - k) + 1)] = -im; }
        }
        fft->performRealOnlyInverseTransform(frame.data());
        const float gOla = invScale * 0.5f;
        for (int i = 0; i < N; ++i) accum[(size_t) ((ow + i) & mask)] += frame[(size_t) i] * win[(size_t) i] * gOla;
        for (int i = 0; i < hop; ++i) { queue[(size_t) i] = accum[(size_t) ((ow + i) & mask)]; accum[(size_t) ((ow + i) & mask)] = 0.0f; }
        ow = (ow + hop) & mask;
    }

    double sr = 44100.0; QualityMode quality = QualityMode::Standard;
    int order = 10, N = 1024, hop = 256, mask = 1023, nb = 513, iw = 0, ow = 0, qPos = 0, qCount = 0, tFrames = 0;
    bool captured = false; float invScale = 1.0f, vel = 1.0f, saw1 = 0, saw2 = 0, saw3 = 0, lp = 0; double srcPos = 0; float srcT = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> frame, inRing, accum, queue, win, mag, ph, outMag, outPh, prevPh, smearMag, frozenMag, rndGain, rndTarget, tmp;
    Rng rng; FastNoise noise; const SourceBuffer* src_ = nullptr; DelayLine widthDelay;
};
} // namespace

std::unique_ptr<IEngine> createSpectral() { return std::make_unique<Spectral>(); }
} // namespace dali
