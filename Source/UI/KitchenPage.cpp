#include "Pages.h"

namespace dali::ui
{
using namespace pid;

juce::Colour KitchenPage::familyColour(int f)
{
    static const juce::Colour c[kNumFamilies] = { juce::Colour(0xff1ff5e0), juce::Colour(0xffffd54a), juce::Colour(0xffff2bd6), juce::Colour(0xff9a63ff), juce::Colour(0xff52f7a0), juce::Colour(0xff6ea8ff) };
    return c[std::clamp(f, 0, kNumFamilies - 1)];
}

static void setChoice(juce::AudioProcessorValueTreeState& s, const char* id, int v)
{
    if (auto* p = s.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float) v)); p->endChangeGesture(); }
}

//==============================================================================================================
// flavour matrix: rows lead, columns blend in; the diagonal is a pure family
//==============================================================================================================
juce::Rectangle<float> KitchenPage::FlavorMatrix::cellArea() const { return getLocalBounds().toFloat().withTrimmedTop(36.0f).withTrimmedLeft(64.0f); }

bool KitchenPage::FlavorMatrix::cellAt(juce::Point<float> p, int& r, int& c) const
{
    auto a = cellArea(); if (! a.contains(p)) return false;
    c = (int) ((p.x - a.getX()) / (a.getWidth() / kNumFamilies)); r = (int) ((p.y - a.getY()) / (a.getHeight() / kNumFamilies));
    return r >= 0 && r < kNumFamilies && c >= 0 && c < kNumFamilies;
}

juce::String KitchenPage::FlavorMatrix::getTooltip()
{
    if (hoverR < 0) return {};
    if (hoverR == hoverC) return juce::String(familyName(hoverR)) + ": " + familyBlurb(hoverR);
    return juce::String(familyName(hoverR)) + " leads, " + familyName(hoverC) + " blends in: " + fusionName(hoverR, hoverC);
}

void KitchenPage::FlavorMatrix::mouseMove(const juce::MouseEvent& e)
{
    int r = -1, c = -1; if (! cellAt(e.position, r, c)) { r = c = -1; }
    if (r != hoverR || c != hoverC) { hoverR = r; hoverC = c; repaint(); }
}
void KitchenPage::FlavorMatrix::mouseExit(const juce::MouseEvent&) { hoverR = hoverC = -1; repaint(); }
void KitchenPage::FlavorMatrix::mouseDown(const juce::MouseEvent& e)
{
    int r, c; if (cellAt(e.position, r, c)) { owner.setFamilies(r, r == c ? -1 : c); owner.cookSoon(); }
}

