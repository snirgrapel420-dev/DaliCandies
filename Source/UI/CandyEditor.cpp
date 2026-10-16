#include "CandyEditor.h"

namespace dali::ui
{
void CandyEditor::Meter::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f); g.setColour(col::bg2); g.fillRoundedRectangle(r, 4.0f); g.setColour(col::line); g.drawRoundedRectangle(r, 4.0f, 1.0f);
    const float db = 20.0f * std::log10(std::max(1.0e-5f, shown)); const float u = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
    juce::ColourGradient gr(col::turquoise, r.getX(), 0, col::magenta, r.getRight(), 0, false); gr.addColour(0.8, col::purple); g.setGradientFill(gr);
    g.fillRoundedRectangle(r.reduced(2.0f).withWidth((r.getWidth() - 4.0f) * u), 3.0f);
    const float hu = std::clamp((20.0f * std::log10(std::max(1.0e-5f, hold)) + 48.0f) / 48.0f, 0.0f, 1.0f); g.setColour(juce::Colours::white); g.fillRect(r.getX() + 2.0f + (r.getWidth() - 4.0f) * hu, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
    g.setColour(db > -1.0f ? col::bad : col::dim); g.setFont(font(9.5f)); g.drawText(juce::String(db, 1) + " dB", getLocalBounds().withTrimmedLeft(4), juce::Justification::centredLeft);
}

CandyEditor::CandyEditor(CandyProcessor& p) : juce::AudioProcessorEditor(p), meter(p), proc(p)
{
    setLookAndFeel(&laf);
    pages[0] = std::make_unique<GeneratePage>(proc); pages[1] = std::make_unique<SequencePage>(proc); pages[2] = std::make_unique<SynthPage>(proc);
    pages[3] = std::make_unique<ModPage>(proc); pages[4] = std::make_unique<AnalysisPage>(proc);
    static const char* names[] = { "GENERATE", "SEQUENCE", "SYNTHESIS", "MODULATION", "TRACK ANALYSIS" };
    for (int i = 0; i < 5; ++i)
    {
        addChildComponent(*pages[(size_t) i]); tabs[(size_t) i].setButtonText(names[i]); tabs[(size_t) i].setClickingTogglesState(false);
        tabs[(size_t) i].setColour(juce::TextButton::buttonOnColourId, i % 2 ? col::turquoise : col::magenta); addAndMakeVisible(tabs[(size_t) i]); tabs[(size_t) i].onClick = [this, i] { showPage(i); };
    }
    for (auto* b : { &prev, &next, &presetName, &save, &fav, &rnd }) addAndMakeVisible(b);
    addAndMakeVisible(search); search.setTextToShowWhenEmpty("search presets...", col::dim); search.setFont(font(13.0f));
    search.onReturnKey = [this] { searchText = search.getText(); showSearchMenu(); }; search.onTextChange = [this] { searchText = search.getText(); };
    prev.onClick = [this] { stepPreset(-1); }; next.onClick = [this] { stepPreset(1); }; presetName.onClick = [this] { showPresetMenu(); };
    presetName.setTooltip("Click to browse factory and user presets"); save.onClick = [this] { savePresetDialog(); };
    fav.setClickingTogglesState(false); fav.setTooltip("Mark the current preset as favourite (stored with the user library)");
    fav.onClick = [this] { const int e = currentEntry(); if (e >= 0) { const bool v = ! proc.presets().entries()[(size_t) e].favorite; proc.presets().setFavorite(e, v); fav.setToggleState(v, juce::dontSendNotification); } };
    rnd.setTooltip("Load a random preset (favourites only when the filter box is empty and favourites exist)");
    rnd.onClick = [this] { static uint32_t n = juce::Time::getMillisecondCounter(); const int e = proc.presets().random(++n * 2654435761u); if (e >= 0) loadEntry(e); };
    addAndMakeVisible(meter);
    addAndMakeVisible(footerStatus); addAndMakeVisible(footerStats); footerStatus.setColour(juce::Label::textColourId, col::turquoise); footerStats.setColour(juce::Label::textColourId, col::dim); footerStats.setJustificationType(juce::Justification::centredRight);
    footerStatus.setFont(font(12.0f)); footerStats.setFont(font(11.0f));
    auto mk = [this](std::unique_ptr<juce::Slider>& s, std::unique_ptr<APVTS::SliderAttachment>& a, juce::Label& l, const char* id, const char* text)
    {
        s = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight); s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 18); addAndMakeVisible(*s);
        a = std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, *s); l.setText(text, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(font(11.0f)); addAndMakeVisible(l);
    };
    mk(outSlider, outAtt, outLabel, pid::outGain, "OUT"); mk(wetSlider, wetAtt, wetLabel, pid::wet, "CANDY");
    if (proc.isFx()) mk(drySlider, dryAtt, dryLabel, pid::dry, "DRY");
    quality = std::make_unique<ParamCombo>(proc.apvts, pid::quality, "Quality"); addAndMakeVisible(*quality);
    if (! proc.uiState.isObject()) proc.uiState = juce::var(new juce::DynamicObject());
    const int saved = (int) proc.uiState.getProperty("page", 0);
    constrainer.setSizeLimits(1040, 700, 1900, 1200); constrainer.setFixedAspectRatio(0.0);
    setConstrainer(&constrainer); setResizable(true, true);
    const int w = std::clamp((int) proc.uiState.getProperty("w", 1240), 1040, 1900), h = std::clamp((int) proc.uiState.getProperty("h", 800), 700, 1200);
    setSize(w, h);
    showPage(std::clamp(saved, 0, 4));
    for (auto& pg : pages) pg->refresh(0xffffffffu);
    startTimerHz(30);
}

