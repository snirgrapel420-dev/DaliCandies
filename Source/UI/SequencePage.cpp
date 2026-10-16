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
    // what the track does: loudness per 1/16 step (grey) and hits (ticks)
    if (ana && ana->valid && ana->numSteps > 0)
        for (int s = 0; s < ana->numSteps; ++s)
        {
            const double t = (double) s * 0.25; if (t >= len) break;
            const float x = x0 + w * (float) (t / len), sw = std::max(1.0f, w * (float) (0.25 / len));
            const float rh = h * 0.8f * std::clamp(ana->rms[(size_t) s] * 1.4f, 0.0f, 1.0f);
            g.setColour(juce::Colour(0xff6c6488).withAlpha(0.45f)); g.fillRect(x, top + h - rh, sw, rh);
            if (ana->onset[(size_t) s] > 0.45f) { g.setColour(juce::Colour(0xffb8b0d8).withAlpha(0.7f)); g.fillRect(x, top + h - 3.0f, sw, 3.0f); }
        }
    for (int b = 0; b <= bars; ++b) { const float x = x0 + w * (float) b / (float) bars; g.setColour(b % 4 == 0 ? col::purple.withAlpha(0.6f) : col::line); g.drawVerticalLine((int) x, top, top + h);
        if (b < bars) { g.setColour(col::dim); g.setFont(font(10.0f)); g.drawText(juce::String(b + 1), (int) x + 3, (int) r.getY() + 2, 30, 14, juce::Justification::centredLeft); } }
    for (size_t i = 0; i < pat->events.size(); ++i)
    {
        const auto& e = pat->events[i]; const float x = x0 + w * (float) (e.position / len);
        const float eh = h * (0.25f + 0.7f * std::clamp(e.velocity, 0.0f, 1.0f)); const auto c = e.muted ? col::dim : juce::Colour::fromHSV((float) (e.slot % kNumSoundSlots) / (float) kNumSoundSlots, 0.65f, 1.0f, 1.0f);
        const float ew = std::max(5.0f, w * (float) (e.lengthBeats / len) * 0.5f);
        g.setColour(c.withAlpha(e.probability < 0.99f ? 0.45f : 0.92f)); g.fillRoundedRectangle(x, top + h - eh, ew, eh, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.7f)); g.setFont(font(9.5f, true)); g.drawText(juce::String(e.slot + 1), juce::Rectangle<float>(x, top + h - eh, std::max(ew, 10.0f), 12.0f), juce::Justification::centred);
        if (e.locked) { g.setColour(col::turquoise); g.drawRoundedRectangle(x - 1.0f, top + h - eh - 1.0f, ew + 2.0f, eh + 2.0f, 2.0f, 1.5f); }
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
    for (auto* c : { &pSimple, &pPattern, &pMain, &pFeel, &pEuclid, &pExport }) addAndMakeVisible(c);
    pPattern.addAndMakeVisible(view); pPattern.addAndMakeVisible(info); info.setColour(juce::Label::textColourId, col::dim);
    // ---- simple controls --------------------------------------------------------------------------------------------------
    for (auto* b : { &build, &variation, &listenBtn }) pSimple.addAndMakeVisible(b);
    pSimple.addAndMakeVisible(moreBtn);
    build.setColour(juce::TextButton::buttonColourId, col::magenta.withAlpha(0.45f)); listenBtn.setColour(juce::TextButton::buttonColourId, col::purple.withAlpha(0.4f));
    build.setTooltip("Place sounds from the eight plates into the gaps of the analysed track. Each spot gets the plate that fits it: free frequency band, enough room until the next hit, transitions before section changes.");
    variation.setTooltip("Same track, same plates, a different arrangement"); listenBtn.setTooltip("Capture the track now (needs the host playing and an audio input), analyse it, cook plates aimed at its gaps and build the pattern");
    build.onClick = [this] { proc.buildPattern(false); }; variation.onClick = [this] { proc.buildPattern(true); }; listenBtn.onClick = [this] { proc.listen(); };
    moreBtn.onClick = [this] { showMore = ! showMore; moreBtn.setToggleState(showMore, juce::dontSendNotification); for (auto* c : { &pMain, &pFeel, &pEuclid }) c->setVisible(showMore); resized(); };
    moreBtn.setClickingTogglesState(false);
    struct K { const char* id; const char* cap; juce::Colour c; const char* tip; };
    for (auto k : { K { pid::density, "AMOUNT", col::magenta, "How many sounds are placed" }, K { pid::variety, "VARIETY", col::purple, "0 = always the best-fitting plate, 100 = spread over all eight" },
                    K { pid::seqEvolve, "SURPRISE", col::turquoise, "How much a new variation changes" }, K { pid::seqSwing, "SWING", col::magenta, "Push off-beat sounds late" },
                    K { pid::seqMicro, "HUMANISE", col::purple, "Tiny timing offsets (ms)" } })
    { topKnobs.push_back(std::make_unique<ParamKnob>(proc.apvts, k.id, k.cap, k.c)); topKnobs.back()->slider.setTooltip(k.tip); pSimple.addAndMakeVisible(*topKnobs.back()); }
    playTop = std::make_unique<ParamToggle>(proc.apvts, pid::seqOn, "PLAY SEQUENCE", col::magenta); freeTop = std::make_unique<ParamToggle>(proc.apvts, pid::seqFreeRun, "Free run (no transport)");
    pSimple.addAndMakeVisible(*playTop); pSimple.addAndMakeVisible(*freeTop);
    playTop->setTooltip("The sequence only plays while this is on and the host transport runs. It starts OFF: nothing plays until you build a pattern.");
    // ---- full toolbox (hidden until MORE CONTROLS) ---------------------------------------------------------------------------
    toggle(seqOn, "Sequencer on", &pMain); combo(seqMode, "Mode", &pMain); knob(seqBars, "Bars", col::turquoise, &pMain);
    combo(seqDiv, "Division", &pMain); combo(seqFeel, "Feel", &pMain); knob(seqSwing, "Swing", col::magenta, &pMain); knob(seqProb, "Probability", col::magenta, &pMain);
    toggle(seqFreeRun, "Free run (no transport)", &pMain);
    knob(seqRatchetProb, "Ratchet", col::purple, &pFeel); knob(seqRatchetMax, "Ratchet max", col::purple, &pFeel); knob(seqMicro, "Micro-timing", col::turquoise, &pFeel);
    knob(seqVel, "Velocity", col::magenta, &pFeel); knob(seqVelRange, "Vel. range", col::magenta, &pFeel); knob(seqPitchVar, "Pitch var.", col::turquoise, &pFeel); knob(seqLength, "Event length", col::purple, &pFeel);
    knob(euSteps, "Steps", col::turquoise, &pEuclid); knob(euHits, "Hits", col::turquoise, &pEuclid); knob(euRotate, "Rotate", col::turquoise, &pEuclid);
    knob(seqEvolve, "Evolve amt", col::magenta, &pEuclid); toggle(seqFreeze, "Freeze pattern", &pEuclid);
    pEuclid.addAndMakeVisible(regenerate); pEuclid.addAndMakeVisible(evolveBtn);
    regenerate.onClick = [this] { proc.regeneratePattern(true); }; evolveBtn.onClick = [this] { proc.evolvePattern(); };
    regenerate.setTooltip("Rhythm-only plan with a new seed (ignores the track; frozen / locked events stay)"); evolveBtn.setTooltip("Phrase evolution: a variation of the current pattern; frozen events are untouched");
    for (auto* c : { &pMain, &pFeel, &pEuclid }) c->setVisible(false);
    pExport.addAndMakeVisible(exportBar);
    refresh(0xffffffffu);
}

