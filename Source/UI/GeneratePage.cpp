#include "Pages.h"

namespace dali::ui
{
using namespace pid;

GeneratePage::Card::Card(GeneratePage& o, int i) : owner(o), index(i)
{
    addAndMakeVisible(wave); for (auto* b : { &use, &breed, &slot, &play }) addAndMakeVisible(b);
    use.setTooltip("Make this the current candy"); breed.setTooltip("Breed the current candy with this one"); slot.setTooltip("Store in a sound slot (MIDI channel / per-event sound)"); play.setTooltip("Audition");
    wave.onClick = [this] { if (has) owner.proc.selectCandidate(index); };
    wave.dragProvider = [this]() -> juce::File
    {   auto c = owner.proc.candidates(); if (index >= (int) c.size()) return {};
        owner.proc.selectCandidate(index); return owner.exportBar.dragFor(CandyProcessor::ExportWhat::OneShot); };
    use.onClick = [this] { owner.proc.selectCandidate(index); };
    play.onClick = [this] { owner.proc.selectCandidate(index); owner.proc.audition(0); };
    breed.onClick = [this] { auto c = owner.proc.candidates(); if (index < (int) c.size()) owner.proc.breedMainWith(c[(size_t) index].dna); };
    slot.onClick = [this]
    {
        juce::PopupMenu m; for (int s = 0; s < kNumSoundSlots; ++s) m.addItem(s + 1, "Slot " + juce::String(s + 1) + (s == 0 ? " (main)" : "") + "  /  MIDI channel " + juce::String(s + 1));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&slot), [this](int r) { if (r > 0) owner.proc.candidateToSlot(index, r - 1); });
    };
}
void GeneratePage::Card::resized()
{
    auto r = getLocalBounds().reduced(6); auto b = r.removeFromBottom(24); r.removeFromBottom(2);
    r.removeFromTop(34);
    wave.setBounds(r);
    const int w = b.getWidth() / 4; use.setBounds(b.removeFromLeft(w).reduced(1)); breed.setBounds(b.removeFromLeft(w).reduced(1)); slot.setBounds(b.removeFromLeft(w).reduced(1)); play.setBounds(b.reduced(1));
}
void GeneratePage::Card::paint(juce::Graphics& g)
{
    drawPanel(g, getLocalBounds().toFloat().reduced(1.0f), "", wave.selected ? col::turquoise : col::purple);
    if (! has) { g.setColour(col::dim); g.setFont(font(12.0f)); g.drawText("empty", getLocalBounds(), juce::Justification::centred); return; }
    g.setColour(col::text); g.setFont(font(13.5f, true)); g.drawText(name, 10, 6, getWidth() - 20, 16, juce::Justification::centredLeft, true);
    g.setColour(col::dim); g.setFont(font(11.0f)); g.drawText(info, 10, 22, getWidth() - 20, 14, juce::Justification::centredLeft, true);
}
void GeneratePage::Card::set(const CandidateView* v, bool sel)
{
    has = v != nullptr; wave.selected = sel; for (auto* b : { &use, &breed, &slot, &play }) b->setEnabled(has);
    if (v)
    {
        name = v->dna.name; juce::String eng; for (auto& l : v->dna.layers) if (l.active) eng << (eng.isEmpty() ? "" : "+") << juce::String(engineName(l.engine)).substring(0, 5);
        info = juce::String(categoryName(v->dna.category)) + "  |  " + eng + "  |  " + juce::String((int) v->dna.durationMs) + " ms  |  " + juce::String(v->dna.score, 2);
        wave.setWave(v->wave);
    } else wave.setWave({});
    wave.setColour(sel ? col::turquoise : col::magenta); repaint();
}