CandyEditor::~CandyEditor()
{
    stopTimer();
    if (proc.uiState.isObject()) { proc.uiState.getDynamicObject()->setProperty("page", page); proc.uiState.getDynamicObject()->setProperty("w", getWidth()); proc.uiState.getDynamicObject()->setProperty("h", getHeight()); }
    setLookAndFeel(nullptr);
}

void CandyEditor::showPage(int i)
{
    page = std::clamp(i, 0, 4);
    for (int k = 0; k < 5; ++k) { pages[(size_t) k]->setVisible(k == page); tabs[(size_t) k].setToggleState(k == page, juce::dontSendNotification); }
    pages[(size_t) page]->refresh(0xffffffffu);
    if (proc.uiState.isObject()) proc.uiState.getDynamicObject()->setProperty("page", page);
}

void CandyEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);
    juce::ColourGradient bgG(col::bg2, 0, 0, col::bg, 0, (float) getHeight(), false); g.setGradientFill(bgG); g.fillAll();
    auto h = getLocalBounds().removeFromTop(66).toFloat();
    g.setGradientFill(juce::ColourGradient(col::panelHi, 0, 0, col::bg2, 0, h.getBottom(), false)); g.fillRect(h);
    g.setColour(col::magenta.withAlpha(0.7f)); g.fillRect(0.0f, h.getBottom() - 1.5f, (float) getWidth() * 0.35f, 1.5f); g.setColour(col::turquoise.withAlpha(0.7f)); g.fillRect((float) getWidth() * 0.35f, h.getBottom() - 1.5f, (float) getWidth() * 0.65f, 1.5f);
    // brand
    g.setFont(font(24.0f, true)); g.setColour(col::magenta.withAlpha(0.35f)); g.drawText("DaliCandy", 17, 7, 200, 30, juce::Justification::centredLeft); g.setColour(col::text); g.drawText("DaliCandy", 16, 6, 200, 30, juce::Justification::centredLeft);
    g.setFont(font(11.0f, true)); g.setColour(col::turquoise); g.drawText(juce::String("DALI AUDIO  |  ") + (proc.isFx() ? "INSTRUMENT + FX" : "INSTRUMENT"), 18, 36, 200, 14, juce::Justification::centredLeft);
}

