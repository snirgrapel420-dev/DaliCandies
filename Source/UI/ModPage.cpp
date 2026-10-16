#include "Pages.h"
#include <random>

namespace dali::ui
{
namespace
{
juce::StringArray destNames() { juce::StringArray a; for (int d = 0; d < kNumDestinations; ++d) a.add(modDestName(d)); return a; }
juce::StringArray sourceNames() { juce::StringArray a; for (int s = 0; s < kNumModSources; ++s) a.add(modSourceName(s)); return a; }

// Draggable bars (step sequencer / custom LFO curve)
struct BarEditor : juce::Component
{
    std::function<float(int)> get; std::function<void(int, float)> set; std::function<int()> count;
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); g.setColour(col::bg2); g.fillRoundedRectangle(r, 6.0f); g.setColour(col::line); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
        const int n = count(); const float w = (r.getWidth() - 8.0f) / (float) n;
        for (int i = 0; i < n; ++i) { const float v = std::clamp(get(i), 0.0f, 1.0f), h = v * (r.getHeight() - 8.0f); auto b = juce::Rectangle<float>(r.getX() + 4.0f + (float) i * w + 1.5f, r.getBottom() - 4.0f - h, w - 3.0f, h);
            g.setColour(col::magenta.withAlpha(0.25f)); g.fillRoundedRectangle(b.expanded(1.5f), 3.0f); g.setColour(i % 4 == 0 ? col::magenta : col::purple); g.fillRoundedRectangle(b, 2.0f); }
    }
    void edit(const juce::MouseEvent& e)
    {
        const int n = count(); const float w = (getWidth() - 8.0f) / (float) n; const int i = std::clamp((int) ((e.x - 4.0f) / w), 0, n - 1);
        set(i, std::clamp(1.0f - (e.y - 4.0f) / (getHeight() - 8.0f), 0.0f, 1.0f)); repaint();
    }
    void mouseDown(const juce::MouseEvent& e) override { edit(e); }
    void mouseDrag(const juce::MouseEvent& e) override { edit(e); }
};

// Multi-segment envelope editor: drag points; shift-drag a point vertically to bend the segment that ends there.
struct MsegEditor : juce::Component
{
    std::function<MsegCfg()> get; std::function<void(const std::function<void(MsegCfg&)>&)> change; int drag = -1; bool bend = false;
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(6.0f); g.setColour(col::bg2); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f); g.setColour(col::line); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.0f, 1.0f);
        const auto m = get(); const int n = std::clamp(m.numPoints, 2, kMsegPoints); juce::Path p;
        auto pos = [&](float t, float v) { return juce::Point<float>(r.getX() + t * r.getWidth(), r.getBottom() - v * r.getHeight()); };
        p.startNewSubPath(pos(0, m.v[0]));
        for (int k = 1; k < n; ++k) { const auto a = pos(m.t[(size_t) k - 1], m.v[(size_t) k - 1]), b = pos(m.t[(size_t) k], m.v[(size_t) k]);
            for (int s = 1; s <= 16; ++s) { const float f = (float) s / 16.0f; const float sh = std::pow(f, std::exp(m.c[(size_t) k] * 1.6f)); p.lineTo(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * sh); } }
        g.setColour(col::turquoise.withAlpha(0.25f)); g.strokePath(p, juce::PathStrokeType(4.0f)); g.setColour(col::turquoise); g.strokePath(p, juce::PathStrokeType(1.6f));
        for (int k = 0; k < n; ++k) { const auto q = pos(m.t[(size_t) k], m.v[(size_t) k]); g.setColour(k == drag ? juce::Colours::white : col::magenta); g.fillEllipse(q.x - 5.0f, q.y - 5.0f, 10.0f, 10.0f); }
        g.setColour(col::dim); g.setFont(font(10.0f)); g.drawText("drag points | shift-drag = bend segment", getLocalBounds().removeFromBottom(14).reduced(6, 0), juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat().reduced(6.0f); const auto m = get(); const int n = std::clamp(m.numPoints, 2, kMsegPoints); float best = 144.0f; drag = -1; bend = e.mods.isShiftDown();
        for (int k = 0; k < n; ++k) { const float dx = r.getX() + m.t[(size_t) k] * r.getWidth() - e.x, dy = r.getBottom() - m.v[(size_t) k] * r.getHeight() - e.y; const float d = dx * dx + dy * dy; if (d < best) { best = d; drag = k; } }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (drag < 0)
            return;
        auto r = getLocalBounds().toFloat().reduced(6.0f); const int k = drag;
        const float t = std::clamp((e.x - r.getX()) / r.getWidth(), 0.0f, 1.0f), v = std::clamp(1.0f - (e.y - r.getY()) / r.getHeight(), 0.0f, 1.0f);
        const bool b = bend;
        change([k, t, v, b](MsegCfg& m) { const int n = std::clamp(m.numPoints, 2, kMsegPoints);
            if (b) { if (k > 0) m.c[(size_t) k] = std::clamp((0.5f - v) * 2.0f, -1.0f, 1.0f); return; }
            const float lo = k == 0 ? 0.0f : m.t[(size_t) k - 1], hi = k == n - 1 ? 1.0f : m.t[(size_t) k + 1];
            m.t[(size_t) k] = k == 0 ? 0.0f : (k == n - 1 ? 1.0f : std::clamp(t, lo, hi)); m.v[(size_t) k] = v; });
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { drag = -1; repaint(); }
};
}

