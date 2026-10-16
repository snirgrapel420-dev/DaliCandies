#include "Pages.h"

namespace dali::ui
{
using namespace pid;

ParamKnob* SequencePage::knob(const char* id, const juce::String& cap, juce::Colour c, juce::Component* parent)
{ knobs.push_back(std::make_unique<ParamKnob>(proc.apvts, id, cap, c)); parent->addAndMakeVisible(*knobs.back()); return knobs.back().get(); }
ParamCombo* SequencePage::combo(const char* id, const juce::String& cap, juce::Component* parent)
{ combos.push_back(std::make_unique<ParamCombo>(proc.apvts, id, cap)); parent->addAndMakeVisible(*combos.back()); return combos.back().get(); }
ParamToggle* SequencePage::toggle(const char* id, const juce::String& t, juce::Component* parent)
{ toggles.push_back(std::make_unique<ParamToggle>(proc.apvts, id, t)); parent->addAndMakeVisible(*toggles.back()); return toggles.back().get(); }

void SequencePage::PatternView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat(); g.setColour(col::bg2); g.fillRoundedRectangle(r, 6.0f); g.setColour(col::line); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    if (! pat) return;
    const double len = std::max(1.0, pat->lengthBeats); const int bpb = 4; const int bars = std::max(1, (int) std::round(len / bpb));
    const float w = r.getWidth() - 16.0f, x0 = r.getX() + 8.0f, top = r.getY() + 18.0f, h = r.getHeight() - 26.0f;
    for (int b = 0; b <= bars; ++b) { const float x = x0 + w * (float) b / (float) bars; g.setColour(b % 4 == 0 ? col::purple.withAlpha(0.6f) : col::line); g.drawVerticalLine((int) x, top, top + h);
        if (b < bars) { g.setColour(col::dim); g.setFont(font(10.0f)); g.drawText(juce::String(b + 1), (int) x + 3, (int) r.getY() + 2, 30, 14, juce::Justification::centredLeft); } }
    const double beatsPerStep = divisionBeats(pat->cfg.division, pat->cfg.feel);
    for (double t = 0; t < len; t += beatsPerStep) { const float x = x0 + w * (float) (t / len); g.setColour(col::line.withAlpha(0.35f)); g.drawVerticalLine((int) x, top + h * 0.85f, top + h); }
    for (size_t i = 0; i < pat->events.size(); ++i)
    {
        const auto& e = pat->events[i]; const float x = x0 + w * (float) (e.position / len);
        const float eh = h * (0.25f + 0.7f * std::clamp(e.velocity, 0.0f, 1.0f)); const auto c = e.muted ? col::dim : (e.slot == 0 ? col::magenta : col::purple);
        const float ew = std::max(3.0f, w * (float) (e.lengthBeats / len) * 0.5f);
        g.setColour(c.withAlpha(e.probability < 0.99f ? 0.45f : 0.9f)); g.fillRoundedRectangle(x, top + h - eh, ew, eh, 2.0f);
        if (e.frozen || e.locked) { g.setColour(col::turquoise); g.drawRoundedRectangle(x - 1.0f, top + h - eh - 1.0f, ew + 2.0f, eh + 2.0f, 2.0f, 1.5f); g.fillEllipse(x + ew * 0.5f - 2.5f, top + h - eh - 8.0f, 5.0f, 5.0f); }
    }
    if (playing) { const float x = x0 + w * (float) (std::fmod(playhead, len) / len); g.setColour(col::turquoise); g.drawVerticalLine((int) x, top - 4.0f, top + h); g.fillRect(x - 3.0f, top - 6.0f, 7.0f, 4.0f); }
}
void SequencePage::PatternView::mouseDown(const juce::MouseEvent& e)
{
    if (! pat)
        return;
    const double len = std::max(1.0, pat->lengthBeats); const float w = (float) getWidth() - 16.0f;
    const double beat = (e.x - 8.0) / w * len; int best = -1; double bd = 0.3 * len / 16.0 + 0.2;
    for (size_t i = 0; i < pat->events.size(); ++i) { const double d = std::abs(pat->events[i].position - beat); if (d < bd) { bd = d; best = (int) i; } }
    if (best >= 0) owner.proc.toggleEventFrozen((size_t) best);
}