void SequencePage::resized()
{
    auto r = getLocalBounds().reduced(8);
    pExport.setBounds(r.removeFromBottom(100)); r.removeFromBottom(8); exportBar.setBounds(pExport.content());
    pSimple.setBounds(r.removeFromTop(150)); r.removeFromTop(8);
    {   auto c = pSimple.content(); auto top = c.removeFromLeft(std::min(c.getWidth() * 42 / 100, 460));
        build.setBounds(top.removeFromTop(40)); top.removeFromTop(6); auto row = top.removeFromTop(34); variation.setBounds(row.removeFromLeft(row.getWidth() / 2 - 3)); row.removeFromLeft(6); listenBtn.setBounds(row);
        top.removeFromTop(6); auto row2 = top.removeFromTop(26); playTop->setBounds(row2.removeFromLeft(row2.getWidth() / 2)); freeTop->setBounds(row2);
        c.removeFromLeft(10); const int kw = (c.getWidth() - 110) / 5;
        for (auto& k : topKnobs) k->setBounds(c.removeFromLeft(kw));
        moreBtn.setBounds(c.reduced(4, 30)); }
    if (showMore)
    {
        auto more = r.removeFromBottom(std::min(210, r.getHeight() / 2)); r.removeFromBottom(8);
        const int w1 = more.getWidth() * 36 / 100, w2 = more.getWidth() * 36 / 100;
        pMain.setBounds(more.removeFromLeft(w1)); more.removeFromLeft(8); pFeel.setBounds(more.removeFromLeft(w2)); more.removeFromLeft(8); pEuclid.setBounds(more);
        auto c = pMain.content(); auto t = c.removeFromTop(26); toggles[0]->setBounds(t.removeFromLeft(130)); toggles[1]->setBounds(t);
        auto row = c.removeFromTop(48); combos[0]->setBounds(row.removeFromLeft(row.getWidth() / 3 - 4)); row.removeFromLeft(6); combos[1]->setBounds(row.removeFromLeft(row.getWidth() / 2 - 3)); row.removeFromLeft(6); combos[2]->setBounds(row);
        c.removeFromTop(4); const int kw = c.getWidth() / 3;
        knobs[0]->setBounds(c.getX(), c.getY(), kw, 96); knobs[1]->setBounds(c.getX() + kw, c.getY(), kw, 96); knobs[2]->setBounds(c.getX() + 2 * kw, c.getY(), kw, 96);
        auto f = pFeel.content(); const int fw = f.getWidth() / 4;
        for (int i = 0; i < 7; ++i) knobs[(size_t) (3 + i)]->setBounds(f.getX() + (i % 4) * fw, f.getY() + (i / 4) * 100, fw, 96);
        auto e = pEuclid.content(); const int ew = e.getWidth() / 4;
        for (int i = 0; i < 4; ++i) knobs[(size_t) (10 + i)]->setBounds(e.getX() + i * ew, e.getY(), ew, 96);
        auto b = e.withTrimmedTop(100); toggles[2]->setBounds(b.removeFromTop(24)); b.removeFromTop(4); auto br = b.removeFromTop(28); regenerate.setBounds(br.removeFromLeft(br.getWidth() / 2 - 3)); br.removeFromLeft(6); evolveBtn.setBounds(br);
    }
    pPattern.setBounds(r);
    auto pc = pPattern.content(); info.setBounds(pc.removeFromBottom(18)); view.setBounds(pc);
}