//==============================================================================
struct ModPage::Row : public juce::Component
{
    Row(CandyProcessor& p, int i) : proc(p), idx(i)
    {
        addAndMakeVisible(en); addAndMakeVisible(src); addAndMakeVisible(dst); addAndMakeVisible(depth); addAndMakeVisible(curve); addAndMakeVisible(slew);
        src.addItemList(sourceNames(), 1); dst.addItemList(destNames(), 1); curve.addItemList({ "Linear", "Exp", "Log", "S-curve" }, 1);
        depth.setSliderStyle(juce::Slider::LinearHorizontal); depth.setRange(-1.0, 1.0, 0.01); depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18); depth.setDoubleClickReturnValue(true, 0.0);
        slew.setSliderStyle(juce::Slider::LinearHorizontal); slew.setRange(0.0, 1000.0, 1.0); slew.setSkewFactorFromMidPoint(80.0); slew.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 18); slew.setTextValueSuffix(" ms"); slew.setColour(juce::Slider::trackColourId, col::purple);
        auto edit = [this](std::function<void(ModRoute&)> f) { if (updating) return; proc.editMain([this, f](CandyDNA& d) { f(d.routes[(size_t) idx]); }); };
        en.onClick = [this, edit] { const bool v = en.getToggleState(); edit([v](ModRoute& r) { r.enabled = v; }); };
        src.onChange = [this, edit] { const int v = src.getSelectedId() - 1; edit([v](ModRoute& r) { r.source = v; }); };
        dst.onChange = [this, edit] { const int v = dst.getSelectedId() - 1; edit([v](ModRoute& r) { r.dest = v; }); };
        curve.onChange = [this, edit] { const int v = curve.getSelectedId() - 1; edit([v](ModRoute& r) { r.curve = v; }); };
        depth.onValueChange = [this, edit] { const float v = (float) depth.getValue(); edit([v](ModRoute& r) { r.depth = v; }); };
        slew.onValueChange = [this, edit] { const float v = (float) slew.getValue(); edit([v](ModRoute& r) { r.slewMs = v; }); };
    }
    void pull(const ModRoute& r)
    {
        updating = true; en.setToggleState(r.enabled, juce::dontSendNotification); src.setSelectedId(std::clamp(r.source, 0, kNumModSources - 1) + 1, juce::dontSendNotification);
        dst.setSelectedId(std::clamp(r.dest, 0, kNumDestinations - 1) + 1, juce::dontSendNotification); curve.setSelectedId(std::clamp(r.curve, 0, 3) + 1, juce::dontSendNotification);
        if (! depth.isMouseButtonDown()) depth.setValue(r.depth, juce::dontSendNotification);
        if (! slew.isMouseButtonDown()) slew.setValue(r.slewMs, juce::dontSendNotification);
        updating = false; repaint();
    }
    void paint(juce::Graphics& g) override
    {
        g.setColour(en.getToggleState() ? col::magenta.withAlpha(0.10f) : (idx % 2 ? col::panel.withAlpha(0.5f) : juce::Colours::transparentBlack)); g.fillRect(getLocalBounds());
        g.setColour(col::dim); g.setFont(font(10.5f)); g.drawText(juce::String(idx + 1), 0, 0, 22, getHeight(), juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced(2, 3); r.removeFromLeft(22); en.setBounds(r.removeFromLeft(26)); src.setBounds(r.removeFromLeft(118)); r.removeFromLeft(4); dst.setBounds(r.removeFromLeft(118)); r.removeFromLeft(6);
        depth.setBounds(r.removeFromLeft(r.getWidth() * 45 / 100)); r.removeFromLeft(4); curve.setBounds(r.removeFromLeft(74)); r.removeFromLeft(6); slew.setBounds(r);
    }
    CandyProcessor& proc; int idx; bool updating = false;
    juce::ToggleButton en; juce::ComboBox src, dst, curve; juce::Slider depth, slew;
};

