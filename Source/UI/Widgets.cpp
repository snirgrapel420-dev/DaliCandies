#include "Widgets.h"

namespace dali::ui
{
static void styleKnob(juce::Slider& s, juce::Colour accent)
{
    s.setColour(juce::Slider::rotarySliderFillColourId, accent);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    s.setDoubleClickReturnValue(true, s.getValue());
}
static void styleCaption(juce::Label& l, const juce::String& t)
{
    l.setText(t, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setColour(juce::Label::textColourId, col::dim); l.setFont(font(11.5f)); l.setInterceptsMouseClicks(false, false);
}

ParamKnob::ParamKnob(APVTS& s, const char* id, const juce::String& caption, juce::Colour accent)
{
    addAndMakeVisible(slider); addAndMakeVisible(label); styleCaption(label, caption);
    att = std::make_unique<APVTS::SliderAttachment>(s, id, slider);
    styleKnob(slider, accent);
    if (auto* p = s.getParameter(id)) slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
}
void ParamKnob::resized() { auto r = getLocalBounds(); label.setBounds(r.removeFromTop(14)); slider.setBounds(r); }

ModelKnob::ModelKnob(const juce::String& caption, double lo, double hi, double step, const juce::String& suffix, std::function<double()> get, std::function<void(double)> set, juce::Colour accent, double skewCentre)
    : getter(std::move(get))
{
    addAndMakeVisible(slider); addAndMakeVisible(label); styleCaption(label, caption);
    slider.setRange(lo, hi, step); if (skewCentre > lo && skewCentre < hi) slider.setSkewFactorFromMidPoint(skewCentre);
    slider.setTextValueSuffix(suffix); styleKnob(slider, accent);
    slider.setValue(getter(), juce::dontSendNotification); slider.setDoubleClickReturnValue(true, getter());
    slider.onValueChange = [this, set = std::move(set)] { set(slider.getValue()); };
    slider.addMouseListener(this, false);
}
void ModelKnob::resized() { auto r = getLocalBounds(); label.setBounds(r.removeFromTop(14)); slider.setBounds(r); }

ParamCombo::ParamCombo(APVTS& s, const char* id, const juce::String& caption)
{
    addAndMakeVisible(box); addAndMakeVisible(label); styleCaption(label, caption); label.setJustificationType(juce::Justification::centredLeft);
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(s.getParameter(id))) box.addItemList(c->choices, 1);
    att = std::make_unique<APVTS::ComboBoxAttachment>(s, id, box);
}
void ParamCombo::resized() { auto r = getLocalBounds(); label.setBounds(r.removeFromTop(14)); box.setBounds(r.removeFromTop(26)); }

ParamToggle::ParamToggle(APVTS& s, const char* id, const juce::String& text, juce::Colour accent)
{
    setButtonText(text); setColour(juce::ToggleButton::tickColourId, accent); att = std::make_unique<APVTS::ButtonAttachment>(s, id, *this);
}

ModelCombo::ModelCombo(const juce::String& caption, const juce::StringArray& items, std::function<int()> get, std::function<void(int)> set, int firstId)
    : getter(std::move(get)), idBase(firstId)
{
    addAndMakeVisible(box); addAndMakeVisible(label); styleCaption(label, caption); label.setJustificationType(juce::Justification::centredLeft);
    box.addItemList(items, firstId); box.setSelectedId(getter() + idBase, juce::dontSendNotification);
    box.onChange = [this, set = std::move(set)] { set(box.getSelectedId() - idBase); };
}
void ModelCombo::resized() { auto r = getLocalBounds(); label.setBounds(r.removeFromTop(14)); box.setBounds(r.removeFromTop(26)); }

ModelToggle::ModelToggle(const juce::String& text, std::function<bool()> get, std::function<void(bool)> set, juce::Colour accent) : getter(std::move(get))
{
    setButtonText(text); setColour(juce::ToggleButton::tickColourId, accent); setToggleState(getter(), juce::dontSendNotification);
    onClick = [this, set = std::move(set)] { set(getToggleState()); };
}

void WaveView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(col::bg2); g.fillRoundedRectangle(r, 6.0f);
    g.setColour(selected ? colour : col::line); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, selected ? 1.6f : 1.0f);
    if (selected) glowRect(g, r.reduced(0.5f), colour, 6.0f, 0.5f);
    const int n = (int) wave.size() / 2; if (n < 2) return;
    const float cy = r.getCentreY(), hh = r.getHeight() * 0.45f, w = r.getWidth() - 8.0f;
    juce::Path p;
    for (int i = 0; i < n; ++i) { const float x = r.getX() + 4.0f + w * (float) i / (float) (n - 1); p.addLineSegment({ x, cy - std::clamp(wave[(size_t) i * 2 + 1], -1.0f, 1.0f) * hh, x, cy - std::clamp(wave[(size_t) i * 2], -1.0f, 1.0f) * hh - 0.6f }, 1.0f); }
    g.setColour(colour.withAlpha(0.25f)); g.strokePath(p, juce::PathStrokeType(3.0f)); g.setColour(colour); g.strokePath(p, juce::PathStrokeType(1.1f));
}
void WaveView::mouseDrag(const juce::MouseEvent& e)
{
    if (! dragProvider || e.getDistanceFromDragStart() < 8) return;
    static bool busy = false; if (busy) return; busy = true;
    auto f = dragProvider();
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
    busy = false;
}

