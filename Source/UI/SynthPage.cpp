#include "Pages.h"
#include <random>

namespace dali::ui
{
void SynthPage::Bands::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat(); const float w = r.getWidth() / 8.0f;
    for (int i = 0; i < 8; ++i)
    {
        const float h = std::clamp(std::sqrt(v[(size_t) i]) * 1.4f, 0.0f, 1.0f) * (r.getHeight() - 14.0f);
        auto b = juce::Rectangle<float>(r.getX() + (float) i * w + 3.0f, r.getBottom() - 14.0f - h, w - 6.0f, h);
        g.setColour(col::purple.withAlpha(0.25f)); g.fillRoundedRectangle(b.expanded(2.0f), 3.0f); g.setColour(i < 3 ? col::purple : i < 6 ? col::magenta : col::turquoise); g.fillRoundedRectangle(b, 2.0f);
        g.setColour(col::dim); g.setFont(font(9.5f)); g.drawText(juce::StringArray { "Sub", "Bass", "Low", "Mid", "HiMid", "Pres", "Air", "Top" }[i], (int) (r.getX() + (float) i * w), (int) r.getBottom() - 13, (int) w, 12, juce::Justification::centred);
    }
}

SynthPage::SynthPage(CandyProcessor& p) : PageBase(p)
{
    for (auto* c : { &pLayer, &pEngine, &pEnv, &pFx, &pView }) addAndMakeVisible(c);
    for (int i = 0; i < kMaxLayers; ++i)
    {
        auto& t = tabs[(size_t) i]; t.setButtonText("Layer " + juce::String(i + 1)); t.setClickingTogglesState(false); pLayer.addAndMakeVisible(t);
        t.onClick = [this, i] { setLayer(i); };
    }
    for (auto* t : { &active, &mute, &solo, &locked }) pLayer.addAndMakeVisible(t);
    active.onClick = [this] { const bool v = active.getToggleState(); proc.editMain([this, v](CandyDNA& d) { auto& l = d.layers[(size_t) layer];
        if (v && ! l.active) { const float pitch = d.layers[0].pitch; initLayer(l, EngineType::MicroSynth, l.seed + 1u); l.pitch = pitch; } else if (! v) l.active = false; }); };
    mute.onClick = [this] { const bool v = mute.getToggleState(); edit([v](LayerConfig& l) { l.mute = v; }); };
    solo.onClick = [this] { const bool v = solo.getToggleState(); edit([v](LayerConfig& l) { l.solo = v; }); };
    locked.onClick = [this] { const bool v = locked.getToggleState(); edit([v](LayerConfig& l) { l.locked = v; }); };
    pLayer.addAndMakeVisible(engine); pLayer.addAndMakeVisible(engineLabel);
    for (int i = 0; i < kNumEngines; ++i) engine.addItem(engineName((EngineType) i), i + 1);
    engine.onChange = [this] { const auto e = (EngineType) (engine.getSelectedId() - 1);
        edit([e](LayerConfig& l) { if (l.engine == e) return; const auto k = l; initLayer(l, e, k.seed); l.gainDb = k.gainDb; l.pitch = k.pitch; l.pan = k.pan; l.attackMs = k.attackMs; l.decayMs = k.decayMs; l.sustain = k.sustain; l.releaseMs = k.releaseMs;
            l.durationMs = k.durationMs; l.startOffsetMs = k.startOffsetMs; l.sendDelay = k.sendDelay; l.sendReverb = k.sendReverb; l.sendMod = k.sendMod; l.insert = k.insert; l.insertA = k.insertA; l.insertB = k.insertB; l.mute = k.mute; l.solo = k.solo; l.locked = k.locked; }); };
    engineLabel.setText("Engine", juce::dontSendNotification); engineLabel.setColour(juce::Label::textColourId, col::dim);

    auto addEnv = [this](const juce::String& cap, double lo, double hi, double step, const juce::String& suf, std::function<double(const LayerConfig&)> get, std::function<void(LayerConfig&, double)> set, juce::Colour c, double mid = 0.0)
    {
        envKnobs.push_back(std::make_unique<ModelKnob>(cap, lo, hi, step, suf, [this, get] { return get(cur.layers[(size_t) layer]); }, [this, set](double v) { edit([set, v](LayerConfig& l) { set(l, v); }); }, c, mid));
        pEnv.addAndMakeVisible(*envKnobs.back());
    };
    cur = proc.mainSound();
    addEnv("Gain", -60, 6, 0.1, " dB", [](auto& l) { return (double) l.gainDb; }, [](auto& l, double v) { l.gainDb = (float) v; }, col::magenta, -18);
    addEnv("Pan", -1, 1, 0.01, "", [](auto& l) { return (double) l.pan; }, [](auto& l, double v) { l.pan = (float) v; }, col::turquoise);
    addEnv("Pitch", 12, 108, 0.01, " st", [](auto& l) { return (double) l.pitch; }, [](auto& l, double v) { l.pitch = (float) v; }, col::magenta);
    addEnv("Attack", 0, 2000, 0.1, " ms", [](auto& l) { return (double) l.attackMs; }, [](auto& l, double v) { l.attackMs = (float) v; }, col::purple, 40);
    addEnv("Decay", 1, 8000, 1, " ms", [](auto& l) { return (double) l.decayMs; }, [](auto& l, double v) { l.decayMs = (float) v; }, col::purple, 400);
    addEnv("Sustain", 0, 1, 0.01, "", [](auto& l) { return (double) l.sustain; }, [](auto& l, double v) { l.sustain = (float) v; }, col::purple);
    addEnv("Release", 1, 8000, 1, " ms", [](auto& l) { return (double) l.releaseMs; }, [](auto& l, double v) { l.releaseMs = (float) v; }, col::purple, 400);
    addEnv("Length", 1, 8000, 1, " ms", [](auto& l) { return (double) l.durationMs; }, [](auto& l, double v) { l.durationMs = (float) v; }, col::turquoise, 500);
    addEnv("Offset", 0, 2000, 1, " ms", [](auto& l) { return (double) l.startOffsetMs; }, [](auto& l, double v) { l.startOffsetMs = (float) v; }, col::turquoise, 200);

    auto addFx = [this](const juce::String& cap, double lo, double hi, double step, const juce::String& suf, std::function<double(const CandyDNA&, const LayerConfig&)> get, std::function<void(CandyDNA&, LayerConfig&, double)> set, juce::Colour c, double mid = 0.0)
    {
        fxKnobs.push_back(std::make_unique<ModelKnob>(cap, lo, hi, step, suf, [this, get] { return get(cur, cur.layers[(size_t) layer]); },
            [this, set](double v) { proc.editMain([this, set, v](CandyDNA& d) { set(d, d.layers[(size_t) layer], v); }); }, c, mid));
        pFx.addAndMakeVisible(*fxKnobs.back());
    };
    addFx("Insert A", 0, 1, 0.01, "", [](auto&, auto& l) { return (double) l.insertA; }, [](auto&, auto& l, double v) { l.insertA = (float) v; }, col::magenta);
    addFx("Insert B", 0, 1, 0.01, "", [](auto&, auto& l) { return (double) l.insertB; }, [](auto&, auto& l, double v) { l.insertB = (float) v; }, col::magenta);
    addFx("Send Delay", 0, 1, 0.01, "", [](auto&, auto& l) { return (double) l.sendDelay; }, [](auto&, auto& l, double v) { l.sendDelay = (float) v; }, col::turquoise);
    addFx("Send Reverb", 0, 1, 0.01, "", [](auto&, auto& l) { return (double) l.sendReverb; }, [](auto&, auto& l, double v) { l.sendReverb = (float) v; }, col::turquoise);
    addFx("Send Mod", 0, 1, 0.01, "", [](auto&, auto& l) { return (double) l.sendMod; }, [](auto&, auto& l, double v) { l.sendMod = (float) v; }, col::turquoise);
    addFx("Dly Feedback", 0, 0.95, 0.01, "", [](auto& d, auto&) { return (double) d.fx.delayFeedback; }, [](auto& d, auto&, double v) { d.fx.delayFeedback = (float) v; }, col::purple);
    addFx("Dly Tone", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.delayTone; }, [](auto& d, auto&, double v) { d.fx.delayTone = (float) v; }, col::purple);
    addFx("Dly Level", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.delayLevel; }, [](auto& d, auto&, double v) { d.fx.delayLevel = (float) v; }, col::purple);
    addFx("Rev Size", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.reverbSize; }, [](auto& d, auto&, double v) { d.fx.reverbSize = (float) v; }, col::magenta);
    addFx("Rev Damp", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.reverbDamp; }, [](auto& d, auto&, double v) { d.fx.reverbDamp = (float) v; }, col::magenta);
    addFx("Rev Level", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.reverbLevel; }, [](auto& d, auto&, double v) { d.fx.reverbLevel = (float) v; }, col::magenta);
    addFx("Mod Rate", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.modRate; }, [](auto& d, auto&, double v) { d.fx.modRate = (float) v; }, col::turquoise);
    addFx("Mod Depth", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.modDepth; }, [](auto& d, auto&, double v) { d.fx.modDepth = (float) v; }, col::turquoise);
    addFx("Mod Fdbk", 0, 0.95, 0.01, "", [](auto& d, auto&) { return (double) d.fx.modFeedback; }, [](auto& d, auto&, double v) { d.fx.modFeedback = (float) v; }, col::turquoise);
    addFx("Mod Level", 0, 1, 0.01, "", [](auto& d, auto&) { return (double) d.fx.modLevel; }, [](auto& d, auto&, double v) { d.fx.modLevel = (float) v; }, col::turquoise);
    addFx("Width", 0, 2, 0.01, "", [](auto& d, auto&) { return (double) d.fx.width; }, [](auto& d, auto&, double v) { d.fx.width = (float) v; }, col::magenta);
    addFx("Master", -24, 12, 0.1, " dB", [](auto& d, auto&) { return (double) d.fx.masterGainDb; }, [](auto& d, auto&, double v) { d.fx.masterGainDb = (float) v; }, col::magenta);
    addFx("Low cut", 20, 600, 1, " Hz", [](auto& d, auto&) { return (double) d.fx.lowCutHz; }, [](auto& d, auto&, double v) { d.fx.lowCutHz = (float) v; }, col::magenta, 90);

    for (int i = 0; i < (int) InsertFx::Count; ++i) insert.addItem(insertName((InsertFx) i), i + 1);
    insert.onChange = [this] { const auto v = (InsertFx) (insert.getSelectedId() - 1); edit([v](LayerConfig& l) { l.insert = v; }); };
    for (auto* n : { "Chorus", "Flanger", "Phaser" }) modType.addItem(n, modType.getNumItems() + 1);
    modType.onChange = [this] { const int v = modType.getSelectedId() - 1; proc.editMain([v](CandyDNA& d) { d.fx.modType = v; }); };
    for (int i = 0; i < kNumDivisions; ++i) delayDiv.addItem(divisionName(i), i + 1);
    delayDiv.onChange = [this] { const int v = delayDiv.getSelectedId() - 1; proc.editMain([v](CandyDNA& d) { d.fx.delayDiv = v; }); };
    pingpong.addItem("Stereo delay", 1); pingpong.addItem("Ping-pong delay", 2);
    pingpong.onChange = [this] { const bool v = pingpong.getSelectedId() == 2; proc.editMain([v](CandyDNA& d) { d.fx.pingPong = v; }); };
    for (auto* c : { &insert, &modType, &delayDiv, &pingpong })
        pFx.addAndMakeVisible(c);
    for (auto* l : { &capInsert, &capDelay, &capMode, &capMod }) { pFx.addAndMakeVisible(l); l->setColour(juce::Label::textColourId, col::dim); l->setFont(font(11.5f)); l->setJustificationType(juce::Justification::centred); }
    capInsert.setText("Insert effect", juce::dontSendNotification); capDelay.setText("Delay time", juce::dontSendNotification); capMode.setText("Delay mode", juce::dontSendNotification); capMod.setText("Mod FX type", juce::dontSendNotification);
    pView.addAndMakeVisible(wave); pView.addAndMakeVisible(bands); pView.addAndMakeVisible(specInfo); specInfo.setColour(juce::Label::textColourId, col::dim); specInfo.setFont(font(11.0f));
    setLayer(0);
}

