// Engine factory and offline source-material synthesis (never called from the audio thread).
#include "Engine.h"

namespace dali
{
std::unique_ptr<IEngine> createMicroSynth();
std::unique_ptr<IEngine> createResonator();
std::unique_ptr<IEngine> createGranular();
std::unique_ptr<IEngine> createGlitch();
std::unique_ptr<IEngine> createSpectral();
std::unique_ptr<IEngine> createNoiseMotion();

std::unique_ptr<IEngine> createEngine(EngineType t)
{
    switch (t)
    {
        case EngineType::MicroSynth:  return createMicroSynth();
        case EngineType::Resonator:   return createResonator();
        case EngineType::Granular:    return createGranular();
        case EngineType::Glitch:      return createGlitch();
        case EngineType::Spectral:    return createSpectral();
        case EngineType::NoiseMotion: return createNoiseMotion();
        default: break;
    }
    return createMicroSynth();
}

bool wantsExternalSource(const LayerConfig& cfg)
{
    switch (cfg.engine)
    {
        case EngineType::Granular: return cfg.p[0] >= 0.5f;
        case EngineType::Glitch:   return cfg.p[10] >= 0.5f;
        case EngineType::Spectral: return cfg.p[9] >= 0.9f;      // top of the Source range selects imported audio
        default: return false;
    }
}

namespace
{
// Texture source for the granular engine: harmonic / inharmonic / noisy / FM material.
std::shared_ptr<SourceBuffer> granularSource(const LayerConfig& cfg, double sr)
{
    auto out = std::make_shared<SourceBuffer>();
    out->sampleRate = sr;
    const int len = (int) (0.6 * sr);
    out->data.assign((size_t) len, 0.0f);
    const auto& p = cfg.p;
    const float hz = std::clamp(midiToHz(cfg.pitch + (p[1] - 0.5f) * 48.0f), 30.0f, 4000.0f);
    const float bright = p[14];
    const int   nh = 2 + (int) (bright * 22.0f);
    const int   kind = (int) (std::fmod(p[0] * 7.0f, 4.0f));        // 0 harmonic 1 inharmonic 2 noise 3 fm
    Rng r(cfg.seed ^ 0xA5A5u);
    float amps[32], fr[32], ph[32];
    for (int h = 0; h < nh; ++h)
    {
        fr[h] = kind == 1 ? (float) (h + 1) * (1.0f + 0.37f * r.uniform()) : (float) (h + 1);
        amps[h] = 1.0f / std::pow((float) (h + 1), lerp(1.6f, 0.6f, bright));
        ph[h] = r.uniform();
    }
    FastNoise nz; nz.seed(cfg.seed + 99u);
    Svf lp; lp.setSampleRate(sr); lp.set(expMap(bright, 300.0f, 12000.0f), 1.0f);
    float peak = 1e-6f; const float movePh = r.uniform();
    for (int i = 0; i < len; ++i)
    {
        const float t = (float) i / (float) sr; float x = 0.0f;
        if (kind == 2) x = lp.tick(nz.white(), 0);
        else if (kind == 3)
        {
            const float m = std::sin(kTwoPi * hz * 1.5f * t + movePh);
            x = std::sin(kTwoPi * hz * t + (2.0f + 4.0f * bright) * m * std::exp(-t * 3.0f));
        }
        else
        {
            const float lim = (float) (sr * 0.45) / hz;
            for (int h = 0; h < nh; ++h) { if (fr[h] > lim) break; x += amps[h] * std::sin(kTwoPi * (hz * fr[h] * t + ph[h])); }
        }
        x *= 0.65f + 0.35f * std::sin(kTwoPi * (1.7f * t + movePh));
        out->data[(size_t) i] = x; peak = std::max(peak, std::abs(x));
    }
    const float g = 0.7f / peak;
    for (auto& v : out->data) v *= g;
    return out;
}

// Burst source for the glitch engine: tone burst / noise burst / FM chirp / click train.
std::shared_ptr<SourceBuffer> glitchSource(const LayerConfig& cfg, double sr)
{
    auto out = std::make_shared<SourceBuffer>();
    out->sampleRate = sr;
    const int len = (int) (0.32 * sr);
    out->data.assign((size_t) len, 0.0f);
    const auto& p = cfg.p;
    const float hz = std::clamp(midiToHz(cfg.pitch + (p[11] - 0.5f) * 36.0f), 30.0f, 6000.0f);
    const float bright = p[12];
    const int kind = std::min(3, (int) (std::fmod(p[10] * 7.0f, 4.0f)));
    Rng r(cfg.seed ^ 0x5A5Au); FastNoise nz; nz.seed(cfg.seed + 7u);
    Svf lp; lp.setSampleRate(sr); lp.set(expMap(bright, 400.0f, 14000.0f), 0.9f);
    float peak = 1e-6f; float ph = 0.0f;
    const float clickRate = 8.0f + 40.0f * r.uniform();
    for (int i = 0; i < len; ++i)
    {
        const float t = (float) i / (float) sr; float x = 0.0f;
        const float seg = std::fmod(t * 11.0f, 1.0f);            // repeating envelope -> several distinct slices
        const float env = std::exp(-seg * 5.0f);
        if (kind == 0) { ph += hz / (float) sr; x = std::sin(kTwoPi * ph) * env; }
        else if (kind == 1) x = lp.tick(nz.white(), 0) * env;
        else if (kind == 2) { ph += hz * (1.0f + 3.0f * seg) / (float) sr; x = std::sin(kTwoPi * ph + 2.0f * std::sin(kTwoPi * ph * 2.01f)) * env; }
        else { const float c = std::fmod(t * clickRate, 1.0f); x = (c < 0.02f ? 1.0f : 0.0f) * (nz.white() * 0.5f + 0.5f) + 0.2f * std::sin(kTwoPi * hz * t) * env; }
        out->data[(size_t) i] = x; peak = std::max(peak, std::abs(x));
    }
    const float g = 0.8f / peak;
    for (auto& v : out->data) v *= g;
    return out;
}
} // namespace

std::shared_ptr<SourceBuffer> makeInternalSource(const LayerConfig& cfg, double sampleRate)
{
    switch (cfg.engine)
    {
        case EngineType::Granular: return granularSource(cfg, sampleRate);
        case EngineType::Glitch:   return glitchSource(cfg, sampleRate);
        default: return nullptr;
    }
}
} // namespace dali