void KitchenPage::FlavorMatrix::paint(juce::Graphics& g)
{
    auto& s = owner.proc.apvts;
    const int a = (int) s.getRawParameterValue(famA)->load() - 1, b = (int) s.getRawParameterValue(famB)->load() - 1;
    g.setFont(font(10.5f, true)); g.setColour(col::dim);
    g.drawText("BLEND IN  >", getLocalBounds().removeFromTop(14).withTrimmedLeft(64), juce::Justification::centredLeft);
    g.saveState(); g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, 12.0f, (float) getHeight() * 0.5f + 12.0f));
    g.drawText("LEAD  >", juce::Rectangle<int>(12 - 70, (int) ((float) getHeight() * 0.5f + 12.0f) - 8, 140, 16), juce::Justification::centred); g.restoreState();
    const auto area = cellArea(); const float cw = area.getWidth() / kNumFamilies, ch = area.getHeight() / kNumFamilies;
    for (int i = 0; i < kNumFamilies; ++i)
    {
        g.setFont(font(12.0f, true)); g.setColour(familyColour(i));
        g.drawText(familyName(i), juce::Rectangle<float>(area.getX() + cw * (float) i, area.getY() - 20.0f, cw, 18.0f), juce::Justification::centred);
        g.drawText(familyName(i), juce::Rectangle<float>(0.0f, area.getY() + ch * (float) i, 62.0f, ch), juce::Justification::centredRight);
    }
    for (int r = 0; r < kNumFamilies; ++r) for (int c = 0; c < kNumFamilies; ++c)
    {
        auto cell = juce::Rectangle<float>(area.getX() + cw * (float) c, area.getY() + ch * (float) r, cw, ch).reduced(2.5f);
        const bool pure = r == c;
        const bool sel = a == r && ((pure && b < 0) || (! pure && b == c));
        const bool hov = hoverR == r && hoverC == c;
        const float alpha = sel ? 0.85f : hov ? 0.5f : pure ? 0.3f : 0.17f;
        g.setGradientFill(juce::ColourGradient(familyColour(r).withAlpha(alpha), cell.getTopLeft(), familyColour(c).withAlpha(alpha), cell.getBottomRight(), false));
        g.fillRoundedRectangle(cell, 7.0f);
        if (sel) glowRect(g, cell, familyColour(r), 7.0f, 0.7f);
        g.setColour(sel ? col::text : hov ? col::text.withAlpha(0.7f) : col::line); g.drawRoundedRectangle(cell, 7.0f, sel ? 1.8f : 1.0f);
        g.setColour(sel ? juce::Colours::white : col::text.withAlpha(pure ? 0.95f : 0.8f));
        g.setFont(font(pure ? 13.5f : 11.5f, pure || sel));
        g.drawFittedText(fusionName(r, c), cell.reduced(4.0f).toNearestInt(), juce::Justification::centred, 2, 0.85f);
    }
}

//==============================================================================================================
// plates, drag pads, keys
//==============================================================================================================
KitchenPage::Plate::Plate(KitchenPage& o, int i) : owner(o), index(i)
{
    addAndMakeVisible(wave);
    wave.onClick = [this] { if (has) owner.proc.selectCandidate(index); };
    wave.dragProvider = [this]() -> juce::File
    {   auto c = owner.proc.candidates(); if (index >= (int) c.size()) return {};
        owner.proc.selectCandidate(index); return owner.proc.makeDragFile(CandyProcessor::ExportWhat::OneShot, ExportFormat()); };
}
void KitchenPage::Plate::resized() { wave.setBounds(getLocalBounds().reduced(5).withTrimmedTop(20)); }
void KitchenPage::Plate::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat().reduced(1.0f), "", wave.selected ? col::turquoise : col::line);
    if (! has) { g.setColour(col::dim); g.setFont(font(11.0f)); g.drawText("empty plate", getLocalBounds(), juce::Justification::centred); return; }
    g.setColour(col::text); g.setFont(font(12.0f, true)); g.drawText(name, 9, 4, getWidth() - 14, 16, juce::Justification::centredLeft, true);
}
void KitchenPage::Plate::set(const CandidateView* v, bool sel)
{
    has = v != nullptr; wave.selected = sel; name = v ? v->dna.name : juce::String();
    wave.setWave(v ? v->wave : std::vector<float>()); wave.setColour(sel ? col::turquoise : col::magenta); repaint();
}

void KitchenPage::DragPad::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f); const auto c = isMidi ? col::purple : col::magenta;
    g.setColour(c.withAlpha(0.22f)); g.fillRoundedRectangle(r, 8.0f); g.setColour(c); g.drawRoundedRectangle(r, 8.0f, 1.4f);
    g.setColour(col::text); g.setFont(font(12.5f, true)); g.drawText(text, r.reduced(8.0f, 0.0f), juce::Justification::centred, true);
}
void KitchenPage::DragPad::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 8) return;
    static bool busy = false; if (busy) return; busy = true;
    ExportFormat f; f.sampleRate = 48000.0; f.bitDepth = 24;
    auto file = owner.proc.makeDragFile(isMidi ? CandyProcessor::ExportWhat::Midi : CandyProcessor::ExportWhat::OneShot, f);
    if (file.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
    busy = false;
}

