#include "PresetManager.h"
#include "Core/Rng.h"

namespace dali
{
using Obj = juce::DynamicObject;

juce::File PresetManager::defaultUserDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Dali Audio").getChildFile("DaliCandy").getChildFile("Presets");
}
PresetManager::PresetManager(juce::File d) : dir_(std::move(d)) {}

void PresetManager::loadFavorites()
{
    favs_.clear(); ratings_.clear();
    auto v = juce::JSON::parse(dir_.getChildFile("library.json").loadFileAsString());
    if (auto* a = v["favorites"].getArray()) for (auto& x : *a) favs_.insert(x.toString());
    if (auto* o = v["ratings"].getDynamicObject()) for (auto& p : o->getProperties()) ratings_[p.name.toString()] = (int) p.value;
}
void PresetManager::saveFavorites() const
{
    auto* o = new Obj(); juce::Array<juce::var> f; for (auto& s : favs_) f.add(s);
    auto* r = new Obj(); for (auto& [k, v] : ratings_) r->setProperty(juce::Identifier(k), v);
    o->setProperty("favorites", f); o->setProperty("ratings", juce::var(r));
    dir_.createDirectory(); dir_.getChildFile("library.json").replaceWithText(juce::JSON::toString(juce::var(o)));
}

static bool fill(PresetEntry& e, const juce::String& json)
{
    Preset p; if (! Preset::fromJson(json, p)) return false;
    e.name = p.name; e.author = p.author; e.category = p.category; e.tags = p.tags; e.rating = p.rating; e.favorite = p.favorite; return true;
}

void PresetManager::rescan()
{
    entries_.clear(); loadFavorites();
    if (provider_)
        for (auto& [name, json] : provider_())
        {
            PresetEntry e; e.id = "factory/" + name; e.factory = true; e.json = json;
            if (! fill(e, json)) continue;
            e.favorite = favs_.count(e.id) > 0; if (auto it = ratings_.find(e.id); it != ratings_.end()) e.rating = it->second;
            entries_.push_back(std::move(e));
        }
    if (dir_.isDirectory())
        for (auto& f : dir_.findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExtension))
        {
            PresetEntry e; e.id = "user/" + f.getFileNameWithoutExtension(); e.file = f;
            if (! fill(e, f.loadFileAsString())) continue;                  // corrupt files are skipped, never crash
            e.favorite = favs_.count(e.id) > 0 || e.favorite; if (auto it = ratings_.find(e.id); it != ratings_.end()) e.rating = it->second;
            entries_.push_back(std::move(e));
        }
    std::stable_sort(entries_.begin(), entries_.end(), [](const PresetEntry& a, const PresetEntry& b)
    { if (a.factory != b.factory) return a.factory; if (a.category != b.category) return a.category < b.category; return a.name.compareIgnoreCase(b.name) < 0; });
}

int PresetManager::indexOf(const juce::String& id) const { for (size_t i = 0; i < entries_.size(); ++i) if (entries_[i].id == id) return (int) i; return -1; }

std::vector<int> PresetManager::search(const juce::String& q, int category, bool favOnly, bool userOnly) const
{
    std::vector<int> r; const auto qq = q.trim();
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        auto& e = entries_[i];
        if (category >= 0 && (int) e.category != category) continue;
        if (favOnly && ! e.favorite) continue;
        if (userOnly && e.factory) continue;
        if (qq.isNotEmpty())
        {
            bool hit = e.name.containsIgnoreCase(qq) || e.author.containsIgnoreCase(qq) || juce::String(categoryName(e.category)).containsIgnoreCase(qq);
            for (auto& t : e.tags) hit |= t.containsIgnoreCase(qq);
            if (! hit) continue;
        }
        r.push_back((int) i);
    }
    return r;
}

