#include "TasteModel.h"
#include <algorithm>

namespace dali
{
std::vector<std::string> TasteModel::featuresOf(const CandyDNA& d)
{
    std::vector<std::string> f;
    f.push_back("cat:" + std::to_string((int) d.category));
    f.push_back("arch:" + std::to_string((int) d.category) + ":" + std::to_string(d.archetype));
    for (auto& l : d.layers) if (l.active)
    {
        f.push_back("eng:" + std::to_string((int) l.engine));
        if (l.insert != InsertFx::None) f.push_back("ins:" + std::to_string((int) l.insert));
    }
    f.push_back("layers:" + std::to_string(std::clamp(d.numLayers, 1, 4)));
    const int bright = std::clamp((int) std::floor(std::log2(std::max(100.0f, d.spectral.centroidHz) / 400.0f) * 1.2f), 0, 5);
    f.push_back("bright:" + std::to_string(bright));
    f.push_back("len:" + std::to_string(d.durationMs < 150 ? 0 : d.durationMs < 600 ? 1 : d.durationMs < 1800 ? 2 : 3));
    return f;
}

void TasteModel::reset() { w_.clear(); count_ = 0; }

void TasteModel::rate(const CandyDNA& d, int signal)
{
    const auto f = featuresOf(d); if (f.empty()) return;
    const float step = 0.22f * (float) signal;
    for (auto& k : f) { auto& w = w_[k]; w = std::clamp(w * 0.97f + step, -1.2f, 1.2f); }
    // gentle decay of everything else so that taste can change over time
    for (auto& [k, v] : w_) if (std::find(f.begin(), f.end(), k) == f.end()) v *= 0.995f;
    ++count_;
}

float TasteModel::multiplier(const CandyDNA& d) const
{
    float s = 0; int n = 0;
    for (auto& k : featuresOf(d)) { auto it = w_.find(k); if (it != w_.end()) { s += it->second; ++n; } else ++n; }
    const float avg = n ? s / (float) n : 0.0f;
    return std::clamp(std::exp(avg * 1.6f), 0.55f, 1.8f);
}

float TasteModel::archetypeMultiplier(Category c, int a) const
{
    auto get = [this](const std::string& k) { auto it = w_.find(k); return it == w_.end() ? 0.0f : it->second; };
    return std::clamp(std::exp(1.2f * get("arch:" + std::to_string((int) c) + ":" + std::to_string(a)) + 0.6f * get("cat:" + std::to_string((int) c))), 0.5f, 2.2f);
}
float TasteModel::engineMultiplier(EngineType e) const
{
    auto it = w_.find("eng:" + std::to_string((int) e)); return std::clamp(std::exp(1.0f * (it == w_.end() ? 0.0f : it->second)), 0.6f, 1.9f);
}

juce::StringArray TasteModel::describe() const
{
    std::vector<std::pair<std::string, float>> v(w_.begin(), w_.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return std::abs(a.second) > std::abs(b.second); });
    juce::StringArray out;
    for (size_t i = 0; i < v.size() && i < 8; ++i)
    {
        const auto& k = v[i].first; juce::String name;
        if (k.rfind("eng:", 0) == 0) name = "Engine " + juce::String(engineName((EngineType) std::stoi(k.substr(4))));
        else if (k.rfind("cat:", 0) == 0) name = "Category " + juce::String(categoryName((Category) std::stoi(k.substr(4))));
        else if (k.rfind("ins:", 0) == 0) name = "Insert " + juce::String(insertName((InsertFx) std::stoi(k.substr(4))));
        else if (k.rfind("bright:", 0) == 0) name = "Brightness band " + juce::String(k.substr(7));
        else if (k.rfind("len:", 0) == 0) name = "Length band " + juce::String(k.substr(4));
        else if (k.rfind("layers:", 0) == 0) name = juce::String(k.substr(7)) + " layer(s)";
        else name = "Archetype " + juce::String(k);
        out.add(name + (v[i].second > 0 ? "  +" : "  ") + juce::String(v[i].second, 2));
    }
    return out;
}

juce::var TasteModel::toVar() const
{
    auto* o = new juce::DynamicObject(); auto* w = new juce::DynamicObject();
    for (auto& [k, v] : w_) w->setProperty(juce::Identifier(juce::String(k)), v);
    o->setProperty("version", 1); o->setProperty("count", count_); o->setProperty("weights", juce::var(w));
    return juce::var(o);
}
void TasteModel::fromVar(const juce::var& v)
{
    reset();
    if (auto* w = v["weights"].getDynamicObject())
        for (auto& p : w->getProperties()) w_[p.name.toString().toStdString()] = std::clamp((float) (double) p.value, -1.2f, 1.2f);
    count_ = (int) v["count"];
}
juce::File TasteModel::defaultFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Dali Audio").getChildFile("DaliCandy").getChildFile("taste.json");
}
bool TasteModel::saveToFile(const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText(juce::JSON::toString(toVar()));
}
bool TasteModel::loadFromFile(const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    auto v = juce::JSON::parse(f.loadFileAsString()); if (! v.isObject()) return false; fromVar(v); return true;
}
} // namespace dali