//==============================================================================
struct ModPage::Editor : public juce::Component
{
    Editor(CandyProcessor& p) : proc(p) { cur = proc.mainSound(); }
    CandyProcessor& proc; CandyDNA cur; int sel = 0;
    std::vector<std::unique_ptr<juce::Component>> items; std::vector<ModelKnob*> knobs; std::vector<ModelCombo*> combos; std::vector<ModelToggle*> toggles;
    BarEditor* bars = nullptr; MsegEditor* mseg = nullptr; bool barsShown = true;

    template <typename F> void change(F&& f) { proc.editMain([f](CandyDNA& d) { f(d.mods); }); }
    template <typename T, typename... A> T* add(A&&... a) { auto c = std::make_unique<T>(std::forward<A>(a)...); auto* p = c.get(); addAndMakeVisible(*p); items.push_back(std::move(c)); return p; }
    void knob(const juce::String& cap, double lo, double hi, double step, const juce::String& suf, std::function<double(const ModulatorBank&)> get, std::function<void(ModulatorBank&, double)> set, double mid = 0.0, juce::Colour c = col::turquoise)
    { knobs.push_back(add<ModelKnob>(cap, lo, hi, step, suf, [this, get] { return get(cur.mods); }, [this, set](double v) { change([set, v](ModulatorBank& m) { set(m, v); }); }, c, mid)); }
    void toggle(const juce::String& t, std::function<bool(const ModulatorBank&)> get, std::function<void(ModulatorBank&, bool)> set)
    { toggles.push_back(add<ModelToggle>(t, [this, get] { return get(cur.mods); }, [this, set](bool v) { change([set, v](ModulatorBank& m) { set(m, v); }); })); }
    void combo(const juce::String& cap, const juce::StringArray& it, std::function<int(const ModulatorBank&)> get, std::function<void(ModulatorBank&, int)> set)
    { combos.push_back(add<ModelCombo>(cap, it, [this, get] { return get(cur.mods); }, [this, set](int v) { change([set, v](ModulatorBank& m) { set(m, v); }); }, 1)); }

