#pragma once
// Human taste feedback: transparent weighted preference learning, stored locally.
// Every feature (engine, archetype, category, brightness band, length band, layer count, insert fx)
// carries a weight w; the multiplier applied to candidates / archetype choices is exp(w) (bounded).
// Like = +1, Favorite = +2, Dislike = -1 are spread over the features of the rated sound, with decay.
#include "Core/CandyDNA.h"
#include <map>
#include <string>

namespace dali
{
class TasteModel
{
public:
    void reset();
    void rate(const CandyDNA& d, int signal);                 // signal: +1 like, +2 favorite, -1 dislike
    float multiplier(const CandyDNA& d) const;                // 0.55 .. 1.8
    float archetypeMultiplier(Category c, int archetype) const;
    float engineMultiplier(EngineType e) const;
    int   ratingCount() const { return count_; }
    juce::StringArray describe() const;                       // human readable list of strongest preferences
    juce::var toVar() const;
    void fromVar(const juce::var& v);
    bool saveToFile(const juce::File& f) const;
    bool loadFromFile(const juce::File& f);
    static juce::File defaultFile();
    static std::vector<std::string> featuresOf(const CandyDNA& d);
private:
    std::map<std::string, float> w_; int count_ = 0;
};
} // namespace dali