void SequencePage::refresh(uint32_t f)
{
    if (f & (kChPattern | kChPreset | kChParamsLoaded | kChSound | kChAnalysis))
    {
        view.pat = proc.pattern(); view.ana = proc.analysis(); view.repaint();
        if (view.pat)
        {
            std::set<int> used; for (auto& e : view.pat->events) used.insert(e.slot);
            juce::String s = juce::String(view.pat->events.size()) + " events over " + juce::String(view.pat->cfg.bars) + " bars, " + juce::String((int) used.size()) + " different sound(s).";
            s << (view.ana ? "  Placed by the analysed track." : "  No track analysed: rhythm only - press LISTEN (or Import WAV in Track analysis).");
            s << (view.pat->cfg.enabled ? "" : "  Sequence is OFF (PLAY SEQUENCE).");
            info.setText(s + "  Click an event to lock it.", juce::dontSendNotification);
        }
    }
}
void SequencePage::tick()
{
    view.playing = proc.hostPlaying() || proc.apvts.getRawParameterValue(seqFreeRun)->load() > 0.5f; view.playhead = proc.playheadBeats(); if (view.playing) view.repaint();
    const bool busy = proc.busy(); build.setEnabled(! busy); variation.setEnabled(! busy); listenBtn.setEnabled(! busy);
}
}
