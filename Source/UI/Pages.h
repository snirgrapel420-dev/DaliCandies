#pragma once
#include "Widgets.h"

namespace dali::ui
{
class PageBase : public juce::Component
{
public:
    explicit PageBase(CandyProcessor& p) : proc(p) {}
    virtual void refresh(uint32_t /*changeFlags*/) {}     // model changed (message thread)
    virtual void tick() {}                                // 30 Hz
    CandyProcessor& proc;
};

class GeneratePage : public PageBase
{
public:
    explicit GeneratePage(CandyProcessor&);
    void resized() override;
    void refresh(uint32_t) override;
    void tick() override;
private:
    struct Card : public juce::Component
    {
        Card(GeneratePage& o, int i);
        void resized() override; void paint(juce::Graphics&) override;
        void set(const CandidateView* v, bool selected);
        GeneratePage& owner; int index; WaveView wave; juce::TextButton use { "Use" }, breed { "Breed" }, slot { "Slot" }, play { ">" };
        juce::String name, info; bool has = false;
    };
    void rebuildCards();
    TitledPanel macros { "Intent", col::magenta }, actions { "Shape the sound", col::turquoise }, current { "Current candy", col::purple };
    std::vector<std::unique_ptr<ParamKnob>> knobs; std::vector<std::unique_ptr<ParamCombo>> combos; std::unique_ptr<ParamKnob> lenKnob, negKnob;
    juce::TextButton generate { "GENERATE" }, mutate { "Mutate" }, evolve { "Evolve" }, recall { "Recall" }, audition { "Audition" }, like { "Like" }, fav { "Favourite" }, dislike { "Dislike" }, cancelBtn { "Cancel" };
    std::array<juce::TextButton, kMaxLayers> regen; std::array<juce::ToggleButton, kMaxLayers> lock;
    std::unique_ptr<ModelKnob> mutAmount;
    float mutateAmount = 0.35f; int recallDepth = 1;
    std::vector<std::unique_ptr<Card>> cards; uint64_t lastMainId = 0;
    juce::Label status, nameLabel, infoLabel, tasteLabel; ExportBar exportBar;
    float progressShown = 0.0f;
    void paintOverChildren(juce::Graphics&) override;
};

class SequencePage : public PageBase
{
public:
    explicit SequencePage(CandyProcessor&);
    void resized() override; void refresh(uint32_t) override; void tick() override;
private:
    struct PatternView : public juce::Component
    {
        explicit PatternView(SequencePage& o) : owner(o) {}
        void paint(juce::Graphics&) override; void mouseDown(const juce::MouseEvent&) override;
        SequencePage& owner; std::shared_ptr<const SeqPattern> pat; double playhead = 0; bool playing = false;
    };
    TitledPanel pPattern { "Pattern", col::turquoise }, pMain { "Generator", col::magenta }, pFeel { "Feel & variation", col::purple }, pEuclid { "Euclidean & evolution", col::turquoise }, pExport { "Export & drag", col::magenta };
    std::vector<std::unique_ptr<ParamKnob>> knobs; std::vector<std::unique_ptr<ParamCombo>> combos; std::vector<std::unique_ptr<ParamToggle>> toggles;
    juce::TextButton regenerate { "New pattern" }, evolveBtn { "Evolve phrase" }, applyBtn { "Rebuild (keep frozen)" };
    PatternView view { *this }; ExportBar exportBar; juce::Label info;
    ParamKnob* knob(const char* id, const juce::String& cap, juce::Colour c = col::magenta, juce::Component* parent = nullptr);
    ParamCombo* combo(const char* id, const juce::String& cap, juce::Component* parent);
    ParamToggle* toggle(const char* id, const juce::String& t, juce::Component* parent);
};

class SynthPage : public PageBase
{
public:
    explicit SynthPage(CandyProcessor&);
    void resized() override; void refresh(uint32_t) override; void tick() override;
private:
    void setLayer(int l); void rebuildEngineKnobs(); void pullAll(); void schedulePreview();
    template <typename F> void edit(F&& f) { proc.editMain([this, f](CandyDNA& d) { f(d.layers[(size_t) layer]); }); }
    TitledPanel pLayer { "Layer", col::magenta }, pEngine { "Engine parameters", col::turquoise }, pEnv { "Envelope & level", col::purple }, pFx { "Inserts, sends & FX bus", col::magenta }, pView { "Rendered sound", col::turquoise };
    int layer = 0; CandyDNA cur; std::array<juce::TextButton, kMaxLayers> tabs;
    juce::ToggleButton active { "Active" }, mute { "Mute" }, solo { "Solo" }, locked { "Lock layer" };
    juce::ComboBox engine, insert, modType, delayDiv, pingpong; juce::Label engineLabel, insertLabel, capInsert, capDelay, capMode, capMod;
    std::vector<std::unique_ptr<ModelKnob>> engineKnobs, envKnobs, fxKnobs; std::vector<std::unique_ptr<juce::ToggleButton>> lockBits;
    WaveView wave; juce::Label specInfo; bool previewDirty = true; int previewCountdown = 0;
    struct Bands : juce::Component { void paint(juce::Graphics&) override; std::array<float, 8> v {}; } bands;
    void paintOverChildren(juce::Graphics&) override {}
};

class ModPage : public PageBase
{
public:
    explicit ModPage(CandyProcessor&);
    ~ModPage() override;
    void resized() override; void refresh(uint32_t) override; void tick() override;
    struct Row; struct Editor;
private:
    TitledPanel pMatrix { "Modulation matrix (32 routes)", col::magenta }, pMods { "Modulator", col::turquoise }, pMacros { "Performance macros", col::purple };
    std::vector<std::unique_ptr<ParamKnob>> macroKnobs;
    juce::Viewport viewport; juce::Component* rowsHolder = nullptr; std::vector<Row*> rows;   // rowsHolder is owned by the viewport
    std::unique_ptr<Editor> editor; juce::ComboBox modSel; juce::Label help;
    juce::TextButton clearAll { "Clear all" }, randomise { "Randomise routes" };
};

class AnalysisPage : public PageBase
{
public:
    explicit AnalysisPage(CandyProcessor&);
    void resized() override; void refresh(uint32_t) override; void tick() override;
private:
    struct TrackView : public juce::Component
    {
        explicit TrackView(AnalysisPage& o) : owner(o) {}
        void paint(juce::Graphics&) override; void mouseDown(const juce::MouseEvent&) override;
        AnalysisPage& owner; int stepAt(float x) const;
    };
    struct SugList : public juce::ListBoxModel
    {
        explicit SugList(AnalysisPage& o) : owner(o) {}
        int getNumRows() override; void paintListBoxItem(int, juce::Graphics&, int, int, bool) override; void listBoxItemClicked(int, const juce::MouseEvent&) override;
        AnalysisPage& owner;
    };
    struct Live : public juce::Component { explicit Live(AnalysisPage& o) : owner(o) {} void paint(juce::Graphics&) override; AnalysisPage& owner; };
    struct Mask : public juce::Component { explicit Mask(AnalysisPage& o) : owner(o) {} void paint(juce::Graphics&) override; AnalysisPage& owner; };
    void rebuildSuggestions();
    TitledPanel pLive { "Live analysis", col::turquoise }, pTrack { "Captured track - opportunity map", col::magenta }, pSug { "Placement suggestions (editable)", col::purple }, pMask { "Masking report (estimate)", col::turquoise }, pSrc { "Capture / import", col::magenta };
    TrackView trackView { *this }; Live live { *this }; Mask mask { *this }; SugList sugModel { *this }; juce::ListBox sugList;
    std::unique_ptr<ParamCombo> capLen, source; std::unique_ptr<ParamKnob> bpmKnob, adaptKnob, negKnob; std::unique_ptr<ParamToggle> liveNeg;
    juce::TextButton capture { "Capture" }, cancelCap { "Cancel" }, importBtn { "Import WAV..." }, regen { "Regenerate" }, applyBtn { "Apply to sequence" }, accept { "Accept" }, reject { "Reject" }, lockBtn { "Lock" }, acceptAll { "Accept all" };
    juce::Label info, capStatus; std::shared_ptr<const TrackAnalysis> ana; std::unique_ptr<juce::FileChooser> chooser; int selected = -1;
    std::vector<int> order;
    friend struct SugList; friend struct TrackView;
};
}
