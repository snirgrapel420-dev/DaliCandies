#pragma once
// Versioned preset format (.dalicandy = JSON).
//   v1  development prototype: { dna, seq, gen, tags:"a,b" }            (single sound, comma separated tag string)
//   v2  current:               { slots[8], mainSlot, seq, gen, macros, tags:[...] , ... }
// Older files are migrated on load; files written by a *newer* version are rejected with a clear message.
#include "Core/CandyDNA.h"

namespace dali
{
constexpr int kPresetVersion = 2;
inline const char* kPresetExtension = ".dalicandy";

struct Preset
{
    juce::String name { "Init" }, author { "Dali Audio" }, description;
    Category     category = Category::MicroDetails;
    juce::StringArray tags;
    bool         factory = false, favorite = false;
    int          rating = 0;
    juce::int64  createdMs = 0;
    int          mainSlot = 0;
    std::array<CandyDNA, kNumSoundSlots> slots;
    std::array<bool, kNumSoundSlots>     slotUsed {};
    SeqConfig    seq;
    GenSettings  gen;
    std::array<float, kNumMacros> macros { 0.5f, 0.5f, 0.5f, 0.5f };

    const CandyDNA& main() const { return slots[(size_t) std::clamp(mainSlot, 0, kNumSoundSlots - 1)]; }
    CandyDNA&       main()       { return slots[(size_t) std::clamp(mainSlot, 0, kNumSoundSlots - 1)]; }
    void setMain(const CandyDNA& d) { mainSlot = 0; slots[0] = d; slotUsed[0] = true; }
    std::array<const CandyDNA*, kNumSoundSlots> slotPointers() const
    {   std::array<const CandyDNA*, kNumSoundSlots> a {}; for (int i = 0; i < kNumSoundSlots; ++i) a[(size_t) i] = slotUsed[(size_t) i] ? &slots[(size_t) i] : nullptr; return a; }

    juce::var  toVar() const;
    juce::String toJson() const { return juce::JSON::toString(toVar(), false); }
    static bool fromVar(const juce::var& v, Preset& out, juce::String* error = nullptr);
    static bool fromJson(const juce::String& text, Preset& out, juce::String* error = nullptr);
};

// Brings any supported older document up to kPresetVersion.  Returns false (and error) when unsupported.
bool migratePresetVar(juce::var& v, juce::String* error = nullptr);
juce::String sanitizeFileName(const juce::String& name);
} // namespace dali