void SynthPage::setLayer(int l)
{
    layer = std::clamp(l, 0, kMaxLayers - 1);
    for (int i = 0; i < kMaxLayers; ++i) tabs[(size_t) i].setToggleState(i == layer, juce::dontSendNotification);
    rebuildEngineKnobs(); pullAll();
}

void SynthPage::rebuildEngineKnobs()
{
    for (auto& k : engineKnobs) pEngine.removeChildComponent(k.get());
    engineKnobs.clear();
    const auto e = cur.layers[(size_t) layer].engine;
    for (int i = 0; i < kParamsPerLayer; ++i)
    {
        auto k = std::make_unique<ModelKnob>(engineParamName(e, i), 0.0, 1.0, 0.001, "", [this, i] { return (double) cur.layers[(size_t) layer].p[(size_t) i]; },
            [this, i](double v) { edit([i, v](LayerConfig& l) { l.p[(size_t) i] = (float) v; }); }, i < 8 ? col::magenta : col::turquoise);
        auto* kp = k.get();
        k->onContext = [this, i, kp]
        {
            juce::PopupMenu m; const bool isLocked = (cur.layers[(size_t) layer].lockMask >> i) & 1;
            m.addItem(1, isLocked ? "Unlock parameter" : "Lock parameter (kept by Mutate / Regenerate)");
            m.addItem(2, "Randomise this parameter");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(kp), [this, i](int r)
            {
                if (r == 1) edit([i](LayerConfig& l) { l.lockMask = (uint16_t) (l.lockMask ^ (1u << i)); });
                if (r == 2) { std::mt19937 g{ std::random_device{}() }; const float v = std::uniform_real_distribution<float>(0.0f, 1.0f)(g); edit([i, v](LayerConfig& l) { l.p[(size_t) i] = v; }); }
            });
        };
        pEngine.addAndMakeVisible(*k); engineKnobs.push_back(std::move(k));
    }
    resized();
}