GeneratePage::GeneratePage(CandyProcessor& p) : PageBase(p), exportBar(p)
{
    for (auto* c : { &macros, &actions, &current }) addAndMakeVisible(c);
    auto& s = proc.apvts;
    struct K { const char* id; const char* cap; juce::Colour c; };
    for (auto k : { K { complexity, "Complexity", col::magenta }, K { motion, "Motion", col::purple }, K { space, "Space", col::turquoise }, K { density, "Density", col::magenta },
                    K { chaos, "Chaos", col::purple }, K { brightness, "Brightness", col::turquoise }, K { musicality, "Musicality", col::magenta } })
    { knobs.push_back(std::make_unique<ParamKnob>(s, k.id, k.cap, k.c)); macros.addAndMakeVisible(*knobs.back()); }
    negKnob = std::make_unique<ParamKnob>(s, negSpace, "Neg. space", col::turquoise); macros.addAndMakeVisible(*negKnob);
    for (auto* id : { lengthMode, category, rootNote, scale })
    { combos.push_back(std::make_unique<ParamCombo>(s, id, id == lengthMode ? "Length" : id == category ? "Category" : id == rootNote ? "Root" : "Scale")); macros.addAndMakeVisible(*combos.back()); }
    lenKnob = std::make_unique<ParamKnob>(s, lengthMs, "Custom ms", col::purple); macros.addAndMakeVisible(*lenKnob);
    macros.addAndMakeVisible(useCategory);
    useCategory.setTooltip("A family chosen in the Kitchen overrides Category. Click to generate from this Category again.");
    useCategory.onClick = [this] { for (const char* id : { famA, famB }) if (auto* q = proc.apvts.getParameter(id)) q->setValueNotifyingHost(0.0f); };

    generate.setColour(juce::TextButton::buttonColourId, col::magenta.withAlpha(0.35f)); generate.setColour(juce::TextButton::buttonOnColourId, col::magenta);
    generate.onClick = [this] { proc.generate(); }; addAndMakeVisible(generate);
    cancelBtn.onClick = [this] { proc.cancelWork(); }; addAndMakeVisible(cancelBtn);
    for (auto* b : { &mutate, &evolve, &recall, &audition, &like, &fav, &dislike }) actions.addAndMakeVisible(b);
    mutate.onClick = [this] { proc.mutateMain(mutateAmount); }; evolve.onClick = [this] { proc.evolveMain(mutateAmount); };
    recall.onClick = [this] { proc.recall((size_t) recallDepth); ++recallDepth; };
    audition.onClick = [this] { proc.audition(proc.mainSlot()); };
    like.onClick = [this] { proc.rate(1); }; fav.onClick = [this] { proc.rate(2); }; dislike.onClick = [this] { proc.rate(-1); };
    mutate.setTooltip("Random variation of the current sound (locked layers/params are kept)"); evolve.setTooltip("Gentle drift that keeps the identity of the sound");
    recall.setTooltip("Step back through previous sounds"); like.setTooltip("Teaches the local taste model (stored on this computer only)");
    mutAmount = std::make_unique<ModelKnob>("Amount", 0.0, 1.0, 0.01, "", [this] { return (double) mutateAmount; }, [this](double v) { mutateAmount = (float) v; }, col::turquoise);
    actions.addAndMakeVisible(*mutAmount);
    for (int i = 0; i < kMaxLayers; ++i)
    {
        regen[(size_t) i].setButtonText("New L" + juce::String(i + 1)); regen[(size_t) i].setTooltip("Regenerate only this layer"); actions.addAndMakeVisible(regen[(size_t) i]);
        regen[(size_t) i].onClick = [this, i] { proc.regenerateLayer(i); };
        lock[(size_t) i].setButtonText("Lock L" + juce::String(i + 1)); lock[(size_t) i].setTooltip("Locked layers survive mutate / evolve / regenerate"); actions.addAndMakeVisible(lock[(size_t) i]);
        lock[(size_t) i].onClick = [this, i] { const bool v = lock[(size_t) i].getToggleState(); proc.editMain([i, v](CandyDNA& d) { d.layers[(size_t) i].locked = v; }); };
    }
    for (auto* l : { &status, &nameLabel, &infoLabel, &tasteLabel }) { current.addAndMakeVisible(l); l->setInterceptsMouseClicks(false, false); }
    nameLabel.getProperties().set("ownFont", true); nameLabel.setFont(font(20.0f, true)); nameLabel.setColour(juce::Label::textColourId, col::text);
    infoLabel.setColour(juce::Label::textColourId, col::dim); tasteLabel.setColour(juce::Label::textColourId, col::dim); tasteLabel.setFont(font(11.0f));
    addAndMakeVisible(status); status.setColour(juce::Label::textColourId, col::turquoise);
    addAndMakeVisible(exportBar);
    for (int i = 0; i < 8; ++i) { cards.push_back(std::make_unique<Card>(*this, i)); addAndMakeVisible(*cards.back()); }
    refresh(0xffffffffu);
}

