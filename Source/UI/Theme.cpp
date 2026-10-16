#include "Theme.h"

namespace dali::ui
{
NeonLookAndFeel::NeonLookAndFeel()
{
    using C = juce::Colour;
    setColour(juce::ResizableWindow::backgroundColourId, col::bg);
    setColour(juce::Label::textColourId, col::text);
    setColour(juce::Slider::textBoxTextColourId, col::text); setColour(juce::Slider::textBoxOutlineColourId, C(0x00000000));
    setColour(juce::Slider::textBoxBackgroundColourId, C(0x00000000)); setColour(juce::Slider::textBoxHighlightColourId, col::purple.withAlpha(0.4f));
    setColour(juce::TextEditor::backgroundColourId, col::bg2); setColour(juce::TextEditor::textColourId, col::text); setColour(juce::TextEditor::outlineColourId, col::line);
    setColour(juce::TextEditor::focusedOutlineColourId, col::turquoise); setColour(juce::CaretComponent::caretColourId, col::turquoise);
    setColour(juce::ComboBox::backgroundColourId, col::bg2); setColour(juce::ComboBox::textColourId, col::text); setColour(juce::ComboBox::outlineColourId, col::line);
    setColour(juce::ComboBox::arrowColourId, col::turquoise);
    setColour(juce::PopupMenu::backgroundColourId, col::panel); setColour(juce::PopupMenu::textColourId, col::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, col::purple.withAlpha(0.55f)); setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::ListBox::backgroundColourId, col::bg2); setColour(juce::ListBox::outlineColourId, col::line); setColour(juce::ListBox::textColourId, col::text);
    setColour(juce::ScrollBar::thumbColourId, col::purple.withAlpha(0.7f));
    setColour(juce::TooltipWindow::backgroundColourId, col::panelHi); setColour(juce::TooltipWindow::textColourId, col::text); setColour(juce::TooltipWindow::outlineColourId, col::purple);
    setColour(juce::TextButton::buttonColourId, col::panelHi); setColour(juce::TextButton::textColourOffId, col::text);
    setColour(juce::AlertWindow::backgroundColourId, col::panel); setColour(juce::AlertWindow::textColourId, col::text); setColour(juce::AlertWindow::outlineColourId, col::purple);
}

void glowRect(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float radius, float strength)
{
    for (int i = 4; i >= 1; --i) { g.setColour(c.withAlpha(strength * 0.07f * (float) (5 - i))); g.drawRoundedRectangle(r.expanded((float) i * 1.4f), radius + (float) i, 1.6f); }
}

void drawPanel(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent)
{
    g.setGradientFill(juce::ColourGradient(col::panelHi, r.getX(), r.getY(), col::panel, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(accent.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour(accent); g.setFont(font(11.0f, true)); g.drawText(title.toUpperCase(), r.withHeight(20.0f).withTrimmedLeft(12.0f), juce::Justification::centredLeft);
        g.setColour(accent.withAlpha(0.5f)); g.fillRect(r.getX() + 12.0f, r.getY() + 20.0f, 22.0f, 1.5f);
    }
}

void NeonLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto b = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(4.0f);
    const float d = std::min(b.getWidth(), b.getHeight()), cx = b.getCentreX(), cy = b.getCentreY(), rad = d * 0.5f;
    const float ang = start + pos * (end - start);
    const bool bip = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroPos = bip ? (float) s.valueToProportionOfLength(0.0) : 0.0f;
    const float a0 = bip ? start + zeroPos * (end - start) : start;
    const auto accent = s.findColour(juce::Slider::rotarySliderFillColourId);
    const auto ring = accent == juce::Colour() || accent.isTransparent() ? col::magenta : accent;
    juce::Path track; track.addCentredArc(cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, start, end, true);
    g.setColour(col::line); g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc(cx, cy, rad - 3.0f, rad - 3.0f, 0.0f, std::min(a0, ang), std::max(a0, ang), true);
    if (s.isEnabled())
    {
        g.setColour(ring.withAlpha(0.22f)); g.strokePath(val, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(ring); g.strokePath(val, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setGradientFill(juce::ColourGradient(col::panelHi.brighter(0.15f), cx, cy - rad * 0.5f, col::bg2, cx, cy + rad, false));
    g.fillEllipse(cx - rad + 8.0f, cy - rad + 8.0f, (rad - 8.0f) * 2.0f, (rad - 8.0f) * 2.0f);
    const float ir = rad - 8.0f; const float px = cx + std::sin(ang) * ir * 0.85f, py = cy - std::cos(ang) * ir * 0.85f;
    g.setColour(s.isEnabled() ? juce::Colours::white : col::dim); g.drawLine(cx + std::sin(ang) * ir * 0.35f, cy - std::cos(ang) * ir * 0.35f, px, py, 2.2f);
}

void NeonLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const auto accent = s.findColour(juce::Slider::trackColourId); const auto c = accent.isTransparent() ? col::turquoise : accent;
    if (style == juce::Slider::LinearVertical)
    {
        const float cx = (float) x + (float) w * 0.5f; g.setColour(col::line); g.fillRoundedRectangle(cx - 2.0f, (float) y, 4.0f, (float) h, 2.0f);
        g.setColour(c); g.fillRoundedRectangle(cx - 2.0f, pos, 4.0f, (float) (y + h) - pos, 2.0f); g.setColour(juce::Colours::white); g.fillEllipse(cx - 5.0f, pos - 5.0f, 10.0f, 10.0f);
        return;
    }
    const float cy = (float) y + (float) h * 0.5f; g.setColour(col::line); g.fillRoundedRectangle((float) x, cy - 2.0f, (float) w, 4.0f, 2.0f);
    const bool bip = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bip ? (float) x + (float) w * (float) s.valueToProportionOfLength(0.0) : (float) x;
    g.setColour(c.withAlpha(0.3f)); g.fillRoundedRectangle(std::min(zero, pos), cy - 4.0f, std::abs(pos - zero), 8.0f, 4.0f);
    g.setColour(c); g.fillRoundedRectangle(std::min(zero, pos), cy - 2.0f, std::abs(pos - zero), 4.0f, 2.0f);
    g.setColour(juce::Colours::white); g.fillEllipse(pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
}

void NeonLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& base, bool hover, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f); const bool on = b.getToggleState();
    const auto accent = b.findColour(juce::TextButton::buttonOnColourId); const auto ac = accent.isTransparent() ? col::magenta : accent;
    auto fill = on ? ac.withAlpha(0.28f) : base; if (hover) fill = fill.brighter(0.15f); if (down) fill = fill.brighter(0.3f);
    g.setColour(fill); g.fillRoundedRectangle(r, 6.0f);
    g.setColour(on ? ac : col::line.brighter(0.2f)); g.drawRoundedRectangle(r, 6.0f, on ? 1.6f : 1.0f);
    if (on) glowRect(g, r, ac, 6.0f, 0.5f);
}

void NeonLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont(getTextButtonFont(b, b.getHeight())); g.setColour(b.getToggleState() ? juce::Colours::white : (b.isEnabled() ? col::text : col::dim));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 1), juce::Justification::centred, 2);
}

void NeonLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool)
{
    const auto r = b.getLocalBounds().toFloat(); const float s = std::min(16.0f, r.getHeight() - 2.0f);
    const auto box = juce::Rectangle<float>(r.getX() + 1.0f, r.getCentreY() - s * 0.5f, s, s);
    const auto accent = b.findColour(juce::ToggleButton::tickColourId); const auto ac = accent.isTransparent() ? col::turquoise : accent;
    g.setColour(col::bg2); g.fillRoundedRectangle(box, 4.0f); g.setColour(b.getToggleState() ? ac : col::line.brighter(hover ? 0.5f : 0.2f)); g.drawRoundedRectangle(box, 4.0f, 1.3f);
    if (b.getToggleState()) { g.setColour(ac); g.fillRoundedRectangle(box.reduced(3.5f), 2.0f); glowRect(g, box, ac, 4.0f, 0.5f); }
    g.setColour(b.isEnabled() ? col::text : col::dim); g.setFont(font(12.0f)); g.drawText(b.getButtonText(), r.withTrimmedLeft(s + 7.0f), juce::Justification::centredLeft);
}

void NeonLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c)
{
    const auto r = juce::Rectangle<float>(0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour(col::bg2); g.fillRoundedRectangle(r, 6.0f); g.setColour(c.hasKeyboardFocus(true) ? col::turquoise : col::line.brighter(0.2f)); g.drawRoundedRectangle(r, 6.0f, 1.0f);
    juce::Path p; const float ax = (float) w - 14.0f, ay = (float) h * 0.5f; p.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(c.isEnabled() ? col::turquoise : col::dim); g.fillPath(p);
}
void NeonLookAndFeel::positionComboBoxText(juce::ComboBox& c, juce::Label& l) { l.setBounds(6, 1, c.getWidth() - 26, c.getHeight() - 2); l.setFont(getComboBoxFont(c)); }
void NeonLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int w, int h) { g.fillAll(col::panel); g.setColour(col::purple.withAlpha(0.6f)); g.drawRect(0, 0, w, h, 1); }
void NeonLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool, int ts, int tsz, bool over, bool)
{
    g.setColour(col::line.withAlpha(0.4f)); g.fillRoundedRectangle((float) x + 2, (float) y, (float) w - 4, (float) h, 3.0f);
    g.setColour(col::purple.withAlpha(over ? 1.0f : 0.7f)); if (h > w) g.fillRoundedRectangle((float) x + 2, (float) ts, (float) w - 4, (float) tsz, 3.0f); else g.fillRoundedRectangle((float) ts, (float) y + 2, (float) tsz, (float) h - 4, 3.0f);
}
}