static bool isBlackKey(int note) { const int n = note % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }
int KitchenPage::Keys::noteAt(juce::Point<float> p) const
{
    const int whites = 14; const float ww = (float) getWidth() / (float) whites, bh = (float) getHeight() * 0.6f;
    static const int wn[7] = { 0, 2, 4, 5, 7, 9, 11 };
    auto whiteNote = [&](int i) { return base + (i / 7) * 12 + wn[i % 7]; };
    if (p.y < bh)   // black keys sit between white keys
        for (int i = 0; i < whites; ++i) { const int nn = whiteNote(i) + 1; if (isBlackKey(nn) && std::abs(p.x - ww * (float) (i + 1)) < ww * 0.3f) return nn; }
    const int i = std::clamp((int) (p.x / ww), 0, whites - 1); return whiteNote(i);
}
void KitchenPage::Keys::paint(juce::Graphics& g)
{
    const int whites = 14; const float ww = (float) getWidth() / (float) whites, bh = (float) getHeight() * 0.6f;
    static const int wn[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < whites; ++i)
    {
        const int n = base + (i / 7) * 12 + wn[i % 7]; const bool on = proc.keyboardState.isNoteOn(1, n);
        auto k = juce::Rectangle<float>(ww * (float) i, 0, ww, (float) getHeight()).reduced(1.0f);
        g.setColour(on ? col::turquoise : col::text.withAlpha(0.88f)); g.fillRoundedRectangle(k, 3.0f);
        if (n % 12 == 0) { g.setColour(col::bg); g.setFont(font(9.5f)); g.drawText("C" + juce::String(n / 12 - 1), k.removeFromBottom(14), juce::Justification::centred); }
    }
    for (int i = 0; i < whites; ++i)
    {
        const int n = base + (i / 7) * 12 + wn[i % 7] + 1; if (! isBlackKey(n)) continue;
        const bool on = proc.keyboardState.isNoteOn(1, n);
        g.setColour(on ? col::magenta : col::bg); g.fillRoundedRectangle(juce::Rectangle<float>(ww * (float) (i + 1) - ww * 0.3f, 0, ww * 0.6f, bh), 3.0f);
    }
}
void KitchenPage::Keys::press(int n) { if (n == down) return; release(); down = n; proc.keyboardState.noteOn(1, n, 0.85f); repaint(); }
void KitchenPage::Keys::release() { if (down >= 0) { proc.keyboardState.noteOff(1, down, 0.0f); down = -1; repaint(); } }
void KitchenPage::Keys::mouseDown(const juce::MouseEvent& e) { press(noteAt(e.position)); }
void KitchenPage::Keys::mouseDrag(const juce::MouseEvent& e) { if (getLocalBounds().toFloat().contains(e.position)) press(noteAt(e.position)); }
void KitchenPage::Keys::mouseUp(const juce::MouseEvent&) { release(); }