void CandyEditor::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop(66).reduced(8, 6); head.removeFromLeft(190);
    auto row1 = head.removeFromTop(26);
    prev.setBounds(row1.removeFromLeft(28)); row1.removeFromLeft(3); presetName.setBounds(row1.removeFromLeft(300)); row1.removeFromLeft(3); next.setBounds(row1.removeFromLeft(28)); row1.removeFromLeft(8);
    save.setBounds(row1.removeFromLeft(54)); row1.removeFromLeft(4); fav.setBounds(row1.removeFromLeft(44)); row1.removeFromLeft(4); rnd.setBounds(row1.removeFromLeft(70)); row1.removeFromLeft(8);
    meter.setBounds(row1.removeFromRight(180)); row1.removeFromRight(8); search.setBounds(row1.removeFromLeft(std::min(220, row1.getWidth())));
    head.removeFromTop(4); auto row2 = head; const int tw = std::min(150, row2.getWidth() / 5);
    for (auto& t : tabs) t.setBounds(row2.removeFromLeft(tw).reduced(2, 0));
    auto foot = r.removeFromBottom(38).reduced(10, 4);
    footerStatus.setBounds(foot.removeFromLeft(foot.getWidth() * 34 / 100)); footerStats.setBounds(foot.removeFromRight(210));
    quality->setBounds(foot.removeFromRight(120).withTrimmedTop(0)); foot.removeFromRight(8);
    const int sw = std::min(200, foot.getWidth() / (proc.isFx() ? 3 : 2));
    auto place = [&](juce::Label& l, std::unique_ptr<juce::Slider>& s) { if (! s) return; auto b = foot.removeFromLeft(sw); l.setBounds(b.removeFromLeft(44)); s->setBounds(b); };
    place(outLabel, outSlider); place(wetLabel, wetSlider); place(dryLabel, drySlider);
    for (auto& p : pages) p->setBounds(r);
}

juce::Array<int> CandyEditor::visibleEntries() const
{
    juce::Array<int> a; for (int i : proc.presets().search(searchText, -1, favOnly)) a.add(i); return a;
}
int CandyEditor::currentEntry() const
{
    const auto n = proc.presetSnapshot().name; const auto& e = proc.presets().entries();
    for (size_t i = 0; i < e.size(); ++i) if (e[i].name == n) return (int) i;
    return -1;
}

void CandyEditor::loadEntry(int idx)
{
    Preset p; juce::String err;
    if (proc.presets().load(idx, p, &err)) { proc.loadPreset(p); fav.setToggleState(proc.presets().entries()[(size_t) idx].favorite, juce::dontSendNotification); }
    else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", "Could not load preset: " + err);
}

void CandyEditor::stepPreset(int dir)
{
    auto v = visibleEntries();
    if (v.isEmpty())
        return;
    const int cur = currentEntry(); int pos = v.indexOf(cur);
    pos = pos < 0 ? (dir > 0 ? 0 : v.size() - 1) : (pos + dir + v.size()) % v.size(); loadEntry(v[pos]);
}

void CandyEditor::showPresetMenu()
{
    const auto& e = proc.presets().entries(); juce::PopupMenu m, factory, user, favs;
    juce::PopupMenu cats[kNumCategories]; bool anyUser = false;
    for (size_t i = 0; i < e.size(); ++i)
    {
        const int id = 1000 + (int) i; const bool tick = (int) i == currentEntry();
        if (e[i].factory) cats[(int) e[i].category].addItem(id, e[i].name, true, tick); else { user.addItem(id, e[i].name, true, tick); anyUser = true; }
        if (e[i].favorite) favs.addItem(id + 100000, e[i].name);
    }
    for (int c = 0; c < kNumCategories; ++c) factory.addSubMenu(categoryName((Category) c), cats[c]);
    m.addSubMenu("Factory", factory); if (anyUser) m.addSubMenu("User", user); else m.addItem(-1, "User (empty)", false); m.addSubMenu("Favourites", favs);
    m.addSeparator(); m.addItem(1, "Import preset file..."); m.addItem(2, "Export current preset..."); m.addItem(3, "Open user preset folder"); m.addItem(4, "Rescan presets"); m.addItem(5, "Rename user preset..."); m.addItem(6, "Delete user preset");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetName).withMinimumWidth(300), [this](int r)
    {
        if (r >= 101000) loadEntry(r - 101000); else if (r >= 1000) loadEntry(r - 1000);
        else if (r == 3) proc.presets().userDir().revealToUser();
        else if (r == 4) { proc.presets().rescan(); }
        else if (r == 1)
        {
            auto fc = std::make_shared<juce::FileChooser>("Import preset", juce::File(), "*.dalicandy");
            fc->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, fc](const juce::FileChooser& c)
            { if (c.getResult().existsAsFile()) { juce::String err; if (! proc.presets().importFile(c.getResult(), &err)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import", err); } });
        }
        else if (r == 2)
        {
            const int cur = currentEntry();
            auto fc = std::make_shared<juce::FileChooser>("Export preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(sanitizeFileName(proc.presetSnapshot().name) + ".dalicandy"), "*.dalicandy");
            fc->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, fc, cur](const juce::FileChooser& c)
            {
                if (c.getResult() == juce::File())
                    return;
                juce::String err;
                bool ok;
                if (cur >= 0) ok = proc.presets().exportTo(cur, c.getResult(), &err); else { ok = c.getResult().replaceWithText(proc.currentAsPreset().toJson()); if (! ok) err = "could not write file"; }
                if (! ok) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Export", err);
            });
        }
        else if (r == 5 || r == 6)
        {
            const int cur = currentEntry();
            if (cur < 0 || proc.presets().entries()[(size_t) cur].factory)
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Preset", "Select a user preset first. Factory presets are read-only.");
                return;
            }
            juce::String err;
            if (r == 6) { if (! proc.presets().remove(cur, &err)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Delete", err); }
            else
            {
                auto* w = new juce::AlertWindow("Rename preset", "New name:", juce::MessageBoxIconType::NoIcon); w->addTextEditor("n", proc.presets().entries()[(size_t) cur].name); w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, cur](int res) { if (res == 1) { juce::String e2; if (! proc.presets().rename(cur, w->getTextEditorContents("n"), &e2)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Rename", e2); } }), true);
            }
        }
    });
}

void CandyEditor::showSearchMenu()
{
    auto v = visibleEntries(); juce::PopupMenu m; const auto& e = proc.presets().entries();
    if (v.isEmpty()) m.addItem(-1, "No presets match", false);
    for (int i = 0; i < std::min(60, v.size()); ++i) m.addItem(1000 + v[i], e[(size_t) v[i]].name + "   (" + categoryName(e[(size_t) v[i]].category) + (e[(size_t) v[i]].factory ? "" : ", user") + ")");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&search), [this](int r) { if (r >= 1000) loadEntry(r - 1000); });
}

