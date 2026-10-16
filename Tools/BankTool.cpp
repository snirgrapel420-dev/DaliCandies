// DaliCandy factory bank generator.  Deterministic: fixed seeds + the generator => the same bank every run
// (sound details may shift when DSP code changes; presets store parameters, not audio).
//   DaliCandyBankTool <output-directory> [presets-per-category=7]
#include "Generator/CandyGenerator.h"
#include "Presets/Preset.h"
#include "Sequencer/Sequencer.h"
#include <juce_gui_basics/juce_gui_basics.h>

using namespace dali;

namespace
{
struct Variant { const char* tag; GenSettings (*make)(Category); SeqMode modeOverride; };
GenSettings base(Category c) { GenSettings g; g.category = c; return g; }

const Variant kVariants[] = {
    { "balanced",  [](Category c) { return base(c); }, SeqMode::Count },
    { "wild",      [](Category c) { auto g = base(c); g.complexity = .9f; g.chaos = .85f; g.motion = .9f; g.musicality = .35f; return g; }, SeqMode::Count },
    { "bright",    [](Category c) { auto g = base(c); g.brightness = .9f; g.complexity = .5f; g.space = .35f; return g; }, SeqMode::Count },
    { "spacious",  [](Category c) { auto g = base(c); g.brightness = .25f; g.space = .9f; g.motion = .6f; return g; }, SeqMode::Count },
    { "musical",   [](Category c) { auto g = base(c); g.musicality = 1.0f; g.chaos = .15f; g.density = .25f; g.scale = 3; return g; }, SeqMode::Count },
    { "dense",     [](Category c) { auto g = base(c); g.density = .85f; g.motion = 1.0f; g.complexity = .8f; return g; }, SeqMode::Count },
    { "dry",       [](Category c) { auto g = base(c); g.space = .05f; g.lengthMode = 0; g.chaos = .3f; return g; }, SeqMode::Count },
};

SeqMode modeFor(Category c)
{
    switch (c)
    {
        case Category::MicroDetails:         return SeqMode::Sparse;
        case Category::PsychedelicMovement:  return SeqMode::PhraseEvolution;
        case Category::OrganicTextures:      return SeqMode::ControlledChaos;
        case Category::MetallicAccents:      return SeqMode::Euclidean;
        case Category::RhythmicGlitches:     return SeqMode::ProbabilitySequence;
        case Category::AtmosphericCandy:     return SeqMode::Sparse;
        case Category::Transitions:          return SeqMode::TransitionBuilder;
        default:                             return SeqMode::CallResponse;
    }
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::fprintf(stderr, "usage: DaliCandyBankTool <output-dir> [per-category]\n"); return 2; }
    juce::File out(argv[1]); out.createDirectory();
    const int perCat = argc > 2 ? std::clamp(std::atoi(argv[2]), 1, 7) : 7;
    for (auto& f : out.findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExtension)) f.deleteFile();

    CandyGenerator gen; int written = 0;
    for (int c = 0; c < kNumCategories; ++c)
    {
        const auto cat = (Category) c; std::set<int> usedArch;
        for (int v = 0; v < perCat; ++v)
        {
            GenSettings gs = kVariants[v].make(cat);
            GenerateOptions go; go.count = 8; go.oversample = 3; go.sampleRate = 48000.0; go.diversity = 0.5f;
            auto batch = gen.generateBatch(gs, 0xDA11u + (uint32_t) (c * 100 + v), go);
            if (batch.size() < 4) { std::fprintf(stderr, "category %d variant %d: only %zu sounds\n", c, v, batch.size()); continue; }
            // prefer an archetype this category has not used yet so the bank covers all recipes
            size_t pick = 0; for (size_t i = 0; i < batch.size(); ++i) if (! usedArch.count(batch[i].archetype)) { pick = i; break; }
            usedArch.insert(batch[pick].archetype);
            Preset p; p.factory = true; p.author = "Dali Audio"; p.category = cat; p.gen = gs; p.createdMs = 0;
            p.setMain(batch[pick]);
            int slot = 1; for (size_t i = 0; i < batch.size() && slot < 4; ++i) if (i != pick) { p.slots[(size_t) slot] = batch[i]; p.slotUsed[(size_t) slot] = true; ++slot; }
            auto& d = p.main();
            p.name = juce::String(categoryName(cat)).upToFirstOccurrenceOf(" ", false, false) + " " + juce::String(v + 1).paddedLeft('0', 2) + " " + d.name;
            d.name = d.name;                      // keep the sound's own name
            p.tags.add(archetypeTable()[(size_t) d.archetype].name); p.tags.add(kVariants[v].tag);
            for (auto& l : d.layers) if (l.active) p.tags.addIfNotAlreadyThere(engineName(l.engine));
            p.tags.add(d.spectral.centroidHz > 4000 ? "bright" : d.spectral.centroidHz < 1200 ? "dark" : "mid");
            p.tags.add(d.durationMs < 200 ? "short" : d.durationMs < 1000 ? "medium" : "long");
            p.tags.removeDuplicates(true);
            p.seq.mode = modeFor(cat); p.seq.seed = 0xC4DD1u + (uint32_t) (c * 10 + v);
            p.seq.bars = (cat == Category::Transitions || cat == Category::AtmosphericCandy) ? 8 : 4;
            p.seq.division = (cat == Category::RhythmicGlitches) ? 3 : 2;
            p.seq.probability = gs.density > .6f ? .85f : .65f; p.seq.pitchVariation = gs.musicality > .6f ? 2.0f : 4.0f;
            p.seq.euclidHits = 3 + (v % 5); p.seq.euclidSteps = 16; p.seq.euclidRotate = v;
            p.seq.lengthBeats = (float) std::clamp(d.durationMs / 1000.0 * 2.0 / 4.0, 0.125, 2.0);
            SeqContext cx; cx.numSlots = slot; cx.density = gs.density; cx.chaos = gs.chaos; cx.negSpaceSensitivity = gs.negSpaceSensitivity;
            p.seq.events = planEvents(p.seq, cx);
            const auto file = out.getChildFile(sanitizeFileName(p.name) + kPresetExtension);
            if (file.replaceWithText(p.toJson())) ++written;
            std::printf("%-60s score %.2f  layers %d  events %zu\n", p.name.toRawUTF8(), d.score, d.activeLayerCount(), p.seq.events.size());
        }
    }
    std::printf("wrote %d presets to %s\n", written, out.getFullPathName().toRawUTF8());
    return written > 0 ? 0 : 1;
}