//==============================================================================================================
// page
//==============================================================================================================
KitchenPage::KitchenPage(CandyProcessor& p) : PageBase(p), keys(p)
{
    for (auto* c : { &pMatrix, &pDish, &pPlates, &pKnobs }) addAndMakeVisible(c);
    pMatrix.addAndMakeVisible(matrix);
    if (proc.apvts.getRawParameterValue(famA)->load() < 0.5f) setChoice(proc.apvts, famA, 1);       // the Kitchen always works with a family
    matrix.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    matrix.setInterceptsMouseClicks(true, false);

    auto& s = proc.apvts;
    spiceKnob = std::make_unique<ParamKnob>(s, spice, "SPICE", col::magenta); driftKnob = std::make_unique<ParamKnob>(s, drift, "DRIFT", col::purple);
    brightKnob = std::make_unique<ParamKnob>(s, brightness, "BRIGHT", col::turquoise); motionKnob = std::make_unique<ParamKnob>(s, motion, "MOTION", col::purple);
    for (auto* k : { spiceKnob.get(), driftKnob.get(), brightKnob.get(), motionKnob.get() }) pKnobs.addAndMakeVisible(*k);
    spiceKnob->slider.setTooltip("More weirdness and motion: deeper modulation, chaotic routes, odd inserts");
    driftKnob->slider.setTooltip("Evolution: every repeat wanders a little further (pitch, timbre, length). 0 = repeats stay the same");
    lenBox = std::make_unique<ParamCombo>(s, lenSync, "LENGTH (tempo-synced)"); pKnobs.addAndMakeVisible(*lenBox);
    lenBox->box.setTooltip("Free, or a window from 1/16 up to 4 bars at the host tempo. The dish is fitted to it and dragged out at exactly that length.");
    keep = std::make_unique<ParamToggle>(s, keepSkel, "KEEP", col::turquoise); pKnobs.addAndMakeVisible(*keep);
    keep->setTooltip("KEEP: COOK makes variations of the current dish - same skeleton (engines, envelopes, levels), different details");

    roll.setColour(juce::TextButton::buttonColourId, col::magenta.withAlpha(0.45f)); roll.setColour(juce::TextButton::buttonOnColourId, col::magenta);
    cook.setColour(juce::TextButton::buttonColourId, col::turquoise.withAlpha(0.3f)); listen.setColour(juce::TextButton::buttonColourId, col::purple.withAlpha(0.35f));
    roll.setTooltip("ROLL: ignores your choice. A random family or fusion, random seasoning, and one dish picked for you");
    cook.setTooltip("COOK: eight new plates from the chosen family / fusion"); listen.setTooltip("LISTEN: capture the track (Analysis Source), place candy in its gaps and aim the sound at the quietest band");
    roll.onClick = [this] { proc.roll(); }; cook.onClick = [this] { proc.generate(); };
    listen.onClick = [this] { proc.listen(); };
    buildBtn.setColour(juce::TextButton::buttonColourId, col::magenta.withAlpha(0.35f));
    buildBtn.setTooltip("BUILD: arrange the eight plates over the analysed track (gaps, free bands, transitions). Press LISTEN first if no track was analysed.");
    buildBtn.onClick = [this] { proc.buildPattern(true); };
    playSeq = std::make_unique<ParamToggle>(proc.apvts, pid::seqOn, "PLAY SEQUENCE", col::magenta); addAndMakeVisible(*playSeq);
    playSeq->setTooltip("Plays the built pattern with the host transport. Off by default: nothing plays until you switch it on.");
    for (auto* b : { &roll, &cook, &listen, &buildBtn }) addAndMakeVisible(b);
    octDown.onClick = [this] { keys.release(); keys.base = std::max(24, keys.base - 12); keys.repaint(); };
    octUp.onClick = [this] { keys.release(); keys.base = std::min(84, keys.base + 12); keys.repaint(); };
    for (auto* b : { &octDown, &octUp }) pDish.addAndMakeVisible(b);
    pDish.addAndMakeVisible(keys);

    pDish.addAndMakeVisible(dishWave); dishWave.setColour(col::turquoise);
    dishWave.onClick = [this] { proc.audition(proc.mainSlot()); };
    dishWave.dragProvider = [this] { ExportFormat f; f.sampleRate = 48000.0; return proc.makeDragFile(CandyProcessor::ExportWhat::OneShot, f); };
    pDish.addAndMakeVisible(dragWav); pDish.addAndMakeVisible(dragMidi);
    for (auto* l : { &dishName, &dishInfo }) { pDish.addAndMakeVisible(l); l->setInterceptsMouseClicks(false, false); }
    dishName.getProperties().set("ownFont", true); dishName.setFont(font(26.0f, true)); dishName.setColour(juce::Label::textColourId, col::text);
    dishInfo.setColour(juce::Label::textColourId, col::dim); dishInfo.setFont(font(12.5f));
    addAndMakeVisible(status); status.setColour(juce::Label::textColourId, col::turquoise);
    for (int i = 0; i < 8; ++i) { plates.push_back(std::make_unique<Plate>(*this, i)); pPlates.addAndMakeVisible(*plates.back()); }
    refresh(0xffffffffu);
}