void SynthPage::pullAll()
{
    const auto& l = cur.layers[(size_t) layer];
    active.setToggleState(l.active, juce::dontSendNotification); mute.setToggleState(l.mute, juce::dontSendNotification); solo.setToggleState(l.solo, juce::dontSendNotification); locked.setToggleState(l.locked, juce::dontSendNotification);
    engine.setSelectedId((int) l.engine + 1, juce::dontSendNotification); insert.setSelectedId((int) l.insert + 1, juce::dontSendNotification);
    modType.setSelectedId(std::clamp(cur.fx.modType, 0, 2) + 1, juce::dontSendNotification); delayDiv.setSelectedId(std::clamp(cur.fx.delayDiv, 0, kNumDivisions - 1) + 1, juce::dontSendNotification);
    pingpong.setSelectedId(cur.fx.pingPong ? 2 : 1, juce::dontSendNotification);
    for (auto& k : engineKnobs)
        k->pull();
    for (size_t i = 0; i < engineKnobs.size(); ++i) engineKnobs[i]->setCaption(juce::String(((l.lockMask >> i) & 1) ? "* " : "") + engineParamName(l.engine, (int) i));
    for (auto& k : envKnobs) k->pull();
    for (auto& k : fxKnobs) k->pull();
    for (int i = 0; i < kMaxLayers; ++i) { tabs[(size_t) i].setButtonText("Layer " + juce::String(i + 1) + (cur.layers[(size_t) i].active ? "" : " (off)")); }
    for (int i = 0; i < 8; ++i)
        bands.v[(size_t) i] = cur.spectral.bands[(size_t) i];
    bands.repaint();
    specInfo.setText("Centroid " + juce::String((int) cur.spectral.centroidHz) + " Hz | peak " + juce::String(cur.spectral.peakDb, 1) + " dB | RMS " + juce::String(cur.spectral.rmsDb, 1) + " dB | crest " + juce::String(cur.spectral.crest, 1) + " dB"
                     + " | flatness " + juce::String(cur.spectral.flatness, 2), juce::dontSendNotification);
    const bool on = l.active; for (auto& k : engineKnobs) k->setEnabled(on); for (auto& k : envKnobs) k->setEnabled(on); engine.setEnabled(on);
}

