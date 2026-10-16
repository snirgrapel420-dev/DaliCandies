#pragma once
#include "Core/Types.h"
#include <array>

namespace dali
{
inline const char* scaleName(int s)
{
    static const char* n[] = { "Minor", "Phrygian", "Harmonic Minor", "Minor Pentatonic", "Phrygian Dominant", "Chromatic" };
    return n[std::clamp(s, 0, 5)];
}
inline const char* noteName(int pc)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((pc % 12) + 12) % 12];
}
inline const std::array<bool, 12>& scaleMask(int s)
{
    static const std::array<std::array<bool, 12>, 6> m = {{
        { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },     // natural minor
        { 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0 },     // phrygian
        { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 },     // harmonic minor
        { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 },     // minor pentatonic
        { 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1, 0 },     // phrygian dominant
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 } }};
    return m[(size_t) std::clamp(s, 0, 5)];
}
// Snap a MIDI note toward the nearest note of the scale. strength 0 = untouched, 1 = fully quantised.
inline float quantizeToScale(float midi, int root, int scale, float strength)
{
    const auto& mask = scaleMask(scale);
    const int base = (int) std::floor(midi);
    float best = midi, bestDist = 1.0e9f;
    for (int n = base - 7; n <= base + 8; ++n)
    {
        const int pc = (((n - root) % 12) + 12) % 12;
        if (! mask[(size_t) pc]) continue;
        const float d = std::abs((float) n - midi); if (d < bestDist) { bestDist = d; best = (float) n; }
    }
    return midi + (best - midi) * std::clamp(strength, 0.0f, 1.0f);
}
} // namespace dali