void KitchenPage::setFamilies(int a, int b) { setChoice(proc.apvts, famA, a + 1); setChoice(proc.apvts, famB, b + 1); setChoice(proc.apvts, keepSkel, 0); matrix.repaint(); }
void KitchenPage::cookSoon() { cookPending = true; }

juce::String KitchenPage::dishTitle() const
{
    const int a = (int) proc.apvts.getRawParameterValue(famA)->load() - 1, b = (int) proc.apvts.getRawParameterValue(famB)->load() - 1;
    if (a < 0) return "Category mode (advanced)";
    if (b < 0) return juce::String(familyName(a)) + "  (pure)";
    return juce::String(familyName(a)) + " x " + familyName(b) + "  -  " + fusionName(a, b);
}

void KitchenPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(52);
    roll.setBounds(top.removeFromLeft(210)); top.removeFromLeft(8); cook.setBounds(top.removeFromLeft(150)); top.removeFromLeft(8); listen.setBounds(top.removeFromLeft(120)); top.removeFromLeft(8); buildBtn.setBounds(top.removeFromLeft(100)); top.removeFromLeft(8); playSeq->setBounds(top.removeFromLeft(150).withSizeKeepingCentre(150, 26)); top.removeFromLeft(10); status.setBounds(top);
    r.removeFromTop(8);
    auto bottom = r.removeFromBottom(150); r.removeFromBottom(8);
    pPlates.setBounds(bottom);
    { auto c = pPlates.content(); const int pw = c.getWidth() / 8; for (int i = 0; i < 8; ++i) plates[(size_t) i]->setBounds(c.getX() + i * pw, c.getY(), pw, c.getHeight()); }
    auto left = r.removeFromLeft(std::min(r.getWidth() * 47 / 100, 640)); r.removeFromLeft(8);
    pMatrix.setBounds(left); matrix.setBounds(pMatrix.content());
    auto right = r;
    auto kn = right.removeFromBottom(150); right.removeFromBottom(8);
    pKnobs.setBounds(kn);
    {   auto c = pKnobs.content(); const int kw = c.getWidth() / 6;
        spiceKnob->setBounds(c.removeFromLeft(kw)); driftKnob->setBounds(c.removeFromLeft(kw)); brightKnob->setBounds(c.removeFromLeft(kw)); motionKnob->setBounds(c.removeFromLeft(kw));
        auto rest = c.reduced(6, 0); lenBox->setBounds(rest.removeFromTop(44)); rest.removeFromTop(8); keep->setBounds(rest.removeFromTop(30)); }
    pDish.setBounds(right);
    {   auto c = pDish.content();
        dishName.setBounds(c.removeFromTop(32)); dishInfo.setBounds(c.removeFromTop(20)); c.removeFromTop(6);
        auto pads = c.removeFromBottom(34); c.removeFromBottom(6);
        auto kb = c.removeFromBottom(58); c.removeFromBottom(6);
        dishWave.setBounds(c);
        dragWav.setBounds(pads.removeFromLeft(pads.getWidth() * 62 / 100)); pads.removeFromLeft(8); dragMidi.setBounds(pads);
        octDown.setBounds(kb.removeFromLeft(26).removeFromTop(26)); octUp.setBounds(kb.removeFromRight(26).removeFromTop(26)); keys.setBounds(kb.reduced(4, 0)); }
}

