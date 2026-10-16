#include "Preset.h"

namespace dali
{
using Obj = juce::DynamicObject;

juce::String sanitizeFileName(const juce::String& n)
{
    juce::String s = n.trim();
    for (auto c : juce::String("\\/:*?\"<>|\n\r\t")) s = s.replaceCharacter(c, '_');
    while (s.startsWithChar('.')) s = s.substring(1);
    if (s.isEmpty()) s = "Preset";
    return s.substring(0, 80);
}

juce::var Preset::toVar() const
{
    auto* o = new Obj();
    o->setProperty("format", "DaliCandyPreset"); o->setProperty("version", kPresetVersion);
    o->setProperty("name", name); o->setProperty("author", author); o->setProperty("description", description);
    o->setProperty("category", (int) category); o->setProperty("tags", tags); o->setProperty("favorite", favorite); o->setProperty("rating", rating);
    o->setProperty("created", createdMs); o->setProperty("mainSlot", mainSlot);
    juce::Array<juce::var> sl;
    for (int i = 0; i < kNumSoundSlots; ++i) { auto* s = new Obj(); s->setProperty("used", slotUsed[(size_t) i]); if (slotUsed[(size_t) i]) s->setProperty("dna", slots[(size_t) i].toVar()); sl.add(juce::var(s)); }
    o->setProperty("slots", sl);
    o->setProperty("seq", seq.toVar()); o->setProperty("gen", gen.toVar()); o->setProperty("macros", vecToVar(macros.data(), kNumMacros));
    return juce::var(o);
}

bool migratePresetVar(juce::var& v, juce::String* error)
{
    auto fail = [&](const juce::String& m) { if (error) *error = m; return false; };
    if (! v.isObject()) return fail("Not a preset document");
    int ver = (int) v.getProperty("version", 1);
    if (ver > kPresetVersion) return fail("Preset was created by a newer version of DaliCandy (format " + juce::String(ver) + ")");
    if (ver < 1) return fail("Unknown preset version");
    if (ver == 1)
    {
        auto* o = v.getDynamicObject(); if (o == nullptr) return fail("Not a preset document");
        // v1 -> v2: single dna becomes slot 0, comma separated tags become an array, macros default
        juce::Array<juce::var> sl;
        for (int i = 0; i < kNumSoundSlots; ++i) { auto* s = new Obj(); s->setProperty("used", i == 0 && o->hasProperty("dna")); if (i == 0 && o->hasProperty("dna")) s->setProperty("dna", o->getProperty("dna")); sl.add(juce::var(s)); }
        o->setProperty("slots", sl); o->removeProperty("dna"); o->setProperty("mainSlot", 0);
        juce::StringArray t; t.addTokens(o->getProperty("tags").toString(), ",", ""); t.trim(); t.removeEmptyStrings();
        o->setProperty("tags", t);
        if (! o->hasProperty("macros")) o->setProperty("macros", vecToVar(std::array<float, kNumMacros> { 0.5f, 0.5f, 0.5f, 0.5f }.data(), kNumMacros));
        o->setProperty("version", 2); ver = 2;
    }
    return true;
}

bool Preset::fromVar(const juce::var& in, Preset& p, juce::String* error)
{
    juce::var v = in;
    if (! migratePresetVar(v, error)) return false;
    p = Preset();
    p.name = v.getProperty("name", "Preset").toString(); p.author = v.getProperty("author", "").toString(); p.description = v.getProperty("description", "").toString();
    p.category = (Category) std::clamp((int) v.getProperty("category", 0), 0, kNumCategories - 1);
    if (auto* a = v["tags"].getArray()) for (auto& t : *a) p.tags.add(t.toString());
    p.favorite = (bool) v.getProperty("favorite", false); p.rating = (int) v.getProperty("rating", 0); p.createdMs = (juce::int64) v.getProperty("created", 0);
    p.mainSlot = std::clamp((int) v.getProperty("mainSlot", 0), 0, kNumSoundSlots - 1);
    if (auto* a = v["slots"].getArray())
        for (int i = 0; i < kNumSoundSlots && i < a->size(); ++i)
            if ((bool) (*a)[i].getProperty("used", false)) p.slotUsed[(size_t) i] = CandyDNA::fromVar((*a)[i]["dna"], p.slots[(size_t) i]);
    if (! p.slotUsed[(size_t) p.mainSlot]) { for (int i = 0; i < kNumSoundSlots; ++i) if (p.slotUsed[(size_t) i]) { p.mainSlot = i; break; } }
    if (! p.slotUsed[(size_t) p.mainSlot]) { if (error) *error = "Preset contains no sound"; return false; }
    SeqConfig::fromVar(v["seq"], p.seq); GenSettings::fromVar(v["gen"], p.gen);
    varToVec(v["macros"], p.macros.data(), kNumMacros);
    for (auto& m : p.macros) m = clamp01(m);
    return true;
}

bool Preset::fromJson(const juce::String& text, Preset& out, juce::String* error)
{
    auto v = juce::JSON::parse(text);
    if (v.isVoid() || ! v.isObject()) { if (error) *error = "File is not valid JSON"; return false; }
    return fromVar(v, out, error);
}
} // namespace dali