SequencePage::SequencePage(CandyProcessor& p) : PageBase(p), exportBar(p)
{
    for (auto* c : { &pPattern, &pMain, &pFeel, &pEuclid, &pExport }) addAndMakeVisible(c);
    pPattern.addAndMakeVisible(view); pPattern.addAndMakeVisible(info); info.setColour(juce::Label::textColourId, col::dim);
    toggle(seqOn, "Sequencer on", &pMain); combo(seqMode, "Mode", &pMain); knob(seqBars, "Bars", col::turquoise, &pMain);
    combo(seqDiv, "Division", &pMain); combo(seqFeel, "Feel", &pMain); knob(seqSwing, "Swing", col::magenta, &pMain); knob(seqProb, "Probability", col::magenta, &pMain);
    toggle(seqFreeRun, "Free run (no transport)", &pMain);
    knob(seqRatchetProb, "Ratchet", col::purple, &pFeel); knob(seqRatchetMax, "Ratchet max", col::purple, &pFeel); knob(seqMicro, "Micro-timing", col::turquoise, &pFeel);
    knob(seqVel, "Velocity", col::magenta, &pFeel); knob(seqVelRange, "Vel. range", col::magenta, &pFeel); knob(seqPitchVar, "Pitch var.", col::turquoise, &pFeel); knob(seqLength, "Event length", col::purple, &pFeel);
    knob(euSteps, "Steps", col::turquoise, &pEuclid); knob(euHits, "Hits", col::turquoise, &pEuclid); knob(euRotate, "Rotate", col::turquoise, &pEuclid);
    knob(seqEvolve, "Evolve amt", col::magenta, &pEuclid); toggle(seqFreeze, "Freeze pattern", &pEuclid);
    pEuclid.addAndMakeVisible(regenerate); pEuclid.addAndMakeVisible(evolveBtn);
    regenerate.onClick = [this] { proc.regeneratePattern(true); }; evolveBtn.onClick = [this] { proc.evolvePattern(); };
    regenerate.setTooltip("Plan a fresh pattern with a new seed (frozen / locked events stay)"); evolveBtn.setTooltip("Phrase evolution: a variation of the current pattern; frozen events are untouched");
    pExport.addAndMakeVisible(exportBar);
    refresh(kChPattern);
}

void SequencePage::resized()
{
    auto r = getLocalBounds().reduced(8);
    pExport.setBounds(r.removeFromBottom(100)); r.removeFromBottom(8); exportBar.setBounds(pExport.content());
    pPattern.setBounds(r.removeFromTop(r.getHeight() * 38 / 100)); r.removeFromTop(8);
    auto pc = pPattern.content(); info.setBounds(pc.removeFromBottom(18)); view.setBounds(pc);
    const int w1 = r.getWidth() * 36 / 100, w2 = r.getWidth() * 36 / 100;
    pMain.setBounds(r.removeFromLeft(w1)); r.removeFromLeft(8); pFeel.setBounds(r.removeFromLeft(w2)); r.removeFromLeft(8); pEuclid.setBounds(r);
    auto c = pMain.content(); auto t = c.removeFromTop(26); toggles[0]->setBounds(t.removeFromLeft(130)); toggles[1]->setBounds(t);
    auto row = c.removeFromTop(48); combos[0]->setBounds(row.removeFromLeft(row.getWidth() / 3 - 4)); row.removeFromLeft(6); combos[1]->setBounds(row.removeFromLeft(row.getWidth() / 2 - 3)); row.removeFromLeft(6); combos[2]->setBounds(row);
    c.removeFromTop(4); const int kw = c.getWidth() / 3; // bars, swing, prob
    knobs[0]->setBounds(c.getX(), c.getY(), kw, 96); knobs[1]->setBounds(c.getX() + kw, c.getY(), kw, 96); knobs[2]->setBounds(c.getX() + 2 * kw, c.getY(), kw, 96);
    auto f = pFeel.content(); const int fw = f.getWidth() / 4;
    for (int i = 0; i < 7; ++i) knobs[(size_t) (3 + i)]->setBounds(f.getX() + (i % 4) * fw, f.getY() + (i / 4) * 100, fw, 96);
    auto e = pEuclid.content(); const int ew = e.getWidth() / 4;
    for (int i = 0; i < 4; ++i) knobs[(size_t) (10 + i)]->setBounds(e.getX() + i * ew, e.getY(), ew, 96);
    auto b = e.withTrimmedTop(100); toggles[2]->setBounds(b.removeFromTop(24)); b.removeFromTop(4); auto br = b.removeFromTop(28); regenerate.setBounds(br.removeFromLeft(br.getWidth() / 2 - 3)); br.removeFromLeft(6); evolveBtn.setBounds(br);
}

void SequencePage::refresh(uint32_t f)
{
    if (f & (kChPattern | kChPreset | kChParamsLoaded | kChSound))
    {
        view.pat = proc.pattern(); view.repaint();
        if (view.pat) info.setText(juce::String(view.pat->events.size()) + " events over " + juce::String(view.pat->cfg.bars) + " bars - click an event to freeze / unfreeze it (frozen events survive regeneration and evolution)."
                                   + (view.pat->cfg.enabled ? "" : "  Sequencer is OFF."), juce::dontSendNotification);
    }
}
void SequencePage::tick()
{
    view.playing = proc.hostPlaying() || proc.apvts.getRawParameterValue(seqFreeRun)->load() > 0.5f; view.playhead = proc.playheadBeats(); if (view.playing) view.repaint();
}
}
