#pragma once
// Dali Audio neon look: deep violet-black surfaces, magenta / purple / turquoise accents.
#include <juce_gui_basics/juce_gui_basics.h>

namespace dali::ui
{
namespace col
{
inline const juce::Colour bg        { 0xff0a0614 };
inline const juce::Colour bg2       { 0xff10081f };
inline const juce::Colour panel     { 0xff170d2b };
inline const juce::Colour panelHi   { 0xff211340 };
inline const juce::Colour line      { 0xff342058 };
inline const juce::Colour magenta   { 0xffff2bd6 };
inline const juce::Colour purple    { 0xff9a63ff };
inline const juce::Colour turquoise { 0xff1ff5e0 };
inline const juce::Colour text      { 0xffece6ff };
inline const juce::Colour dim       { 0xff9588c0 };
inline const juce::Colour warn      { 0xffffb347 };
inline const juce::Colour bad       { 0xffff4d6d };
inline const juce::Colour good      { 0xff52f7a0 };
}

inline juce::Font font(float h, bool bold = false) { return juce::FontOptions(h, bold ? juce::Font::bold : juce::Font::plain); }

class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool hover, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool hover, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool hover, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(13.0f); }
    juce::Font getPopupMenuFont() override { return font(14.0f); }
    juce::Font getLabelFont(juce::Label& l) override { return l.getProperties().contains("ownFont") ? l.getFont() : font(12.0f); }   // labels with the "ownFont" property keep their own font
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(13.0f, true); }
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    int getDefaultScrollbarWidth() override { return 9; }
};

// glow helpers
void glowRect(juce::Graphics&, juce::Rectangle<float>, juce::Colour, float radius = 6.0f, float strength = 0.5f);
void drawPanel(juce::Graphics&, juce::Rectangle<float>, const juce::String& title, juce::Colour accent = col::purple);
}
