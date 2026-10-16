#include "Pages.h"

namespace dali::ui
{
using namespace pid;

namespace
{
const char* kBandShort[] = { "Sub", "Bass", "Low", "Mid", "HiMid", "Pres", "Air", "Top" };
void bar(juce::Graphics& g, juce::Rectangle<float> r, float v, juce::Colour c)
{
    g.setColour(col::bg2); g.fillRoundedRectangle(r, 3.0f); g.setColour(col::line); g.drawRoundedRectangle(r, 3.0f, 0.8f);
    g.setColour(c.withAlpha(0.3f)); g.fillRoundedRectangle(r.withWidth(r.getWidth() * std::clamp(v, 0.0f, 1.0f)).expanded(0, 1), 3.0f);
    g.setColour(c); g.fillRoundedRectangle(r.reduced(1.0f).withWidth(std::max(0.0f, (r.getWidth() - 2.0f) * std::clamp(v, 0.0f, 1.0f))), 2.0f);
}
float dbToUnit(float db) { return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); }
}

void AnalysisPage::Live::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat(); const auto& L = owner.proc.live();
    const float w = r.getWidth();
    auto row = [&](float y, const juce::String& name, float v, juce::Colour c, const juce::String& txt)
    { g.setColour(col::dim); g.setFont(font(11.0f)); g.drawText(name, 0, (int) y, 60, 16, juce::Justification::centredLeft); bar(g, { 62.0f, y + 2.0f, w * 0.45f, 12.0f }, v, c); g.setColour(col::text); g.drawText(txt, (int) (62.0f + w * 0.45f + 6.0f), (int) y, 80, 16, juce::Justification::centredLeft); };
    const float rmsDb = 20.0f * std::log10(std::max(1.0e-6f, L.rms())), pkDb = 20.0f * std::log10(std::max(1.0e-6f, L.peak()));
    row(0, "RMS", dbToUnit(rmsDb), col::turquoise, juce::String(rmsDb, 1) + " dB"); row(18, "Peak", dbToUnit(pkDb), pkDb > -1.0f ? col::bad : col::magenta, juce::String(pkDb, 1) + " dB");
    row(36, "Low", L.low(), col::purple, ""); row(54, "Mid", L.mid(), col::magenta, ""); row(72, "High", L.high(), col::turquoise, "");
    row(90, "Bright", L.brightness(), col::magenta, juce::String(L.brightness(), 2));
    row(108, "Follow", L.follower(), col::turquoise, ""); row(126, "Transient", L.transient(), col::bad, "");
    g.setColour(col::dim); g.drawText("Onsets/s " + juce::String(L.onsetDensity(), 1) + "   Stereo corr " + juce::String(L.stereoCorrelation(), 2), 0, 146, (int) w, 16, juce::Justification::centredLeft);
    // level history
    auto h = r.withTrimmedLeft(w * 0.62f).reduced(0, 0); g.setColour(col::bg2); g.fillRoundedRectangle(h, 5.0f); g.setColour(col::line); g.drawRoundedRectangle(h, 5.0f, 0.8f);
    juce::Path p; const int n = LiveAnalyzer::kHistory; const int pos = L.historyPos();
    for (int i = 0; i < n; ++i) { const float v = std::clamp(L.history((pos + i) & (n - 1)), 0.0f, 1.0f); const float x = h.getX() + h.getWidth() * (float) i / (float) (n - 1), y = h.getBottom() - 3.0f - std::sqrt(v) * (h.getHeight() - 6.0f); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
    g.setColour(col::turquoise.withAlpha(0.3f)); g.strokePath(p, juce::PathStrokeType(3.5f)); g.setColour(col::turquoise); g.strokePath(p, juce::PathStrokeType(1.3f));
    g.setColour(col::dim); g.setFont(font(10.0f)); g.drawText("level history", h.reduced(6, 3), juce::Justification::topLeft);
}