    void build(int s)
    {
        sel = s; items.clear(); knobs.clear(); combos.clear(); toggles.clear(); bars = nullptr; mseg = nullptr; removeAllChildren();
        const juce::StringArray divs { "1/16 (4)", "1/8 (2)", "1/4 (1)" };
        if (s < 4)   // LFO
        {
            const int i = s;
            combo("Shape", { "Sine", "Triangle", "Saw", "Square", "Sample & hold", "Smooth random", "Custom curve" }, [i](auto& m) { return m.lfo[(size_t) i].shape; }, [i](auto& m, int v) { m.lfo[(size_t) i].shape = v; });
            knob("Rate", 0.02, 40.0, 0.01, " Hz", [i](auto& m) { return (double) m.lfo[(size_t) i].rateHz; }, [i](auto& m, double v) { m.lfo[(size_t) i].rateHz = (float) v; }, 2.0);
            toggle("Tempo sync", [i](auto& m) { return m.lfo[(size_t) i].sync; }, [i](auto& m, bool v) { m.lfo[(size_t) i].sync = v; });
            knob("Sync (steps/beat)", 1, 16, 1, "", [i](auto& m) { return (double) m.lfo[(size_t) i].syncDiv; }, [i](auto& m, double v) { m.lfo[(size_t) i].syncDiv = (int) v; });
            knob("Phase", 0, 1, 0.01, "", [i](auto& m) { return (double) m.lfo[(size_t) i].phase; }, [i](auto& m, double v) { m.lfo[(size_t) i].phase = (float) v; }, 0.0, col::purple);
            bars = add<BarEditor>(); bars->count = [] { return kCurvePoints; }; bars->get = [this, i](int k) { return cur.mods.lfo[(size_t) i].curve[(size_t) k]; };
            bars->set = [this, i](int k, float v) { cur.mods.lfo[(size_t) i].curve[(size_t) k] = v; change([i, k, v](ModulatorBank& m) { m.lfo[(size_t) i].curve[(size_t) k] = v; }); };
        }
        else if (s < 8)  // MSEG
        {
            const int i = s - 4;
            mseg = add<MsegEditor>(); mseg->get = [this, i] { return cur.mods.mseg[(size_t) i]; };
            mseg->change = [this, i](const std::function<void(MsegCfg&)>& f) { f(cur.mods.mseg[(size_t) i]); change([i, f](ModulatorBank& m) { f(m.mseg[(size_t) i]); }); };
            knob("Points", 2, kMsegPoints, 1, "", [i](auto& m) { return (double) m.mseg[(size_t) i].numPoints; }, [i](auto& m, double v) { auto& g = m.mseg[(size_t) i]; const int old = g.numPoints; g.numPoints = (int) v;
                for (int k = old; k < g.numPoints; ++k) { g.t[(size_t) k] = std::min(1.0f, g.t[(size_t) k - 1] + 0.1f); g.v[(size_t) k] = g.v[(size_t) k - 1]; } g.t[(size_t) g.numPoints - 1] = 1.0f; });
            knob("Length", 5, 8000, 1, " ms", [i](auto& m) { return (double) m.mseg[(size_t) i].lengthMs; }, [i](auto& m, double v) { m.mseg[(size_t) i].lengthMs = (float) v; }, 500.0);
            toggle("Loop", [i](auto& m) { return m.mseg[(size_t) i].loop; }, [i](auto& m, bool v) { m.mseg[(size_t) i].loop = v; });
        }
        else if (s < 10)  // S&H
        {
            const int i = s - 8;
            knob("Rate", 0.1, 40.0, 0.01, " Hz", [i](auto& m) { return (double) m.sh[(size_t) i].rateHz; }, [i](auto& m, double v) { m.sh[(size_t) i].rateHz = (float) v; }, 4.0);
            toggle("Tempo sync", [i](auto& m) { return m.sh[(size_t) i].sync; }, [i](auto& m, bool v) { m.sh[(size_t) i].sync = v; });
            knob("Sync (steps/beat)", 1, 16, 1, "", [i](auto& m) { return (double) m.sh[(size_t) i].syncDiv; }, [i](auto& m, double v) { m.sh[(size_t) i].syncDiv = (int) v; });
            knob("Slew", 0, 500, 1, " ms", [i](auto& m) { return (double) m.sh[(size_t) i].slewMs; }, [i](auto& m, double v) { m.sh[(size_t) i].slewMs = (float) v; }, 40.0, col::purple);
        }
        else if (s < 12)  // chaos
        {
            const int i = s - 10;
            combo("Map", { "Logistic", "Henon-style" }, [i](auto& m) { return m.chaos[(size_t) i].type; }, [i](auto& m, int v) { m.chaos[(size_t) i].type = v; });
            knob("Rate", 0.1, 40.0, 0.01, " Hz", [i](auto& m) { return (double) m.chaos[(size_t) i].rateHz; }, [i](auto& m, double v) { m.chaos[(size_t) i].rateHz = (float) v; }, 4.0);
            knob("r (chaos)", 3.5, 3.99, 0.001, "", [i](auto& m) { return (double) m.chaos[(size_t) i].r; }, [i](auto& m, double v) { m.chaos[(size_t) i].r = (float) v; }, 0.0, col::magenta);
        }
        else if (s == 12)
        {
            knob("Rate", 0.1, 40.0, 0.01, " Hz", [](auto& m) { return (double) m.rw.rateHz; }, [](auto& m, double v) { m.rw.rateHz = (float) v; }, 6.0);
            knob("Step size", 0.0, 1.0, 0.01, "", [](auto& m) { return (double) m.rw.step; }, [](auto& m, double v) { m.rw.step = (float) v; }, 0.0, col::purple);
        }
        else if (s == 13)
        {
            bars = add<BarEditor>(); bars->count = [this] { return std::clamp(cur.mods.step.length, 1, kStepCount); }; bars->get = [this](int k) { return cur.mods.step.steps[(size_t) k]; };
            bars->set = [this](int k, float v) { cur.mods.step.steps[(size_t) k] = v; change([k, v](ModulatorBank& m) { m.step.steps[(size_t) k] = v; }); };
            knob("Length", 1, kStepCount, 1, "", [](auto& m) { return (double) m.step.length; }, [this](auto& m, double v) { m.step.length = (int) v; if (bars) bars->repaint(); });
            knob("Rate", 0.1, 40.0, 0.01, " Hz", [](auto& m) { return (double) m.step.rateHz; }, [](auto& m, double v) { m.step.rateHz = (float) v; }, 6.0);
            toggle("Tempo sync", [](auto& m) { return m.step.sync; }, [](auto& m, bool v) { m.step.sync = v; });
            knob("Sync (steps/beat)", 1, 16, 1, "", [](auto& m) { return (double) m.step.syncDiv; }, [](auto& m, double v) { m.step.syncDiv = (int) v; });
            knob("Slew", 0, 200, 1, " ms", [](auto& m) { return (double) m.step.slewMs; }, [](auto& m, double v) { m.step.slewMs = (float) v; }, 20.0, col::purple);
        }
        else
        {
            knob("Attack", 0.1, 200, 0.1, " ms", [](auto& m) { return (double) m.followerAttackMs; }, [](auto& m, double v) { m.followerAttackMs = (float) v; }, 10.0);
            knob("Release", 5, 1500, 1, " ms", [](auto& m) { return (double) m.followerReleaseMs; }, [](auto& m, double v) { m.followerReleaseMs = (float) v; }, 150.0, col::purple);
        }
        resized(); pull();
    }
    void pull()
    {
        for (auto* k : knobs) k->pull();
        for (auto* c : combos) c->pull();
        for (auto* t : toggles) t->pull();
        if (bars && sel < 4)
            bars->setVisible(cur.mods.lfo[(size_t) sel].shape == 6);
        const bool relayout = bars != nullptr && bars->isVisible() != barsShown;
        if (bars)
            barsShown = bars->isVisible();
        if (relayout)
            resized();
        if (bars)
            bars->repaint();
        if (mseg)
            mseg->repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto big = ((bars && bars->isVisible()) || mseg) ? r.removeFromBottom(std::max(110, r.getHeight() * 45 / 100)) : juce::Rectangle<int>();
        if (bars)
            bars->setBounds(big);
        if (mseg)
            mseg->setBounds(big);
        r.removeFromBottom(6);
        int x = r.getX(), y = r.getY(); const int kw = 84, kh = 96;
        for (auto* c : combos) { c->setBounds(x, y + 20, 120, 44); x += 128; }
        for (auto* t : toggles) { t->setBounds(x, y + 28, 100, 24); x += 106; }
        for (auto* k : knobs) { if (x + kw > r.getRight()) { x = r.getX(); y += kh + 4; } k->setBounds(x, y, kw, kh); x += kw + 2; }
    }
};