void CandyEditor::savePresetDialog()
{
    auto* w = new juce::AlertWindow("Save preset to the user bank", "Name, then tags (comma separated):", juce::MessageBoxIconType::NoIcon);
    const auto cur = proc.presetSnapshot(); w->addTextEditor("name", cur.name == "Init" ? proc.mainSound().name : cur.name, "Name"); w->addTextEditor("tags", cur.tags.joinIntoString(", "), "Tags");
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int res)
    {
        if (res != 1)
            return;
        const auto name = w->getTextEditorContents("name").trim();
        if (name.isEmpty())
            return;
        juce::StringArray tags; tags.addTokens(w->getTextEditorContents("tags"), ",", ""); tags.trim(); tags.removeEmptyStrings();
        proc.setPresetInfo(name, tags, "User"); auto p = proc.currentAsPreset(); juce::String err;
        auto doSave = [this, p](bool overwrite) { juce::String e2; if (! proc.presets().save(p, overwrite, &e2)) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save", e2); };
        if (! proc.presets().save(p, false, &err))
        {
            if (err.containsIgnoreCase("exist")) juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Overwrite?", "A user preset named \"" + name + "\" already exists. Overwrite it?", "Overwrite", "Cancel", nullptr, juce::ModalCallbackFunction::create([doSave](int r) { if (r == 1) doSave(true); }));
            else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Save", err);
        }
    }), true);
}

void CandyEditor::timerCallback()
{
    const uint32_t f = proc.takeUiFlags();
    if (f != 0)
        pages[(size_t) page]->refresh(f);          // hidden pages are refreshed in full when they are shown
    pages[(size_t) page]->tick();
    const auto n = proc.presetSnapshot().name;
    if (n != shownPreset || f & (kChPreset | kChParamsLoaded)) { shownPreset = n; presetName.setButtonText(n); const int e = currentEntry(); fav.setToggleState(e >= 0 && proc.presets().entries()[(size_t) e].favorite, juce::dontSendNotification); }
    const float pk = proc.outputPeak(); meter.shown = std::max(pk, meter.shown * 0.86f); meter.hold = std::max(pk, meter.hold * 0.985f); meter.repaint();
    footerStatus.setText(proc.statusText(), juce::dontSendNotification);
    footerStats.setText("CPU " + juce::String((int) (proc.cpuLoad() * 100.0f)) + " %   voices " + juce::String(proc.activeVoices()) + "   protect " + juce::String((int) proc.protectCount()), juce::dontSendNotification);
}
}