void AnalysisPage::Mask::paint(juce::Graphics& g)
{
    const auto m = owner.proc.masking(); auto r = getLocalBounds().toFloat();
    if (! owner.ana || ! owner.ana->valid) { g.setColour(col::dim); g.setFont(font(12.0f)); g.drawFittedText("Capture or import a track to estimate masking.\nThe report compares the spectral occupancy of the current candy with the track in 8 bands. It is an estimate - it cannot guarantee that nothing is masked.", getLocalBounds().reduced(6), juce::Justification::centredLeft, 6); return; }
    const float bw = r.getWidth() / 8.0f, hh = r.getHeight() - 40.0f;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float x = r.getX() + (float) b * bw;
        const float a = m.candyMaskedRisk[(size_t) b], t = m.trackMaskedRisk[(size_t) b];
        g.setColour(col::magenta); g.fillRoundedRectangle(x + 4.0f, r.getY() + hh * (1.0f - a), bw * 0.4f, hh * a, 2.0f);
        g.setColour(col::turquoise); g.fillRoundedRectangle(x + 6.0f + bw * 0.4f, r.getY() + hh * (1.0f - t), bw * 0.4f, hh * t, 2.0f);
        g.setColour(b == m.worstBand ? col::warn : col::dim); g.setFont(font(10.0f)); g.drawText(kBandShort[b], (int) x, (int) (r.getY() + hh + 2.0f), (int) bw, 12, juce::Justification::centred);
    }
    g.setColour(col::text); g.setFont(font(11.0f));
    g.drawText("Overall risk " + juce::String(m.overall, 2) + "   worst band: " + kBandShort[std::clamp(m.worstBand, 0, 7)] + "   mono-compat " + juce::String(m.stereoCompat, 2), (int) r.getX(), (int) (r.getBottom() - 22.0f), (int) r.getWidth(), 14, juce::Justification::centredLeft);
    g.setColour(col::magenta); g.fillRect(r.getRight() - 150.0f, r.getY() + 3.0f, 8.0f, 8.0f); g.setColour(col::dim); g.drawText("candy masked", (int) r.getRight() - 138, (int) r.getY(), 70, 14, juce::Justification::centredLeft);
    g.setColour(col::turquoise); g.fillRect(r.getRight() - 70.0f, r.getY() + 3.0f, 8.0f, 8.0f); g.setColour(col::dim); g.drawText("track masked", (int) r.getRight() - 58, (int) r.getY(), 60, 14, juce::Justification::centredLeft);
}

int AnalysisPage::TrackView::stepAt(float x) const
{
    if (! owner.ana || owner.ana->numSteps <= 0)
        return -1;
    return std::clamp((int) (x / (float) getWidth() * (float) owner.ana->numSteps), 0, owner.ana->numSteps - 1);
}

