#pragma once
// Preset library: factory bank (read-only, supplied by a provider so it can come from embedded binary data)
// plus the user bank on disk.  Message-thread only.
#include "Preset.h"
#include <functional>
#include <map>
#include <set>

namespace dali
{
struct PresetEntry
{
    juce::String id;                // "factory/<name>" or "user/<file stem>"
    juce::String name, author;
    Category category = Category::MicroDetails;
    juce::StringArray tags;
    bool factory = false, favorite = false;
    int rating = 0;
    juce::File file;                // empty for factory presets
    juce::String json;              // factory only
};

class PresetManager
{
public:
    using FactoryProvider = std::function<std::vector<std::pair<juce::String, juce::String>>()>;   // (name, json)
    explicit PresetManager(juce::File userDir = defaultUserDir());
    static juce::File defaultUserDir();
    void setFactoryProvider(FactoryProvider f) { provider_ = std::move(f); }
    void rescan();

    const std::vector<PresetEntry>& entries() const { return entries_; }
    int  indexOf(const juce::String& id) const;
    // query: case-insensitive substring over name / tags / author; category < 0 = any
    std::vector<int> search(const juce::String& query, int category = -1, bool favoritesOnly = false, bool userOnly = false) const;

    bool load(int index, Preset& out, juce::String* error = nullptr) const;
    bool save(Preset p, bool overwrite, juce::String* error = nullptr);          // always to the user bank
    bool rename(int index, const juce::String& newName, juce::String* error = nullptr);   // user presets only
    bool remove(int index, juce::String* error = nullptr);                       // user presets only
    bool importFile(const juce::File& f, juce::String* error = nullptr);
    bool exportTo(int index, const juce::File& dest, juce::String* error = nullptr) const;
    void setFavorite(int index, bool fav);
    void setRating(int index, int rating);
    int  random(uint32_t seed, int category = -1, bool favoritesOnly = false) const;      // deterministic for a seed; -1 if none

    const juce::File& userDir() const { return dir_; }
private:
    void loadFavorites(); void saveFavorites() const;
    juce::File dir_; FactoryProvider provider_; std::vector<PresetEntry> entries_;
    std::set<juce::String> favs_; std::map<juce::String, int> ratings_;
};
} // namespace dali
