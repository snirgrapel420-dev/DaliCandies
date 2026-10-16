#pragma once
// Small allocation-free DSP building blocks used by every engine and effect.
#include "Types.h"
#include "Rng.h"
#include <vector>
#include <cmath>

namespace dali
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float fastTanh(float x) noexcept
{
    x = std::clamp(x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float softClip(float x) noexcept { return fastTanh(x); }

//------------------------------------------------------------------------------
// One-pole parameter smoother (time constant in ms)
//------------------------------------------------------------------------------
struct Smoother
{
    float value = 0.0f, target = 0.0f, coeff = 0.0f;
    void setup(double sr, float ms) { coeff = ms <= 0.0f ? 0.0f : std::exp(-1.0f / (0.001f * ms * (float) sr)); }
    void reset(float v) { value = target = v; }
    void set(float t) { target = t; }
    float tick() { value = target + (value - target) * coeff; return value; }
};

//------------------------------------------------------------------------------
// DC blocker (1-pole high-pass ~ 10 Hz)
//------------------------------------------------------------------------------
struct DcBlocker
{
    float x1 = 0, y1 = 0, r = 0.9995f;
    void setup(double sr) { r = 1.0f - (kTwoPi * 8.0f / (float) sr); }
    void reset() { x1 = y1 = 0; }
    float process(float x) { const float y = x - x1 + r * y1; x1 = x; y1 = killDenormal(y); return y; }
};

//------------------------------------------------------------------------------
// TPT state-variable filter (Zavalishin).  Stable for all fc < 0.49 fs and Q >= 0.5.
//------------------------------------------------------------------------------
struct Svf
{
    float s1 = 0, s2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    double sr = 44100.0;
    void setSampleRate(double s) { sr = s; }
    void reset() { s1 = s2 = 0; }
    void set(float fc, float q)
    {
        fc = std::clamp(fc, 8.0f, (float) sr * 0.45f);
        q = std::clamp(q, 0.5f, 40.0f);
        g = std::tan(kPi * fc / (float) sr);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    struct Out { float lp, bp, hp; };
    inline Out process(float v0) noexcept
    {
        const float v3 = v0 - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = killDenormal(2.0f * v1 - s1);
        s2 = killDenormal(2.0f * v2 - s2);
        return { v2, v1, v0 - k * v1 - v2 };
    }
    // type: 0 LP, 1 BP, 2 HP, 3 Notch
    inline float tick(float x, int type) noexcept
    {
        auto o = process(x);
        switch (type) { case 0: return o.lp; case 1: return o.bp * k; case 2: return o.hp; default: return o.lp + o.hp; }
    }
};

//------------------------------------------------------------------------------
// RBJ biquad
//------------------------------------------------------------------------------
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    void setLowpass(double sr, float f, float q)
    {
        f = std::clamp(f, 10.0f, (float) sr * 0.45f);
        const float w = kTwoPi * f / (float) sr, c = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
        b0 = (1 - c) * 0.5f / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void setHighpass(double sr, float f, float q)
    {
        f = std::clamp(f, 5.0f, (float) sr * 0.45f);
        const float w = kTwoPi * f / (float) sr, c = std::cos(w), al = std::sin(w) / (2.0f * q), a0 = 1.0f + al;
        b0 = (1 + c) * 0.5f / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void setPeak(double sr, float f, float q, float gainDb)
    {
        f = std::clamp(f, 20.0f, (float) sr * 0.45f);
        const float A = std::pow(10.0f, gainDb / 40.0f), w = kTwoPi * f / (float) sr, c = std::cos(w), al = std::sin(w) / (2.0f * q);
        const float a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
    inline float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = killDenormal(b1 * x - a1 * y + z2);
        z2 = killDenormal(b2 * x - a2 * y);
        return y;
    }
};

//------------------------------------------------------------------------------
// Fixed-capacity fractional delay line (linear interpolation, allocation at prepare only)
//------------------------------------------------------------------------------
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void prepare(int maxSamples)
    {
        int n = 16; while (n < maxSamples + 4) n <<= 1;
        buf.assign((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push(float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read(float delay) const noexcept
    {
        const float d = std::clamp(delay, 1.0f, (float) mask - 2.0f);
        const int di = (int) d; const float f = d - (float) di;
        const float a = buf[(size_t) ((w - 1 - di) & mask)];
        const float b = buf[(size_t) ((w - 2 - di) & mask)];
        return a + (b - a) * f;
    }
};

//------------------------------------------------------------------------------
// Noise
//------------------------------------------------------------------------------
struct FastNoise
{
    uint32_t s = 22222u;
    void seed(uint32_t v) { s = v ? v : 1u; }
    inline float white() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};
struct PinkNoise    // Paul Kellet economy filter
{
    float b0 = 0, b1 = 0, b2 = 0;
    inline float process(float w) noexcept
    {
        b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.22f;
    }
};

//------------------------------------------------------------------------------
// ADSR style envelope with exponential segments; duration-gated note-off.
//------------------------------------------------------------------------------
struct Envelope
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0, sustainLevel = 0;
    float attackInc = 0, decayCoef = 0, releaseCoef = 0;
    int   gateSamples = 0, elapsed = 0;
    double sr = 44100.0;

    void setSampleRate(double s) { sr = s; }
    void trigger(float aMs, float dMs, float s, float rMs, float gateMs)
    {
        sustainLevel = std::clamp(s, 0.0f, 1.0f);
        attackInc  = 1.0f / std::max(1.0f, 0.001f * aMs * (float) sr);
        decayCoef  = std::exp(-4.6f / std::max(1.0f, 0.001f * dMs * (float) sr));   // ~ -40 dB per time
        releaseCoef= std::exp(-4.6f / std::max(1.0f, 0.001f * rMs * (float) sr));
        gateSamples = (int) (0.001f * gateMs * (float) sr);
        elapsed = 0; stage = Attack; level = 0.0f;
    }
    void noteOff() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }
    inline float tick() noexcept
    {
        switch (stage)
        {
            case Idle: return 0.0f;
            case Attack:  level += attackInc; if (level >= 1.0f) { level = 1.0f; stage = Decay; } break;
            case Decay:   level = sustainLevel + (level - sustainLevel) * decayCoef;
                          if (level - sustainLevel < 1.0e-4f) { level = sustainLevel; stage = Sustain; } break;
            case Sustain: break;
            case Release: level *= releaseCoef; if (level < 1.0e-4f) { level = 0.0f; stage = Idle; } break;
        }
        if (stage != Release && stage != Idle && ++elapsed >= gateSamples) stage = Release;
        if (stage == Sustain && sustainLevel <= 1.0e-4f) { level = 0; stage = Idle; }
        return level;
    }
};

//------------------------------------------------------------------------------
// PolyBLEP helpers
//------------------------------------------------------------------------------
inline float polyBlep(float t, float dt) noexcept
{
    if (t < dt)         { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

//------------------------------------------------------------------------------
// IIR Hilbert transformer (Niemitalo 12-pole). Produces ~quadrature pair.
//------------------------------------------------------------------------------
struct Hilbert
{
    struct AP { float a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                inline float process(float x) noexcept { const float y = a2 * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = killDenormal(y); return y; } };
    AP p1[4], p2[4]; float d = 0;
    Hilbert()
    {
        const float c1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        const float c2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { p1[i].a2 = c1[i] * c1[i]; p2[i].a2 = c2[i] * c2[i]; }
    }
    void reset() { for (auto& a : p1) a = AP { a.a2 }; for (auto& a : p2) a = AP { a.a2 }; d = 0; }
    // returns real (delayed in-phase) and imaginary (quadrature) parts
    inline void process(float x, float& re, float& im) noexcept
    {
        float a = x, b = x;
        for (auto& s : p1) a = s.process(a);
        for (auto& s : p2) b = s.process(b);
        re = d; d = a; im = -b;   // sign chosen so that  re*cos(w t) - im*sin(w t)  shifts upward
    }
};

//------------------------------------------------------------------------------
// Sine LFO / phasor helper
//------------------------------------------------------------------------------
struct Phasor
{
    float ph = 0;
    inline float advance(float inc) noexcept { ph += inc; if (ph >= 1.0f) ph -= std::floor(ph); return ph; }
};

inline float panGainL(float pan) { return std::cos((pan * 0.5f + 0.5f) * kPi * 0.5f); }
inline float panGainR(float pan) { return std::sin((pan * 0.5f + 0.5f) * kPi * 0.5f); }

} // namespace dali