void AnalysisPage::TrackView::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat(); g.setColour(col::bg2); g.fillRoundedRectangle(r, 6.0f); g.setColour(col::line); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    const auto& a = owner.ana;
    if (! a || ! a->valid)
    { g.setColour(col::dim); g.setFont(font(13.0f)); g.drawFittedText("No analysis yet.\nCapture the incoming audio (FX) or import a WAV file. The map shows where the track leaves room for ear candy.", getLocalBounds().reduced(20), juce::Justification::centred, 4); return; }
    const float stripH = 26.0f, waveH = r.getHeight() - stripH - 8.0f;
    // opportunity strip
    const int ns = a->numSteps; const float sw = r.getWidth() / (float) std::max(1, ns);
    for (int i = 0; i < ns && i < (int) a->opportunity.size(); ++i) { const float o = std::clamp(a->opportunity[(size_t) i], 0.0f, 1.0f); g.setColour(col::turquoise.withAlpha(0.08f + 0.8f * o)); g.fillRect(r.getX() + (float) i * sw, r.getBottom() - stripH - 2.0f, std::max(1.0f, sw), stripH); }
    g.setColour(col::dim); g.setFont(font(9.5f)); g.drawText("opportunity", (int) r.getX() + 4, (int) (r.getBottom() - stripH - 2.0f), 80, 12, juce::Justification::centredLeft);
    // waveform
    const int wn = (int) a->waveform.size() / 2; juce::Path p; const float cy = r.getY() + waveH * 0.5f + 4.0f;
    for (int i = 0; i < wn; ++i) { const float x = r.getX() + r.getWidth() * (float) i / (float) std::max(1, wn - 1); p.addLineSegment({ x, cy - std::clamp(a->waveform[(size_t) i * 2 + 1], -1.0f, 1.0f) * waveH * 0.5f, x, cy - std::clamp(a->waveform[(size_t) i * 2], -1.0f, 1.0f) * waveH * 0.5f - 0.5f }, 1.0f); }
    g.setColour(col::purple.withAlpha(0.9f)); g.strokePath(p, juce::PathStrokeType(1.0f));
    // bars + sections
    for (int b = 0; b <= a->bars; ++b) { const float x = r.getX() + r.getWidth() * (float) (b * a->stepsPerBar) / (float) std::max(1, ns); g.setColour(col::line.brighter(0.2f).withAlpha(b % 4 == 0 ? 0.9f : 0.4f)); g.drawVerticalLine((int) x, r.getY() + 2.0f, r.getBottom() - 2.0f); }
    for (int sb : a->sectionBars) { const float x = r.getX() + r.getWidth() * (float) (sb * a->stepsPerBar) / (float) std::max(1, ns); g.setColour(col::warn); g.drawVerticalLine((int) x, r.getY() + 2.0f, r.getY() + 14.0f); g.setFont(font(9.5f)); g.drawText("section", (int) x + 2, (int) r.getY() + 1, 50, 12, juce::Justification::centredLeft); }
    // suggestions
    for (size_t i = 0; i < a->suggestions.size(); ++i)
    {
        const auto& s = a->suggestions[i]; const float x = r.getX() + (float) s.step * sw; const float h = 10.0f + 28.0f * std::clamp(s.score, 0.0f, 1.0f);
        const auto c = s.rejected ? col::dim : s.accepted ? col::good : col::magenta; const float y = r.getY() + waveH - h + 4.0f;
        g.setColour(c.withAlpha(s.rejected ? 0.5f : 0.95f)); g.fillRoundedRectangle(x, y, std::max(4.0f, sw), h, 2.0f);
        if (s.locked) { g.setColour(col::turquoise); g.drawRoundedRectangle(x - 1.0f, y - 1.0f, std::max(4.0f, sw) + 2.0f, h + 2.0f, 2.0f, 1.5f); }
        if ((int) i == owner.selected) { g.setColour(juce::Colours::white); g.drawRoundedRectangle(x - 2.0f, y - 2.0f, std::max(4.0f, sw) + 4.0f, h + 4.0f, 3.0f, 1.3f); }
    }
}

void AnalysisPage::TrackView::mouseDown(const juce::MouseEvent& e)
{
    if (! owner.ana || ! owner.ana->valid)
        return;
    const int st = stepAt((float) e.x);
    if (st < 0)
        return;
    int hit = -1;
    for (size_t i = 0; i < owner.ana->suggestions.size(); ++i)
        if (owner.ana->suggestions[i].step == st)
            hit = (int) i;
    if (hit >= 0) { owner.selected = hit; owner.sugList.selectRow(hit); repaint(); return; }
    owner.proc.editAnalysis([st](TrackAnalysis& t) { Suggestion s; s.step = st; s.score = 0.6f; s.accepted = true; s.locked = true; t.suggestions.push_back(s); });   // user-placed
}

int AnalysisPage::SugList::getNumRows() { return owner.ana ? (int) owner.ana->suggestions.size() : 0; }
void AnalysisPage::SugList::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (! owner.ana || row >= (int) owner.ana->suggestions.size()) return;
    const auto& s = owner.ana->suggestions[(size_t) row]; const int spb = std::max(1, owner.ana->stepsPerBar);
    if (sel) { g.setColour(col::purple.withAlpha(0.35f)); g.fillRect(0, 0, w, h); }
    g.setColour(s.rejected ? col::dim : s.accepted ? col::good : col::magenta); g.fillEllipse(6.0f, (float) h * 0.5f - 4.0f, 8.0f, 8.0f);
    g.setColour(col::text); g.setFont(font(12.0f)); g.drawText("Bar " + juce::String(s.step / spb + 1) + "  beat " + juce::String((s.step % spb) / 4 + 1) + "." + juce::String(s.step % 4 + 1), 20, 0, 120, h, juce::Justification::centredLeft);
    g.setColour(col::dim); g.setFont(font(11.0f)); g.drawText("score " + juce::String(s.score, 2) + "  rhy " + juce::String(s.rhythm, 1) + " spc " + juce::String(s.spectral, 1) + " dyn " + juce::String(s.dynamic, 1) + " phr " + juce::String(s.phrase, 1), 140, 0, w - 220, h, juce::Justification::centredLeft);
    g.setColour(col::turquoise); g.drawText(juce::String(s.locked ? "locked " : "") + (s.rejected ? "rejected" : s.accepted ? "accepted" : "proposed"), w - 90, 0, 86, h, juce::Justification::centredRight);
}
void AnalysisPage::SugList::listBoxItemClicked(int row, const juce::MouseEvent&) { owner.selected = row; owner.trackView.repaint(); }

