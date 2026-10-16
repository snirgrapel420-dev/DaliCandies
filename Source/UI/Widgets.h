#pragma once
// Reusable GUI widgets: parameter-attached and model-bound knobs/combos/toggles, waveform view, export bar.
#include "Theme.h"
#include "Plugin/CandyProcessor.h"
#include <functional>

namespace dali::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

// Rotary knob with caption, attached to a host parameter.
class ParamKnob : public juce::Component
{
public:
    ParamKnob(APVTS& s, const char* paramId, const juce::String& caption, juce::Colour accent = col::magenta);
    void resized() override;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
private:
    juce::Label label; std::unique_ptr<APVTS::SliderAttachment> att;
};

// Rotary knob bound to a getter/setter on the sound model (no host parameter).
class ModelKnob : public juce::Component
{
public:
    ModelKnob(const juce::String& caption, double lo, double hi, double step, const juce::String& suffix,
              std::function<double()> get, std::function<void(double)> set, juce::Colour accent = col::turquoise, double skewCentre = 0.0);
    void resized() override;
    void pull() { if (! slider.isMouseButtonDown()) slider.setValue(getter(), juce::dontSendNotification); }
    void setCaption(const juce::String& c) { label.setText(c, juce::dontSendNotification); }
    void mouseDown(const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && onContext) onContext(); }
    std::function<void()> onContext;                    // right click
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
private:
    juce::Label label; std::function<double()> getter;
};

class ParamCombo : public juce::Component
{
public:
    ParamCombo(APVTS& s, const char* paramId, const juce::String& caption);
    void resized() override;
    juce::ComboBox box;
private:
    juce::Label label; std::unique_ptr<APVTS::ComboBoxAttachment> att;
};

class ParamToggle : public juce::ToggleButton
{
public:
    ParamToggle(APVTS& s, const char* paramId, const juce::String& text, juce::Colour accent = col::turquoise);
private:
    std::unique_ptr<APVTS::ButtonAttachment> att;
};

class ModelCombo : public juce::Component
{
public:
    ModelCombo(const juce::String& caption, const juce::StringArray& items, std::function<int()> get, std::function<void(int)> set, int firstId = 1);
    void resized() override;
    void pull() { box.setSelectedId(getter() + idBase, juce::dontSendNotification); }
    juce::ComboBox box;
private:
    juce::Label label; std::function<int()> getter; int idBase;
};

class ModelToggle : public juce::ToggleButton
{
public:
    ModelToggle(const juce::String& text, std::function<bool()> get, std::function<void(bool)> set, juce::Colour accent = col::turquoise);
    void pull() { setToggleState(getter(), juce::dontSendNotification); }
private:
    std::function<bool()> getter;
};

// A panel with a title; children are laid out by the owner.
class TitledPanel : public juce::Component
{
public:
    explicit TitledPanel(juce::String t, juce::Colour a = col::purple) : title(std::move(t)), accent(a) {}
    void paint(juce::Graphics& g) override { drawPanel(g, getLocalBounds().toFloat().reduced(1.0f), title, accent); }
    juce::Rectangle<int> content() const { return getLocalBounds().reduced(8).withTrimmedTop(22); }
    juce::String title; juce::Colour accent;
};

// Min/max waveform (pairs). Optional overlay of vertical markers and drag-out of a rendered file.
class WaveView : public juce::Component
{
public:
    void setWave(std::vector<float> w) { wave = std::move(w); repaint(); }
    void setColour(juce::Colour c) { colour = c; }
    void paint(juce::Graphics& g) override;
    std::vector<float> wave; juce::Colour colour = col::turquoise; bool selected = false;
    std::function<void()> onClick;
    std::function<juce::File()> dragProvider;      // returns a real temp file to drag to the DAW / file manager
    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseDrag(const juce::MouseEvent& e) override;
};

// Downsample an audio buffer into min/max pairs (n points)
std::vector<float> minMaxPairs(const juce::AudioBuffer<float>& b, int points);

// Export controls shared by the Generate and Sequence pages.
class ExportBar : public juce::Component
{
public:
    explicit ExportBar(CandyProcessor& p);
    void resized() override;
    ExportFormat format() const;
    juce::File dragFor(CandyProcessor::ExportWhat w) { return proc.makeDragFile(w, format()); }
private:
    void doExport(CandyProcessor::ExportWhat what, const juce::String& ext);
    CandyProcessor& proc;
    juce::ComboBox rate, depth, mix;
    juce::ToggleButton normalize { "Normalize" }, trim { "Trim" };
    juce::TextButton bOne { "One-shot" }, bLoop { "Loop" }, bBars { "Multi-bar" }, bLayers { "Layers" }, bDry { "Dry/Wet" }, bBatch { "All cands" }, bMidi { "MIDI" };
    struct Drag : juce::Component { Drag(ExportBar& o) : owner(o) {} void paint(juce::Graphics&) override; void mouseDrag(const juce::MouseEvent&) override; ExportBar& owner; } dragHandle { *this };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Label dragLabel;
};
}