//==============================================================================
ModPage::ModPage(CandyProcessor& p) : PageBase(p)
{
    for (auto* c : { &pMatrix, &pMods, &pMacros }) addAndMakeVisible(c);
    rowsHolder = new juce::Component();
    for (int i = 0; i < kNumModRoutes; ++i) { auto* r = new Row(proc, i); rows.push_back(r); rowsHolder->addAndMakeVisible(r); }
    viewport.setViewedComponent(rowsHolder, true); viewport.setScrollBarsShown(true, false); pMatrix.addAndMakeVisible(viewport);
    pMatrix.addAndMakeVisible(clearAll); pMatrix.addAndMakeVisible(randomise);
    clearAll.onClick = [this] { proc.editMain([](CandyDNA& d) { for (auto& r : d.routes) r.enabled = false; }); };
    randomise.setTooltip("Fills the free routes with random, moderate-depth routes");
    randomise.onClick = [this]
    {
        std::mt19937 g { std::random_device {}() };
        proc.editMain([&g](CandyDNA& d)
        {
            for (auto& r : d.routes) if (! r.enabled)
            {
                std::uniform_real_distribution<float> u(0.0f, 1.0f); if (u(g) > 0.15f) continue;
                r.enabled = true; r.source = (int) (u(g) * 20.0f) % 20; r.dest = (int) (u(g) * 64.0f) % 64; r.depth = (u(g) - 0.5f) * 0.6f; r.curve = (int) (u(g) * 4.0f) % 4;
            }
        });
    };
    editor = std::make_unique<Editor>(proc); pMods.addAndMakeVisible(*editor);
    juce::StringArray names; for (int i = 0; i < 4; ++i) names.add("LFO " + juce::String(i + 1)); for (int i = 0; i < 4; ++i) names.add("MSEG " + juce::String(i + 1));
    names.add("S&H 1"); names.add("S&H 2"); names.add("Chaos 1"); names.add("Chaos 2"); names.add("Random walk"); names.add("Step modulator"); names.add("Envelope follower");
    modSel.addItemList(names, 1); modSel.setSelectedId(1, juce::dontSendNotification); modSel.onChange = [this] { editor->build(modSel.getSelectedId() - 1); };
    pMods.addAndMakeVisible(modSel); pMods.addAndMakeVisible(help);
    help.setText("Sources: 4 LFOs, 4 MSEGs, 2 S&H, 2 chaos, random walk, step, follower, transient, velocity, note, mod wheel, aftertouch, 4 macros.", juce::dontSendNotification); help.setColour(juce::Label::textColourId, col::dim); help.setFont(font(11.0f));
    for (int i = 0; i < kNumMacros; ++i)
    {
        const char* ids[] = { pid::macro1, pid::macro2, pid::macro3, pid::macro4 };
        macroKnobs.push_back(std::make_unique<ParamKnob>(proc.apvts, ids[i], "Macro " + juce::String(i + 1), i % 2 ? col::turquoise : col::magenta)); pMacros.addAndMakeVisible(*macroKnobs.back());
    }
    editor->build(0); refresh(kChSound);
}