void SynthPage::refresh(uint32_t f)
{
    if (f & (kChSound | kChPreset | kChParamsLoaded))
    {
        const auto before = cur.layers[(size_t) layer].engine; cur = proc.mainSound();
        if (cur.layers[(size_t) layer].engine != before || engineKnobs.empty()) rebuildEngineKnobs();
        pullAll(); previewDirty = true; previewCountdown = 8;
    }
}

void SynthPage::tick()
{
    if (previewDirty && --previewCountdown <= 0 && isVisible())
    {
        previewDirty = false;
        ExportFormat fmt; fmt.sampleRate = 44100.0; fmt.quality = QualityMode::Eco; fmt.bitDepth = 24; fmt.trimSilence = true;
        auto b = proc.renderPreview(fmt, ExportKind::OneShot, ExportMix::Full, -1);
        wave.setWave(minMaxPairs(b, 360));
    }
}

void SynthPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto left = r.removeFromLeft(r.getWidth() * 42 / 100); r.removeFromLeft(8);
    pLayer.setBounds(left.removeFromTop(104)); left.removeFromTop(8);
    pView.setBounds(left.removeFromBottom(168)); left.removeFromBottom(8);
    pEngine.setBounds(left);
    { auto c = pLayer.content(); auto t = c.removeFromTop(26); const int w = t.getWidth() / 4; for (auto& b : tabs) b.setBounds(t.removeFromLeft(w).reduced(2, 0));
      c.removeFromTop(4); auto row = c.removeFromTop(26); active.setBounds(row.removeFromLeft(62)); mute.setBounds(row.removeFromLeft(56)); solo.setBounds(row.removeFromLeft(52)); locked.setBounds(row.removeFromLeft(90));
      engineLabel.setBounds(row.removeFromLeft(48)); engine.setBounds(row.reduced(0, 1)); }
    { auto c = pEngine.content(); const int w = c.getWidth() / 4, h = c.getHeight() / 4; for (size_t i = 0; i < engineKnobs.size(); ++i) engineKnobs[i]->setBounds(c.getX() + (int) (i % 4) * w, c.getY() + (int) (i / 4) * h, w, h); }
    pEnv.setBounds(r.removeFromTop(r.getHeight() * 36 / 100)); r.removeFromTop(8); pFx.setBounds(r);
    { auto c = pEnv.content(); const int w = c.getWidth() / 5, h = c.getHeight() / 2; for (size_t i = 0; i < envKnobs.size(); ++i) envKnobs[i]->setBounds(c.getX() + (int) (i % 5) * w, c.getY() + (int) (i / 5) * h, w, h); }
    { auto c = pFx.content(); const int rowH = c.getHeight() / 4; const int w = c.getWidth() / 6;
      auto row = [&](int i) { return juce::Rectangle<int>(c.getX(), c.getY() + i * rowH, c.getWidth(), rowH); };
      auto cb = [](juce::Rectangle<int> b) { return b.withSizeKeepingCentre(b.getWidth() - 6, 26).translated(0, 8); };
      auto cap = [this](juce::Label& l, juce::Rectangle<int> b) { l.setBounds(b.withSizeKeepingCentre(b.getWidth() - 6, 14).translated(0, -22)); };
      auto r0 = row(0); { auto b = r0.removeFromLeft(w); insert.setBounds(cb(b)); cap(capInsert, cb(b)); } for (int i = 0; i < 5; ++i) fxKnobs[(size_t) i]->setBounds(r0.removeFromLeft(w));
      auto r1 = row(1); { auto b = r1.removeFromLeft(w); delayDiv.setBounds(cb(b)); cap(capDelay, cb(b)); } for (int i = 5; i < 8; ++i) fxKnobs[(size_t) i]->setBounds(r1.removeFromLeft(w)); { auto b = r1.removeFromLeft(w * 2); pingpong.setBounds(cb(b)); cap(capMode, cb(b)); }
      auto r2 = row(2); for (int i = 8; i < 11; ++i) fxKnobs[(size_t) i]->setBounds(r2.removeFromLeft(w)); { auto b = r2.removeFromLeft(w); modType.setBounds(cb(b)); cap(capMod, cb(b)); } for (int i = 11; i < 13; ++i) fxKnobs[(size_t) i]->setBounds(r2.removeFromLeft(w));
      auto r3 = row(3); for (int i = 13; i < 18; ++i) fxKnobs[(size_t) i]->setBounds(r3.removeFromLeft(w)); }
    { auto c = pView.content(); specInfo.setBounds(c.removeFromBottom(16)); auto b = c.removeFromRight(c.getWidth() * 35 / 100); bands.setBounds(b.reduced(4, 0)); wave.setBounds(c); }
}
}