void GeneratePage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto left = r.removeFromLeft(340); r.removeFromLeft(8);
    macros.setBounds(left);
    auto c = macros.content();
    const int kw = c.getWidth() / 4, kh = 106;
    for (int i = 0; i < 7; ++i) knobs[(size_t) i]->setBounds(c.getX() + (i % 4) * kw, c.getY() + (i / 4) * kh, kw, kh);
    negKnob->setBounds(c.getX() + 3 * kw, c.getY() + kh, kw, kh);
    auto lo = c.withTrimmedTop(kh * 2 + 6);
    lenKnob->setBounds(lo.getX() + 3 * kw, lo.getY(), kw, kh);
    auto col1 = lo.removeFromLeft(kw * 3 - 8);
    { auto cr = col1.removeFromTop(44); useCategory.setBounds(cr.removeFromRight(86).withTrimmedTop(18)); cr.removeFromRight(4); combos[1]->setBounds(cr); }
    combos[0]->setBounds(col1.removeFromTop(44));
    auto row = col1.removeFromTop(44); combos[2]->setBounds(row.removeFromLeft(row.getWidth() / 2 - 3)); row.removeFromLeft(6); combos[3]->setBounds(row);

    auto top = r.removeFromTop(48); generate.setBounds(top.removeFromLeft(230)); top.removeFromLeft(8); cancelBtn.setBounds(top.removeFromLeft(80)); top.removeFromLeft(10); status.setBounds(top);
    r.removeFromTop(6);
    auto bottom = r.removeFromBottom(58); exportBar.setBounds(bottom);
    r.removeFromBottom(6);
    auto lower = r.removeFromBottom(150); r.removeFromBottom(6);
    // cards 4 x 2
    const int cw = r.getWidth() / 4, ch = r.getHeight() / 2;
    for (int i = 0; i < 8; ++i) cards[(size_t) i]->setBounds(r.getX() + (i % 4) * cw, r.getY() + (i / 4) * ch, cw, ch);
    current.setBounds(lower.removeFromLeft(lower.getWidth() * 4 / 10).reduced(0, 0)); lower.removeFromLeft(8); actions.setBounds(lower);
    auto cc = current.content(); nameLabel.setBounds(cc.removeFromTop(28)); infoLabel.setBounds(cc.removeFromTop(18)); tasteLabel.setBounds(cc);
    auto ac = actions.content(); mutAmount->setBounds(ac.removeFromLeft(70));
    auto r1 = ac.removeFromTop(28); const int bw = r1.getWidth() / 7; for (auto* b : { &mutate, &evolve, &recall, &audition, &like, &fav, &dislike }) b->setBounds(r1.removeFromLeft(bw).reduced(2, 0));
    ac.removeFromTop(6); auto r2 = ac.removeFromTop(28); const int lw = r2.getWidth() / 4; for (int i = 0; i < kMaxLayers; ++i) regen[(size_t) i].setBounds(r2.removeFromLeft(lw).reduced(2, 0));
    ac.removeFromTop(4); auto r3 = ac.removeFromTop(24); for (int i = 0; i < kMaxLayers; ++i) lock[(size_t) i].setBounds(r3.removeFromLeft(lw).reduced(4, 0));
}

void GeneratePage::refresh(uint32_t f)
{
    const auto d = proc.mainSound();
    if (f & (kChCandidates | kChSound | kChPreset | kChParamsLoaded))
    {
        auto cands = proc.candidates();
        for (size_t i = 0; i < cards.size(); ++i) cards[i]->set(i < cands.size() ? &cands[i] : nullptr, i < cands.size() && cands[i].dna.id == d.id && d.id != 0);
        nameLabel.setText(d.name, juce::dontSendNotification);
        juce::String eng; for (auto& l : d.layers) if (l.active) eng << (eng.isEmpty() ? "" : ", ") << engineName(l.engine);
        infoLabel.setText(juce::String(categoryName(d.category)) + " | " + juce::String(d.activeLayerCount()) + " layer(s): " + eng + " | " + juce::String((int) d.durationMs) + " ms | role " + roleName(d.role)
                          + (d.meta.favorite ? " | favourite" : ""), juce::dontSendNotification);
        for (int i = 0; i < kMaxLayers; ++i) { lock[(size_t) i].setToggleState(d.layers[(size_t) i].locked, juce::dontSendNotification); regen[(size_t) i].setEnabled(true); }
        if (d.id != lastMainId)
            recallDepth = 1;
        lastMainId = d.id;
    }
    if (f & (kChTaste | kChSound))
    {
        auto lines = proc.taste().describe();
        tasteLabel.setText(proc.taste().ratingCount() == 0 ? "Taste model: no ratings yet. Like / dislike sounds to teach DaliCandy what you prefer (stored locally, never uploaded)."
                                                           : "Taste model (" + juce::String(proc.taste().ratingCount()) + " ratings): " + lines.joinIntoString(";  "), juce::dontSendNotification);
    }
    if (f & (kChStatus | kChSound)) status.setText(proc.statusText(), juce::dontSendNotification);
}

void GeneratePage::tick()
{
    const bool b = proc.busy(); generate.setEnabled(! b); cancelBtn.setEnabled(b);
    if (b || progressShown > 0.0f) { progressShown = b ? proc.progress() : 0.0f; repaint(generate.getBounds().expanded(0, 0)); }
    status.setText(proc.statusText(), juce::dontSendNotification);
    lenKnob->setEnabled((int) proc.apvts.getRawParameterValue(lengthMode)->load() == 3);
}

void GeneratePage::paintOverChildren(juce::Graphics& g)
{
    if (progressShown > 0.001f) { auto r = generate.getBounds().toFloat().removeFromBottom(4.0f).reduced(3, 0); g.setColour(col::bg); g.fillRect(r); g.setColour(col::turquoise); g.fillRect(r.withWidth(r.getWidth() * progressShown)); }
}
}