std::vector<float> minMaxPairs(const juce::AudioBuffer<float>& b, int points)
{
    std::vector<float> out((size_t) points * 2, 0.0f); const int n = b.getNumSamples(); if (n == 0 || points < 2) return out;
    for (int i = 0; i < points; ++i)
    {
        const int a = (int) ((int64_t) i * n / points), z = std::max(a + 1, (int) ((int64_t) (i + 1) * n / points));
        float mn = 0, mx = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) { auto* d = b.getReadPointer(c); for (int k = a; k < std::min(z, n); ++k) { mn = std::min(mn, d[k]); mx = std::max(mx, d[k]); } }
        out[(size_t) i * 2] = mn; out[(size_t) i * 2 + 1] = mx;
    }
    return out;
}

//==============================================================================
ExportBar::ExportBar(CandyProcessor& p) : proc(p)
{
    for (auto* c : { &rate, &depth, &mix }) addAndMakeVisible(c);
    rate.addItemList({ "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1); rate.setSelectedId(2);
    depth.addItemList({ "16 bit", "24 bit", "32 bit float" }, 1); depth.setSelectedId(2);
    mix.addItemList({ "Full", "Dry (no FX)", "FX only" }, 1); mix.setSelectedId(1);
    for (auto* b : { &normalize, &trim }) { addAndMakeVisible(b); b->setToggleState(b == &trim, juce::dontSendNotification); }
    struct E { juce::TextButton* b; CandyProcessor::ExportWhat w; const char* ext; };
    for (auto e : { E { &bOne, CandyProcessor::ExportWhat::OneShot, ".wav" }, E { &bLoop, CandyProcessor::ExportWhat::Loop, ".wav" }, E { &bBars, CandyProcessor::ExportWhat::MultiBar, ".wav" },
                    E { &bLayers, CandyProcessor::ExportWhat::Layers, "" }, E { &bDry, CandyProcessor::ExportWhat::DryWet, "" }, E { &bBatch, CandyProcessor::ExportWhat::Candidates, "" }, E { &bMidi, CandyProcessor::ExportWhat::Midi, ".mid" } })
    {
        addAndMakeVisible(e.b); e.b->onClick = [this, e] { doExport(e.w, e.ext); };
    }
    bLayers.setTooltip("One WAV per layer into a folder"); bDry.setTooltip("Writes _dry, _full and _fx files into a folder"); bBatch.setTooltip("Exports every candidate card into a folder");
    addAndMakeVisible(dragHandle); addAndMakeVisible(dragLabel); dragLabel.setText("Drag audio out ->", juce::dontSendNotification); dragLabel.setColour(juce::Label::textColourId, col::dim); dragLabel.setJustificationType(juce::Justification::centredRight);
}

ExportFormat ExportBar::format() const
{
    ExportFormat f; static const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 }; f.sampleRate = rates[std::clamp(rate.getSelectedId() - 1, 0, 3)];
    f.bitDepth = depth.getSelectedId() == 1 ? 16 : depth.getSelectedId() == 3 ? 32 : 24; f.normalize = normalize.getToggleState(); f.trimSilence = trim.getToggleState(); f.dither = true;
    return f;
}

void ExportBar::resized()
{
    auto r = getLocalBounds(); auto top = r.removeFromTop(r.getHeight() / 2).reduced(0, 2); auto bot = r.reduced(0, 2);
    rate.setBounds(top.removeFromLeft(90)); top.removeFromLeft(4); depth.setBounds(top.removeFromLeft(100)); top.removeFromLeft(4); mix.setBounds(top.removeFromLeft(110)); top.removeFromLeft(10);
    normalize.setBounds(top.removeFromLeft(90)); trim.setBounds(top.removeFromLeft(60));
    dragHandle.setBounds(top.removeFromRight(120)); dragLabel.setBounds(top.removeFromRight(120));
    const int w = bot.getWidth() / 7;
    for (auto* b : { &bOne, &bLoop, &bBars, &bLayers, &bDry, &bBatch, &bMidi }) b->setBounds(bot.removeFromLeft(w).reduced(2, 0));
}

void ExportBar::Drag::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f); g.setColour(col::magenta.withAlpha(0.25f)); g.fillRoundedRectangle(r, 6.0f); g.setColour(col::magenta); g.drawRoundedRectangle(r, 6.0f, 1.3f);
    g.setColour(col::text); g.setFont(font(12.0f, true)); g.drawText("DRAG WAV", r, juce::Justification::centred);
}
void ExportBar::Drag::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 8) return;
    static bool busy = false;
    if (busy) return;
    busy = true;
    auto f = owner.dragFor(CandyProcessor::ExportWhat::OneShot);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
    busy = false;
}

void ExportBar::doExport(CandyProcessor::ExportWhat what, const juce::String& ext)
{
    using W = CandyProcessor::ExportWhat;
    const auto fmt = format(); const ExportMix m = mix.getSelectedId() == 2 ? ExportMix::Dry : mix.getSelectedId() == 3 ? ExportMix::FxOnly : ExportMix::Full;
    const bool isDir = ext.isEmpty(); const auto name = sanitizeFileName(proc.mainSound().name);
    const auto start = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser>(isDir ? "Choose export folder" : "Export", isDir ? start : start.getChildFile(name + (what == W::Loop ? "_loop" : what == W::MultiBar ? "_bars" : "") + ext), isDir ? "" : (ext == ".mid" ? "*.mid" : "*.wav"));
    const int flags = isDir ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;
    chooser->launchAsync(flags, [this, what, fmt, m](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f != juce::File()) proc.exportAsync(what, f, fmt, m); });
}
}