AnalysisPage::AnalysisPage(CandyProcessor& p) : PageBase(p)
{
    for (auto* c : { &pLive, &pTrack, &pSug, &pMask, &pSrc }) addAndMakeVisible(c);
    pLive.addAndMakeVisible(live); pTrack.addAndMakeVisible(trackView); pMask.addAndMakeVisible(mask);
    sugList.setModel(&sugModel); sugList.setRowHeight(24); pSug.addAndMakeVisible(sugList);
    auto& s = proc.apvts;
    capLen = std::make_unique<ParamCombo>(s, capBars, "Capture length"); pSrc.addAndMakeVisible(*capLen);
    if (proc.isFx()) { source = std::make_unique<ParamCombo>(s, anaSource, "Analysis source"); pSrc.addAndMakeVisible(*source); }
    bpmKnob = std::make_unique<ParamKnob>(s, anaBpm, "Track BPM", col::turquoise); adaptKnob = std::make_unique<ParamKnob>(s, adapt, "Adaptive mix", col::magenta); negKnob = std::make_unique<ParamKnob>(s, negSpace, "Neg. space", col::purple);
    for (auto* k : { bpmKnob.get(), adaptKnob.get(), negKnob.get() }) pSrc.addAndMakeVisible(*k);
    liveNeg = std::make_unique<ParamToggle>(s, pid::liveNeg, "Live negative space"); pSrc.addAndMakeVisible(*liveNeg);
    for (auto* b : { &capture, &cancelCap, &importBtn })
        pSrc.addAndMakeVisible(b);
    pSrc.addAndMakeVisible(capStatus); capStatus.setColour(juce::Label::textColourId, col::turquoise); capStatus.setFont(font(12.0f));
    capture.setTooltip(proc.isFx() ? "Arms capture of the audio input (or sidechain): recording starts at the next bar line while the host plays. Needs the plug-in's audio input to be connected - otherwise use Import WAV" : "No audio input in this build - import a WAV instead");
    capture.setEnabled(proc.isFx());
    capture.onClick = [this] { proc.startCapture(); }; cancelCap.onClick = [this] { proc.cancelCapture(); };
    importBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser>("Import audio for analysis", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) proc.importAudioFile(f); });
    };
    for (auto* b : { &regen, &applyBtn, &accept, &reject, &lockBtn, &acceptAll }) pSug.addAndMakeVisible(b);
    regen.setTooltip("Propose new placements (accepted / locked ones are kept, rejected ones are not proposed again)");
    applyBtn.setTooltip("Turns accepted and locked suggestions into sequencer events");
    regen.onClick = [this] { proc.regenerateSuggestions(); }; applyBtn.onClick = [this] { proc.applySuggestionsToSequence(); };
    auto sel = [this](std::function<void(Suggestion&)> f) { const int i = selected; if (i < 0) return; proc.editAnalysis([i, f](TrackAnalysis& t) { if (i < (int) t.suggestions.size()) f(t.suggestions[(size_t) i]); }); };
    accept.onClick = [sel] { sel([](Suggestion& s) { s.accepted = true; s.rejected = false; }); };
    reject.onClick = [sel] { sel([](Suggestion& s) { s.rejected = true; s.accepted = false; }); };
    lockBtn.onClick = [sel] { sel([](Suggestion& s) { s.locked = ! s.locked; }); };
    acceptAll.onClick = [this] { proc.editAnalysis([](TrackAnalysis& t) { for (auto& s : t.suggestions) if (! s.rejected) s.accepted = true; }); };
    pSrc.addAndMakeVisible(info); info.setColour(juce::Label::textColourId, col::dim);
    refresh(kChAnalysis);
}

void AnalysisPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(196); pSrc.setBounds(top.removeFromLeft(top.getWidth() * 48 / 100)); top.removeFromLeft(8); pLive.setBounds(top); r.removeFromTop(8);
    auto bottom = r.removeFromBottom(std::max(170, r.getHeight() * 46 / 100)); r.removeFromBottom(0);
    pTrack.setBounds(r); r = pTrack.content(); info.setBounds(juce::Rectangle<int>()); trackView.setBounds(r);
    r.removeFromTop(0); bottom.removeFromTop(8);
    pSug.setBounds(bottom.removeFromLeft(bottom.getWidth() * 58 / 100)); bottom.removeFromLeft(8); pMask.setBounds(bottom);
    { auto c = pSug.content(); auto b = c.removeFromBottom(28); const int w = b.getWidth() / 6; for (auto* x : { &accept, &reject, &lockBtn, &acceptAll, &regen, &applyBtn }) x->setBounds(b.removeFromLeft(w).reduced(2, 0));
      c.removeFromBottom(4); auto b2 = c.removeFromBottom(0); (void) b2; sugList.setBounds(c); }
    { auto c = pSug.getLocalBounds(); (void) c; }
    live.setBounds(pLive.content()); mask.setBounds(pMask.content());
    { auto c = pSrc.content(); auto row = c.removeFromTop(30); capture.setBounds(row.removeFromLeft(90)); row.removeFromLeft(4); cancelCap.setBounds(row.removeFromLeft(70)); row.removeFromLeft(4); importBtn.setBounds(row.removeFromLeft(120)); row.removeFromLeft(6); capStatus.setBounds(row);
      auto r2 = c.removeFromTop(46); capLen->setBounds(r2.removeFromLeft(130)); r2.removeFromLeft(8); if (source) source->setBounds(r2.removeFromLeft(130));
      auto r3 = c.removeFromTop(86); const int w = 90; bpmKnob->setBounds(r3.removeFromLeft(w)); adaptKnob->setBounds(r3.removeFromLeft(w)); negKnob->setBounds(r3.removeFromLeft(w)); liveNeg->setBounds(r3.removeFromLeft(150).withHeight(24).translated(0, 30));
      info.setBounds(c); info.setJustificationType(juce::Justification::topLeft); }
}

void AnalysisPage::refresh(uint32_t f)
{
    if (f & (kChAnalysis | kChPreset | kChParamsLoaded))
    {
        ana = proc.analysis(); if (selected >= (int) (ana ? ana->suggestions.size() : 0)) selected = -1;
        sugList.updateContent(); sugList.repaint(); trackView.repaint(); mask.repaint();
        if (ana && ana->valid)
            info.setText(juce::String(ana->bars) + " bars @ " + juce::String(ana->bpm, 1) + " bpm | RMS " + juce::String(ana->rmsDb, 1) + " dB | peak " + juce::String(ana->peakDb, 1) + " dB | centroid " + juce::String((int) ana->centroidHz)
                         + " Hz | rhythmic activity " + juce::String(ana->rhythmicActivity, 2) + " | dynamic density " + juce::String(ana->dynamicDensity, 2) + " | " + juce::String(ana->sectionBars.size()) + " section change(s)"
                         + " | " + juce::String(ana->suggestions.size()) + " suggestions.\nClick the map to place your own candy hit; select a row to accept / reject / lock it.", juce::dontSendNotification);
        else info.setText("No analysis. Capture (FX plugin, host must be playing) or import a WAV file.\nNothing is analysed until you ask for it, and analysis never runs on the audio thread.", juce::dontSendNotification);
    }
}

void AnalysisPage::tick()
{
    live.repaint();
    const int st = proc.captureState();
    capStatus.setText(st == 1 ? "armed - waiting for the next bar line (start the host transport)" : st == 2 ? "recording " + juce::String((int) (proc.captureProgress() * 100)) + " %" : st == 3 ? "capture finished - analysing" : proc.busy() ? proc.statusText() : "idle", juce::dontSendNotification);
    cancelCap.setEnabled(st == 1 || st == 2);
}
}