bool PresetManager::load(int i, Preset& out, juce::String* err) const
{
    if (i < 0 || i >= (int) entries_.size()) { if (err) *err = "No such preset"; return false; }
    auto& e = entries_[(size_t) i];
    const juce::String text = e.factory ? e.json : e.file.loadFileAsString();
    if (! Preset::fromJson(text, out, err)) return false;
    out.factory = e.factory; out.favorite = e.favorite; out.rating = e.rating; return true;
}

bool PresetManager::save(Preset p, bool overwrite, juce::String* err)
{
    p.factory = false; if (p.createdMs == 0) p.createdMs = juce::Time::currentTimeMillis();
    if (! dir_.createDirectory().wasOk()) { if (err) *err = "Cannot create the preset folder"; return false; }
    const auto f = dir_.getChildFile(sanitizeFileName(p.name) + kPresetExtension);
    if (f.existsAsFile() && ! overwrite) { if (err) *err = "A preset with this name already exists"; return false; }
    if (! f.replaceWithText(p.toJson())) { if (err) *err = "Could not write " + f.getFullPathName(); return false; }
    rescan(); return true;
}

bool PresetManager::rename(int i, const juce::String& n, juce::String* err)
{
    if (i < 0 || i >= (int) entries_.size() || entries_[(size_t) i].factory) { if (err) *err = "Only user presets can be renamed"; return false; }
    Preset p; if (! load(i, p, err)) return false;
    const auto old = entries_[(size_t) i].file; p.name = n;
    const auto f = dir_.getChildFile(sanitizeFileName(n) + kPresetExtension);
    if (f.existsAsFile() && f != old) { if (err) *err = "A preset with this name already exists"; return false; }
    if (! f.replaceWithText(p.toJson())) { if (err) *err = "Could not write file"; return false; }
    if (f != old) old.deleteFile();
    rescan(); return true;
}

bool PresetManager::remove(int i, juce::String* err)
{
    if (i < 0 || i >= (int) entries_.size() || entries_[(size_t) i].factory) { if (err) *err = "Only user presets can be deleted"; return false; }
    if (! entries_[(size_t) i].file.deleteFile()) { if (err) *err = "Could not delete file"; return false; }
    favs_.erase(entries_[(size_t) i].id); ratings_.erase(entries_[(size_t) i].id); saveFavorites(); rescan(); return true;
}

bool PresetManager::importFile(const juce::File& f, juce::String* err)
{
    if (! f.existsAsFile()) { if (err) *err = "File not found"; return false; }
    Preset p; if (! Preset::fromJson(f.loadFileAsString(), p, err)) return false;
    auto base = sanitizeFileName(p.name); int n = 1; juce::String nm = p.name;
    while (dir_.getChildFile(sanitizeFileName(nm) + kPresetExtension).existsAsFile()) nm = p.name + " (" + juce::String(++n) + ")";
    p.name = nm; return save(p, false, err);
}

bool PresetManager::exportTo(int i, const juce::File& dest, juce::String* err) const
{
    Preset p; if (! load(i, p, err)) return false;
    if (! dest.getParentDirectory().createDirectory().wasOk() || ! dest.replaceWithText(p.toJson())) { if (err) *err = "Could not write " + dest.getFullPathName(); return false; }
    return true;
}

void PresetManager::setFavorite(int i, bool fav)
{
    if (i < 0 || i >= (int) entries_.size()) return;
    auto& e = entries_[(size_t) i]; e.favorite = fav; if (fav) favs_.insert(e.id); else favs_.erase(e.id); saveFavorites();
}
void PresetManager::setRating(int i, int r)
{
    if (i < 0 || i >= (int) entries_.size()) return;
    auto& e = entries_[(size_t) i]; e.rating = std::clamp(r, 0, 5); ratings_[e.id] = e.rating; saveFavorites();
}

int PresetManager::random(uint32_t seed, int category, bool favOnly) const
{
    auto idx = search({}, category, favOnly); if (idx.empty()) return -1;
    Rng r(seed); return idx[(size_t) r.rangeInt(0, (int) idx.size() - 1)];
}
} // namespace dali