void KitchenPage::updateDishLabels()
{
    const auto d = proc.mainSound();
    dishName.setText(d.name, juce::dontSendNotification);
    juce::String eng; for (auto& l : d.layers) if (l.active) eng << (eng.isEmpty() ? "" : " + ") << engineName(l.engine);
    dishInfo.setText(dishTitle() + "   |   " + juce::String(d.activeLayerCount()) + " layer(s): " + eng, juce::dontSendNotification);
    const double w = proc.syncWindowMs();
    lengthNote = w > 0.0 ? juce::String((int) std::lround(w)) + " ms window  |  " + lenSyncName((int) proc.apvts.getRawParameterValue(lenSync)->load()) + " @ " + juce::String(proc.hostBpm(), 1) + " bpm" : juce::String();
    dragWav.text = w > 0.0 ? "DRAG THE DISH TO YOUR TRACK   (WAV, " + juce::String((int) std::lround(w)) + " ms, on the grid)" : "DRAG THE DISH TO YOUR TRACK   (WAV)";
    dragMidi.text = "DRAG PATTERN (MIDI)"; dragWav.repaint(); dragMidi.repaint();
}

void KitchenPage::renderDishWave()
{
    ExportFormat f; f.sampleRate = 44100.0; f.bitDepth = 16; f.quality = QualityMode::Eco; f.trimSilence = true; f.fitMs = (float) proc.syncWindowMs();
    auto b = proc.renderPreview(f, ExportKind::OneShot, ExportMix::Full, -1);
    dishWave.setWave(b.getNumSamples() > 0 ? minMaxPairs(b, 480) : std::vector<float>());
}

void KitchenPage::refresh(uint32_t f)
{
    const auto d = proc.mainSound();
    if (f & (kChCandidates | kChSound | kChPreset | kChParamsLoaded))
    {
        auto cands = proc.candidates();
        for (size_t i = 0; i < plates.size(); ++i) plates[i]->set(i < cands.size() ? &cands[i] : nullptr, i < cands.size() && cands[i].dna.id == d.id && d.id != 0);
        updateDishLabels();
        if (d.id != lastMainId || (f & (kChPreset | kChParamsLoaded))) { previewDirty = true; previewCountdown = 2; }
        lastMainId = d.id;
    }
    if (f & kChStatus) status.setText(proc.statusText(), juce::dontSendNotification);
}

void KitchenPage::tick()
{
    const bool busy = proc.busy();
    cook.setEnabled(! busy); roll.setEnabled(! busy); listen.setEnabled(! busy); buildBtn.setEnabled(! busy);
    if (cookPending && ! busy) { cookPending = false; proc.generate(); }
    if (busy || progressShown > 0.0f) { progressShown = busy ? proc.progress() : 0.0f; repaint(); }
    status.setText(proc.statusText(), juce::dontSendNotification);
    const int a = (int) proc.apvts.getRawParameterValue(famA)->load(), b = (int) proc.apvts.getRawParameterValue(famB)->load();
    if (a != shownA || b != shownB) { shownA = a; shownB = b; matrix.repaint(); updateDishLabels(); }
    if (previewDirty && --previewCountdown <= 0 && isVisible()) { previewDirty = false; renderDishWave(); updateDishLabels(); }
    keys.repaint();
}

void KitchenPage::paintOverChildren(juce::Graphics& g)
{
    auto wb = dishWave.getBounds().translated(pDish.getX(), pDish.getY()).toFloat();
    if (lengthNote.isNotEmpty() && dishWave.isVisible())
    {   // quarter-window guides: the dragged WAV is exactly this long
        g.setColour(col::turquoise.withAlpha(0.22f));
        for (int i = 1; i < 4; ++i) g.drawVerticalLine((int) (wb.getX() + wb.getWidth() * (float) i / 4.0f), wb.getY() + 4.0f, wb.getBottom() - 4.0f);
        g.setColour(col::dim); g.setFont(font(10.5f)); g.drawText(lengthNote, wb.reduced(8.0f, 4.0f), juce::Justification::bottomLeft);
    }
    if (progressShown > 0.001f)
    {
        auto r = cook.getBounds().toFloat().removeFromBottom(4.0f).reduced(3, 0); g.setColour(col::bg); g.fillRect(r); g.setColour(col::turquoise); g.fillRect(r.withWidth(r.getWidth() * progressShown));
    }
}
}