ModPage::~ModPage() = default;

void ModPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    pMatrix.setBounds(r.removeFromLeft(r.getWidth() * 55 / 100)); r.removeFromLeft(8);
    pMacros.setBounds(r.removeFromBottom(130)); r.removeFromBottom(8); pMods.setBounds(r);
    { auto c = pMatrix.content(); auto b = c.removeFromBottom(26); clearAll.setBounds(b.removeFromLeft(100)); b.removeFromLeft(6); randomise.setBounds(b.removeFromLeft(150)); c.removeFromBottom(4);
      auto hdr = c.removeFromTop(0); viewport.setBounds(c); rowsHolder->setBounds(0, 0, c.getWidth() - 12, kNumModRoutes * 30); for (int i = 0; i < kNumModRoutes; ++i) rows[(size_t) i]->setBounds(0, i * 30, rowsHolder->getWidth(), 30); (void) hdr; }
    { auto c = pMods.content(); modSel.setBounds(c.removeFromTop(26).removeFromLeft(220)); auto h = c.removeFromBottom(30); help.setBounds(h); c.removeFromTop(4); editor->setBounds(c); }
    { auto c = pMacros.content(); const int w = c.getWidth() / 4; for (int i = 0; i < kNumMacros; ++i) macroKnobs[(size_t) i]->setBounds(c.getX() + i * w, c.getY(), w, c.getHeight()); }
}

void ModPage::refresh(uint32_t f)
{
    if (f & (kChSound | kChPreset | kChParamsLoaded))
    {
        editor->cur = proc.mainSound();
        for (int i = 0; i < kNumModRoutes; ++i) rows[(size_t) i]->pull(editor->cur.routes[(size_t) i]);
        editor->pull();
    }
}
void ModPage::tick() {}
}
