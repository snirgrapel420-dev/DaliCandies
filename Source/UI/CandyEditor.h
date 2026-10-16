#pragma once
// DaliCandy plug-in editor: header (brand, preset browser, page tabs, meter), the Kitchen plus five advanced pages, footer.
#include "Pages.h"

namespace dali::ui
{
class CandyEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit CandyEditor(CandyProcessor&);
    ~CandyEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPage(int index);
    int  currentPage() const { return page; }
    PageBase* pageAt(int i) { return pages[(size_t) i].get(); }
private:
    void timerCallback() override;
    void loadEntry(int entryIndex);
    void stepPreset(int dir);
    void showPresetMenu();
    void showSearchMenu();
    void savePresetDialog();
    juce::Array<int> visibleEntries() const;
    int currentEntry() const;

    struct Meter : juce::Component { explicit Meter(CandyProcessor& p) : proc(p) {} void paint(juce::Graphics&) override; CandyProcessor& proc; float shown = 0.0f, hold = 0.0f; } meter;

    CandyProcessor& proc;
    NeonLookAndFeel laf;
    juce::TooltipWindow tooltips { this, 500 };
    std::array<std::unique_ptr<PageBase>, 6> pages;      // 0 = Kitchen (simple main screen), 1..5 = advanced pages
    std::array<juce::TextButton, 6> tabs;
    juce::TextButton advanced { "ADVANCED" }; bool advancedOn = false;
    void setAdvanced(bool on);
    int page = 0;
    juce::TextButton prev { "<" }, next { ">" }, presetName, save { "Save" }, fav { "Fav" }, rnd { "Random" };
    juce::TextEditor search;
    juce::String searchText; bool favOnly = false;
    juce::Label footerStatus, footerStats;
    std::unique_ptr<juce::Slider> outSlider, wetSlider, drySlider; std::unique_ptr<APVTS::SliderAttachment> outAtt, wetAtt, dryAtt;
    juce::Label outLabel, wetLabel, dryLabel; std::unique_ptr<ParamCombo> quality;
    juce::ComponentBoundsConstrainer constrainer;
    uint32_t lastPresetFlag = 0; juce::String shownPreset;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CandyEditor)
};
}
