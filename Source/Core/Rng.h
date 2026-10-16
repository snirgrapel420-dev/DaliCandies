#pragma once
// Deterministic random number generation (PCG32 + SplitMix64 seeding).
// Identical seeds produce identical sequences on every platform.
#include <cstdint>
#include <cmath>
#include <span>

namespace dali
{
inline uint64_t splitmix64(uint64_t& x) noexcept
{
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

inline uint32_t hashCombine(uint32_t a, uint32_t b) noexcept
{
    uint64_t s = ((uint64_t) a << 32) | b;
    return (uint32_t) (splitmix64(s) >> 16);
}

class Rng
{
public:
    explicit Rng(uint32_t seed = 1u) noexcept { reseed(seed); }

    void reseed(uint32_t seed) noexcept
    {
        uint64_t s = seed;
        state = splitmix64(s);
        inc   = splitmix64(s) | 1ull;
        next();
    }

    // Child generator derived from this stream's seed + a salt (does not advance this stream).
    Rng derive(uint32_t salt) const noexcept { return Rng(hashCombine((uint32_t) (state >> 32), salt)); }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const uint32_t xorshifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = (uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }           // [0,1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }                                 // [-1,1)
    float range(float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }
    int   rangeInt(int lo, int hiInclusive) noexcept
    {
        const uint32_t span = (uint32_t) (hiInclusive - lo + 1);
        return lo + (int) (next() % span);
    }
    bool  chance(float p) noexcept { return uniform() < p; }

    // Approximate normal (sum of uniforms), mean 0, sd 1.
    float normal() noexcept
    {
        float s = 0.0f;
        for (int i = 0; i < 6; ++i) s += uniform();
        return (s - 3.0f) * 1.41421356f;
    }

    // Value in [0,1] concentrated around `centre` with spread `sd` (clamped).
    float around(float centre, float sd) noexcept
    {
        const float v = centre + normal() * sd;
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    // Weighted pick; returns index. Weights <= 0 are never chosen (unless all are).
    int pickWeighted(std::span<const float> w) noexcept
    {
        float total = 0.0f;
        for (float x : w) total += (x > 0.0f ? x : 0.0f);
        if (total <= 0.0f) return rangeInt(0, (int) w.size() - 1);
        float r = uniform() * total;
        for (size_t i = 0; i < w.size(); ++i)
        {
            const float x = w[i] > 0.0f ? w[i] : 0.0f;
            if (r < x) return (int) i;
            r -= x;
        }
        return (int) w.size() - 1;
    }

private:
    uint64_t state = 0, inc = 1;
};

} // namespace dali
