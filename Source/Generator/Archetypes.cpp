#include "Archetypes.h"
#include "DSP/Modulation/ModEngine.h"

namespace dali
{
Intent Intent::from(const GenSettings& g)
{
    Intent i; i.complexity = g.complexity; i.motion = g.motion; i.space = g.space; i.density = g.density; i.chaos = g.chaos;
    i.brightness = g.brightness; i.musicality = g.musicality; i.lengthMs = lengthMsFor(g, g.category); i.root = g.rootNote; i.scale = g.scale;
    i.category = g.category; i.sync = g.syncLength; return i;
}

namespace
{
using P = std::array<float, kParamsPerLayer>;
inline float J(Rng& r, float c, float sd) { return r.around(c, sd); }
inline float R(Rng& r, float lo, float hi) { return r.range(lo, hi); }
inline float expRange(Rng& r, float lo, float hi) { return lo * std::pow(hi / lo, r.uniform()); }

LayerConfig& layer(CandyDNA& d, int i, EngineType e, Rng& r)
{
    auto& l = d.layers[(size_t) i]; initLayer(l, e, r.next()); d.numLayers = std::max(d.numLayers, i + 1); return l;
}
void env(LayerConfig& l, float a, float dc, float s, float rel, float dur) { l.attackMs = a; l.decayMs = dc; l.sustain = s; l.releaseMs = rel; l.durationMs = dur; }
// Probability that an optional extra layer is added, driven by Complexity.
bool extra(Rng& r, const Intent& in, float base) { return r.chance(std::clamp(base * (0.4f + 1.2f * in.complexity), 0.0f, 0.95f)); }
float dur(Rng& r, const Intent& in, float lo, float hi, float lengthPull = 0.5f)
{   // archetype range, pulled toward the requested length
    const float v = expRange(r, lo, hi); return std::clamp(lerp(v, in.lengthMs * R(r, 0.7f, 1.3f), lengthPull), lo * 0.5f, hi * 2.0f);
}
float brightCut(Rng& r, const Intent& in, float lo, float hi) { return J(r, lerp(lo, hi, in.brightness), 0.05f); }

// ============================================================================ Micro Details
void a_droplet(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { 0.0f, 0.5f, R(r, .1f, .5f), R(r, .02f, .2f) * in.complexity, 0.0f, 0.0f, R(r, .62f, .82f), R(r, .18f, .38f),
            brightCut(r, in, .62f, .95f), .15f, 0.0f, 0.0f, .05f, .55f, 0.0f, 0.0f };
    l.pitch = pitchNote(r, in, 72, 92); env(l, .4f, R(r, 40, 110), 0.0f, 40, dur(r, in, 90, 200));
    if (extra(r, in, 0.65f))
    {
        auto& e = layer(d, 1, EngineType::MicroSynth, r); e = l; e.seed = r.next(); e.pitch = pitchNote(r, in, l.pitch + 5, l.pitch + 14); e.gainDb = -10.0f;
        e.startOffsetMs = R(r, 35, 95); e.decayMs *= .7f; e.durationMs *= .7f; e.pan = R(r, -.6f, .6f); l.pan = -e.pan * .5f;
    }
}
void a_laser(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { R(r, .55f, 1.0f), R(r, .2f, .8f), R(r, .2f, .7f), R(r, .25f, .7f), 0.0f, R(r, 0.f, .25f * in.chaos), R(r, .05f, .25f), R(r, .3f, .5f),
            brightCut(r, in, .55f, .9f), R(r, .35f, .7f), r.chance(.7f) ? 0.0f : .3f, R(r, 0.f, .12f), R(r, .25f, .5f), R(r, .5f, .7f), R(r, 0.f, .3f), 0.0f };
    l.pitch = pitchNote(r, in, 72, 96); env(l, .2f, R(r, 100, 220), 0.0f, 50, dur(r, in, 100, 260));
    if (extra(r, in, .4f)) { auto& n = layer(d, 1, EngineType::NoiseMotion, r); randomEngineParams(n, r, in); n.p[0] = .2f; n.p[1] = .8f; n.p[3] = .15f; n.p[6] = 0; n.gainDb = -16; env(n, 1, 80, 0, 60, 120); }
}
void a_blip(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { r.chance(.5f) ? .33f : 1.0f, R(r, .3f, .7f), R(r, .1f, .6f), R(r, .0f, .3f), R(r, 0.f, .2f), 0.0f, R(r, .48f, .56f), R(r, .1f, .3f),
            brightCut(r, in, .55f, .9f), R(r, .1f, .35f), 0.0f, 0.0f, R(r, .0f, .2f), .5f, R(r, 0.f, .3f), 0.0f };
    l.pitch = pitchNote(r, in, 64, 88); env(l, .3f, R(r, 40, 110), 0.0f, 30, dur(r, in, 40, 130));
    if (extra(r, in, .55f))
    {
        auto& e = layer(d, 1, EngineType::MicroSynth, r); e = l; e.seed = r.next(); e.pitch = pitchNote(r, in, l.pitch + R(r, 3, 12), l.pitch + 14); e.startOffsetMs = R(r, 60, 130); e.gainDb = -8.0f;
    }
}
void a_chirp(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { R(r, 0.f, .4f), .5f, R(r, .1f, .7f), R(r, .0f, .3f), 0.0f, 0.0f, R(r, .78f, .95f), R(r, .3f, .5f),
            brightCut(r, in, .6f, .92f), R(r, .3f, .6f), .3f, R(r, 0.f, .1f), R(r, 0.f, .2f), R(r, .55f, .8f), 0.0f, R(r, .3f, .7f) };
    l.pitch = pitchNote(r, in, 70, 90); env(l, .5f, R(r, 80, 180), 0.0f, 50, dur(r, in, 120, 300));
}
void a_click(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { 1.0f, R(r, .1f, .5f), .3f, 0.0f, 0.0f, 0.0f, .6f, .04f, brightCut(r, in, .5f, .9f), R(r, .3f, .6f), r.chance(.6f) ? R(r, .25f, .48f) : R(r, .5f, .74f),
            R(r, .85f, 1.0f), R(r, 0.f, .3f), .5f, 0.0f, 0.0f };
    l.pitch = pitchNote(r, in, 70, 96); env(l, .05f, R(r, 8, 20), 0.0f, 10, dur(r, in, 10, 35, .1f));
    if (extra(r, in, .6f))
    {
        auto& e = layer(d, 1, EngineType::Resonator, r); randomEngineParams(e, r, in);
        e.p[0] = R(r, .25f, .5f); e.p[3] = R(r, .05f, .2f); e.p[2] = R(r, .6f, .9f); e.p[6] = .05f; e.p[12] = .5f; e.pitch = pitchNote(r, in, 84, 100); e.gainDb = -9; env(e, .1f, 60, 0, 40, 60);
    }
}
void a_bleepPair(CandyDNA& d, Rng& r, const Intent& in)
{
    a_blip(d, r, in);
    auto& e = layer(d, 1, EngineType::MicroSynth, r); e = d.layers[0]; e.seed = r.next();
    e.pitch = pitchNote(r, in, d.layers[0].pitch + R(r, 3, 8), d.layers[0].pitch + 9); e.startOffsetMs = R(r, 70, 160); e.gainDb = -6; e.pan = R(r, -.5f, .5f); d.layers[0].pan = -e.pan;
}

// ============================================================================ Psychedelic Movement
void a_sweep(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    const bool up = r.chance(.6f);
    l.p = { R(r, .3f, .7f), J(r, lerp(.35f, .7f, in.brightness), .06f), R(r, .6f, .85f), up ? R(r, .8f, .96f) : R(r, .04f, .2f), R(r, .2f, .5f), R(r, 0.f, 1.f), R(r, 0.f, .25f),
            R(r, 0.f, .3f) * in.chaos, R(r, 0.f, .4f) * in.chaos, 0.0f, R(r, .2f, .5f), R(r, .2f, .5f), R(r, .2f, .5f), R(r, .1f, .4f), R(r, .1f, .4f), R(r, .3f, .6f) };
    l.pitch = pitchNote(r, in, 60, 80); const float t = dur(r, in, 500, 1600); env(l, R(r, 60, 200), 200, R(r, .4f, .7f), R(r, 300, 700), t);
    if (extra(r, in, .5f)) { auto& e = layer(d, 1, EngineType::MicroSynth, r); randomEngineParams(e, r, in); e.p[0] = 0; e.p[6] = up ? .8f : .2f; e.p[7] = .6f; e.p[3] = .1f; e.pitch = pitchNote(r, in, 72, 90); e.gainDb = -12; env(e, 30, 300, 0.2f, 200, t * .6f); }
}
void a_swirl(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    l.p = { R(r, .5f, .9f), J(r, lerp(.4f, .75f, in.brightness), .08f), R(r, .3f, .55f), R(r, .35f, .65f), R(r, .2f, .45f), R(r, 0.f, 1.f), R(r, .1f, .4f), R(r, .1f, .4f), R(r, 0.f, .3f), R(r, .2f, .5f),
            R(r, .2f, .45f), R(r, .7f, 1.f), R(r, .2f, .5f), R(r, .5f, .8f), 0.0f, R(r, 0.f, .3f) };
    l.pitch = pitchNote(r, in, 60, 76); env(l, R(r, 80, 200), 300, R(r, .4f, .6f), R(r, 400, 800), dur(r, in, 700, 1800)); l.sendMod = R(r, .5f, .8f);
}
void a_spiral(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { R(r, 0.f, .4f), .5f, R(r, .25f, .6f), R(r, .35f, .75f), R(r, 0.f, .3f), 0.0f, R(r, .5f, .62f), R(r, .3f, .5f), brightCut(r, in, .5f, .85f), R(r, .2f, .5f), 0.0f, 0.0f, R(r, .1f, .3f),
            R(r, .5f, .7f), R(r, .1f, .4f), R(r, .6f, 1.f) };
    l.pitch = pitchNote(r, in, 60, 84); env(l, 5, 200, R(r, .2f, .5f), 250, dur(r, in, 400, 1000));
    if (extra(r, in, .7f))
    {
        auto& g = layer(d, 1, EngineType::Granular, r); randomEngineParams(g, r, in); g.p[0] = R(r, .0f, .12f); g.p[2] = R(r, .35f, .55f); g.p[3] = R(r, .4f, .65f); g.p[14] = in.brightness;
        g.pitch = l.pitch; g.gainDb = -10; env(g, 80, 300, .5f, 400, l.durationMs);
    }
}
void a_bubble(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { 0.0f, .5f, R(r, .1f, .4f), R(r, 0.f, .2f), R(r, .3f, .6f), 0.0f, R(r, .66f, .8f), R(r, .3f, .45f), brightCut(r, in, .45f, .75f), R(r, .3f, .6f), 0.0f, 0.0f, R(r, .05f, .2f), R(r, .7f, .9f), 0.0f, R(r, 0.f, .3f) };
    l.pitch = pitchNote(r, in, 60, 80); env(l, .5f, R(r, 120, 300), 0.0f, 120, dur(r, in, 200, 500));
    if (extra(r, in, .6f))
    {
        auto& s = layer(d, 1, EngineType::Spectral, r); randomEngineParams(s, r, in); s.p[0] = .9f; s.p[2] = R(r, .6f, .85f); s.p[1] = .4f; s.p[9] = .1f; s.pitch = l.pitch; s.gainDb = -12; env(s, 40, 300, .3f, 500, l.durationMs * 1.5f);
    }
}

// ============================================================================ Organic Textures
void a_breath(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    l.p = { R(r, .6f, 1.f), J(r, lerp(.3f, .6f, in.brightness), .06f), R(r, .35f, .6f), R(r, .35f, .65f), R(r, .1f, .3f), R(r, 0.f, 1.f), R(r, .65f, 1.f), R(r, .2f, .5f), R(r, .1f, .5f) * (0.5f + in.chaos),
            R(r, .5f, .9f), R(r, .15f, .35f), R(r, .3f, .6f), R(r, .1f, .3f), R(r, .5f, .8f), R(r, 0.f, .2f), R(r, 0.f, .2f) };
    l.pitch = pitchNote(r, in, 52, 70); env(l, R(r, 150, 300), 200, R(r, .5f, .7f), R(r, 400, 900), dur(r, in, 800, 2200));
}
void a_insect(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { 0.0f, .5f, R(r, .75f, 1.f), R(r, .3f, .7f), R(r, .5f, .9f), R(r, 0.f, .3f), R(r, .55f, .68f), R(r, .1f, .25f), brightCut(r, in, .7f, .95f), R(r, .2f, .5f), .3f, R(r, 0.f, .15f), R(r, 0.f, .2f), .5f, R(r, .1f, .4f), R(r, .3f, .8f) };
    l.pitch = pitchNote(r, in, 86, 100); env(l, .5f, R(r, 40, 120), R(r, 0.f, .2f), 40, dur(r, in, 80, 250));
    if (extra(r, in, .6f)) { auto& n = layer(d, 1, EngineType::NoiseMotion, r); randomEngineParams(n, r, in); n.p[1] = .85f; n.p[2] = .6f; n.p[14] = R(r, .5f, 1.f); n.p[9] = 0; n.p[6] = 0; n.gainDb = -14; env(n, 2, 80, 0, 50, l.durationMs); }
}
void a_clickCluster(CandyDNA& d, Rng& r, const Intent& in)
{
    if (r.chance(.5f))
    {
        auto& l = layer(d, 0, EngineType::Granular, r);
        l.p = { R(r, .27f, .34f), R(r, .3f, .8f), R(r, .08f, .25f), R(r, .55f, .85f), .5f, R(r, .3f, .6f), R(r, 0.f, 1.f), R(r, .2f, .5f), R(r, .1f, .4f), R(r, .7f, 1.f), R(r, .1f, .5f), 0.0f, .5f, R(r, .6f, .9f), in.brightness, R(r, .2f, .5f) };
        l.pitch = pitchNote(r, in, 60, 84); env(l, 1, 100, R(r, .3f, .6f), 80, dur(r, in, 150, 500));
    }
    else
    {
        auto& l = layer(d, 0, EngineType::Glitch, r);
        l.p = { R(r, .1f, .25f), R(r, 0.f, .3f), R(r, .2f, .5f), R(r, .6f, 1.f), R(r, .1f, .4f), R(r, 0.f, .3f), 0.0f, .5f, R(r, .7f, 1.f), 0.0f, R(r, .27f, .34f), R(r, .3f, .8f), in.brightness, R(r, .2f, .5f), R(r, 0.f, .3f), R(r, .1f, .5f) };
        l.pitch = pitchNote(r, in, 60, 84); env(l, 1, 100, R(r, .3f, .6f), 80, dur(r, in, 150, 500));
    }
}
void a_throat(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    l.p = { R(r, .5f, 1.f), J(r, lerp(.25f, .5f, in.brightness), .05f), R(r, .4f, .65f), R(r, .4f, .6f), R(r, .1f, .3f), R(r, 0.f, 1.f), R(r, .8f, 1.f), R(r, .2f, .5f), R(r, .5f, .9f), R(r, .5f, .8f), R(r, .6f, .85f), R(r, .2f, .5f), R(r, .1f, .3f), R(r, .5f, .9f), 0.0f, R(r, 0.f, .2f) };
    l.pitch = pitchNote(r, in, 50, 66); env(l, 20, 200, R(r, .5f, .8f), 300, dur(r, in, 400, 1200));
    if (extra(r, in, .5f)) { auto& m = layer(d, 1, EngineType::MicroSynth, r); randomEngineParams(m, r, in); m.p[0] = .33f; m.p[4] = .7f; m.p[8] = .45f; m.pitch = pitchNote(r, in, 55, 67); m.gainDb = -14; env(m, 10, 200, .5f, 200, l.durationMs * .8f); }
}

// ============================================================================ Metallic Accents
void a_metalTick(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Resonator, r);
    l.p = { R(r, .3f, .45f), R(r, .3f, .7f), R(r, .5f, .8f), R(r, .15f, .35f), R(r, 0.f, .3f), R(r, .6f, .9f), R(r, .1f, .3f), R(r, .2f, .6f), R(r, 0.f, .25f * in.chaos), .5f, 0.0f, R(r, 0.f, .25f), R(r, .4f, .8f), R(r, .3f, .7f), R(r, .2f, .5f), 0.0f };
    l.pitch = pitchNote(r, in, 72, 96); env(l, .1f, 220, 0.0f, 80, dur(r, in, 100, 260, .3f));
}
void a_bell(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Resonator, r);
    l.p = { R(r, .6f, .78f), R(r, .1f, .4f), R(r, .6f, .9f), R(r, .45f, .7f), R(r, 0.f, .2f), R(r, .5f, .8f), R(r, .1f, .25f), .5f, 0.0f, .5f, 0.0f, 0.0f, R(r, .6f, 1.f), R(r, .3f, .8f), R(r, .2f, .5f), 0.0f };
    l.pitch = pitchNote(r, in, 72, 96); env(l, .1f, 900, 0.0f, 200, dur(r, in, 500, 1500, .4f));
}
void a_spring(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Resonator, r);
    l.p = { R(r, 0.f, .2f), R(r, .2f, .5f), R(r, .4f, .7f), R(r, .35f, .5f), R(r, .2f, .5f), R(r, .4f, .8f), R(r, .1f, .3f), .5f, 0.0f, R(r, .35f, .65f), R(r, .2f, .4f), R(r, .1f, .4f), R(r, .5f, .9f), R(r, .3f, .6f), R(r, .2f, .5f), R(r, .6f, 1.f) };
    l.pitch = pitchNote(r, in, 58, 82); env(l, .2f, 400, 0.0f, 150, dur(r, in, 250, 700, .4f));
}
void a_alienPerc(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Resonator, r);
    l.p = { R(r, .85f, 1.f), R(r, .3f, .8f), R(r, .4f, .75f), R(r, .2f, .35f), R(r, 0.f, .3f), R(r, .3f, .7f), R(r, .2f, .45f), R(r, .3f, .7f), R(r, .4f, .7f), .5f, 0.0f, R(r, 0.f, .2f), R(r, .4f, .8f), R(r, .4f, .8f), R(r, .2f, .6f), R(r, 0.f, .3f) };
    l.pitch = pitchNote(r, in, 52, 76); env(l, .2f, 250, 0.0f, 100, dur(r, in, 120, 400, .3f));
    if (extra(r, in, .7f)) { auto& m = layer(d, 1, EngineType::MicroSynth, r); randomEngineParams(m, r, in); m.p[11] = .9f; m.p[7] = .05f; m.p[8] = .7f; m.pitch = 84; m.gainDb = -10; env(m, .05f, 15, 0, 10, 25); }
}

// ============================================================================ Rhythmic Glitches
void glitchBase(LayerConfig& l, Rng& r, const Intent& in)
{
    l.p = { R(r, .1f, .35f), R(r, 0.f, .4f), R(r, .2f, .6f), R(r, .4f, .9f), R(r, .05f, .4f), R(r, 0.f, .3f), r.chance(.6f) ? R(r, .3f, .8f) : 0.0f, R(r, .35f, .7f), R(r, .6f, 1.f), 0.0f,
            R(r, 0.f, .45f), R(r, .3f, .8f), in.brightness, R(r, .2f, .6f), R(r, 0.f, .3f), R(r, .1f, .5f) };
    l.pitch = pitchNote(r, in, 60, 84);
}
void a_stutter(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Glitch, r); glitchBase(l, r, in); l.p[3] = R(r, .5f, .9f); l.p[2] = R(r, .4f, .7f); l.p[1] = R(r, 0.f, .25f);
    env(l, .5f, 100, R(r, .4f, .7f), 60, dur(r, in, 200, 600));
}
void a_reverseSlice(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Glitch, r); glitchBase(l, r, in); l.p[1] = R(r, .8f, 1.f); l.p[0] = R(r, .25f, .5f); l.p[3] = R(r, .2f, .5f); l.p[8] = R(r, .8f, 1.f);
    env(l, 2, 150, R(r, .4f, .7f), 80, dur(r, in, 200, 700));
}
void a_bitShred(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Glitch, r); glitchBase(l, r, in); l.p[4] = R(r, .7f, .95f); l.p[5] = R(r, .45f, .8f); l.p[6] = R(r, .5f, .9f); l.p[15] = R(r, .8f, 1.f);
    l.insert = InsertFx::Saturation; l.insertA = R(r, .2f, .5f); l.insertB = R(r, .5f, .9f);
    env(l, .5f, 120, R(r, .4f, .7f), 60, dur(r, in, 150, 450));
}
void a_microLoop(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Glitch, r); glitchBase(l, r, in); l.p[9] = R(r, .4f, .8f); l.p[10] = R(r, .27f, .34f); l.p[0] = R(r, .1f, .25f); l.p[3] = R(r, .5f, .8f); l.p[6] = R(r, .3f, .5f);
    env(l, 1, 120, R(r, .4f, .7f), 60, dur(r, in, 150, 500));
}

// ============================================================================ Atmospheric Candy
void a_wash(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Spectral, r);
    l.p = { R(r, .8f, 1.f), R(r, .3f, .7f), R(r, .6f, .9f), R(r, .4f, .7f), R(r, .4f, .8f), R(r, .2f, .5f), R(r, .2f, .5f), R(r, .2f, .4f), .5f, R(r, .35f, .65f), .5f, J(r, in.brightness, .08f), R(r, .4f, .6f), R(r, .1f, .4f), R(r, .2f, .4f), R(r, .6f, .9f) };
    l.pitch = pitchNote(r, in, 55, 76); env(l, R(r, 200, 500), 400, R(r, .5f, .7f), R(r, 800, 1500), dur(r, in, 1200, 3200));
    if (extra(r, in, .5f)) { auto& n = layer(d, 1, EngineType::NoiseMotion, r); randomEngineParams(n, r, in); n.p[0] = .8f; n.p[1] = lerp(.4f, .7f, in.brightness); n.p[9] = .4f; n.p[10] = .2f; n.gainDb = -16; env(n, 300, 400, .5f, 900, l.durationMs); }
}
void a_cloud(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Granular, r);
    l.p = { r.chance(.7f) ? R(r, .0f, .12f) : R(r, .15f, .27f), R(r, .35f, .65f), R(r, .4f, .65f), R(r, .45f, .7f), J(r, .5f, .03f), R(r, .2f, .5f), R(r, 0.f, 1.f), R(r, .15f, .3f), R(r, .1f, .3f), R(r, .7f, 1.f), R(r, .1f, .4f), 0.0f, R(r, .55f, .7f), R(r, .3f, .6f), in.brightness, R(r, .2f, .5f) };
    l.pitch = pitchNote(r, in, 50, 74); env(l, R(r, 200, 500), 400, R(r, .5f, .7f), R(r, 800, 1600), dur(r, in, 1200, 3000));
}
void a_shimmer(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Spectral, r);
    l.p = { R(r, .85f, 1.f), R(r, .8f, 1.f), R(r, .3f, .6f), R(r, .6f, .85f), R(r, .5f, .9f), R(r, .1f, .3f), R(r, .2f, .4f), R(r, .1f, .3f), R(r, .88f, 1.f), R(r, .05f, .25f), .5f, R(r, .6f, .95f), R(r, .5f, .7f), R(r, .05f, .25f), R(r, .3f, .6f), R(r, .6f, .9f) };
    l.pitch = pitchNote(r, in, 60, 76); env(l, R(r, 250, 600), 500, R(r, .5f, .75f), R(r, 900, 1800), dur(r, in, 1500, 3500));
    if (extra(r, in, .6f)) { auto& g = layer(d, 1, EngineType::Granular, r); randomEngineParams(g, r, in); g.p[0] = .05f; g.p[4] = .75f; g.p[3] = .5f; g.p[2] = .5f; g.pitch = l.pitch; g.gainDb = -12; env(g, 300, 400, .6f, 1000, l.durationMs); }
}
void a_darkBreath(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    l.p = { R(r, .8f, 1.f), R(r, .12f, .3f) + .15f * in.brightness, R(r, .3f, .5f), R(r, .4f, .6f), R(r, .1f, .25f), R(r, 0.f, 1.f), R(r, .4f, .7f), R(r, .2f, .4f), R(r, .1f, .3f), R(r, .6f, .85f), R(r, .1f, .2f), R(r, .3f, .6f), R(r, .1f, .2f), R(r, .4f, .7f), 0.0f, R(r, 0.f, .15f) };
    l.pitch = pitchNote(r, in, 48, 64); env(l, R(r, 300, 700), 400, R(r, .5f, .7f), R(r, 900, 1800), dur(r, in, 1500, 3000));
}

// ============================================================================ Transitions
void a_riser(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r); const float t = dur(r, in, 1000, 3000, .6f);
    l.p = { R(r, .4f, .8f), R(r, .2f, .4f), R(r, .55f, .75f), R(r, .9f, 1.f), R(r, .08f, .2f), R(r, 0.f, 1.f), R(r, 0.f, .25f), R(r, 0.f, .2f), R(r, 0.f, .3f) * in.chaos, 0.0f, .3f, R(r, .3f, .6f), R(r, .2f, .4f), R(r, .1f, .3f), R(r, .3f, .6f), R(r, .3f, .5f) };
    l.pitch = pitchNote(r, in, 60, 76); env(l, t * .85f, 30, 0.0f, 80, t);
    if (extra(r, in, .7f)) { auto& s = layer(d, 1, EngineType::Spectral, r); randomEngineParams(s, r, in); s.p[0] = .9f; s.p[9] = .7f; s.p[8] = .6f; s.p[3] = .75f; s.p[4] = .5f; s.pitch = l.pitch; s.gainDb = -10; env(s, t * .85f, 30, 0.0f, 80, t); }
}
void a_downlifter(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r); const float t = dur(r, in, 800, 2500, .6f);
    l.p = { R(r, .4f, .8f), R(r, .7f, .9f), R(r, .45f, .7f), R(r, .04f, .15f), R(r, .08f, .2f), R(r, 0.f, 1.f), R(r, 0.f, .2f), R(r, 0.f, .2f), R(r, 0.f, .3f) * in.chaos, 0.0f, .3f, R(r, .3f, .6f), R(r, .2f, .4f), R(r, .1f, .3f), R(r, .1f, .3f), R(r, .2f, .5f) };
    l.pitch = pitchNote(r, in, 60, 76); env(l, 8, t * .8f, 0.0f, 150, t);
    if (extra(r, in, .6f)) { auto& m = layer(d, 1, EngineType::MicroSynth, r); randomEngineParams(m, r, in); m.p[0] = 0; m.p[6] = .12f; m.p[7] = .85f; m.p[3] = .1f; m.pitch = pitchNote(r, in, 76, 92); m.gainDb = -12; env(m, 1, t * .6f, 0, 100, t * .7f); }
}
void a_glitchRiser(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Glitch, r); glitchBase(l, r, in); l.p[6] = R(r, .15f, .3f); l.p[3] = R(r, .6f, .9f); l.p[2] = R(r, .3f, .5f);
    const float t = dur(r, in, 800, 2200, .6f); env(l, t * .6f, 50, .3f, 80, t);
    d.mods.mseg[0].numPoints = 2; d.mods.mseg[0].t = { 0, 1 }; d.mods.mseg[0].v = { 0, 1 }; d.mods.mseg[0].lengthMs = t;
    d.routes[0] = { kSrcMseg1, layerParamDest(0, 6), .55f, 0, 0, false, 4, true }; d.routes[1] = { kSrcMseg1, layerParamDest(0, 2), .35f, 0, 0, false, 4, true };
    auto& n = layer(d, 1, EngineType::NoiseMotion, r); randomEngineParams(n, r, in); n.p[3] = .92f; n.p[0] = .6f; n.p[1] = .3f; n.p[9] = 0; n.gainDb = -12; env(n, t * .8f, 30, 0, 80, t);
}
void a_impactTail(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Spectral, r); const float t = dur(r, in, 1500, 3500, .6f);
    l.p = { 1.0f, R(r, .6f, .9f), R(r, .6f, .85f), R(r, .3f, .6f), R(r, .5f, .9f), R(r, .1f, .3f), R(r, .1f, .3f), R(r, .1f, .3f), R(r, .25f, .45f), R(r, .3f, .6f), .5f, R(r, .3f, .6f), R(r, .3f, .5f), R(r, 0.f, .2f), R(r, .2f, .4f), R(r, .7f, 1.f) };
    l.pitch = pitchNote(r, in, 52, 70); env(l, 2, t * .6f, 0.0f, 300, t);
    auto& b = layer(d, 1, EngineType::Resonator, r); randomEngineParams(b, r, in); b.p[0] = R(r, .55f, .75f); b.p[3] = R(r, .55f, .75f); b.pitch = pitchNote(r, in, 55, 67); b.gainDb = -8; env(b, .1f, t * .6f, 0, 200, t * .4f);
}

// ============================================================================ Experimental
void a_ringMachine(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::MicroSynth, r);
    l.p = { R(r, 0.f, 1.f), R(r, .2f, .8f), R(r, 0.f, 1.f), R(r, .3f, .7f), R(r, .2f, .6f), R(r, .7f, 1.f), R(r, .3f, .7f), R(r, .2f, .5f), brightCut(r, in, .5f, .9f), R(r, .2f, .5f), 0.0f, R(r, 0.f, .2f), R(r, .1f, .4f), R(r, .4f, .6f), R(r, 0.f, .4f), R(r, 0.f, .6f) };
    l.pitch = pitchNote(r, in, 60, 88); env(l, .5f, R(r, 60, 200), R(r, 0.f, .3f), 80, dur(r, in, 120, 500));
    auto& e = layer(d, 1, EngineType::Resonator, r); randomEngineParams(e, r, in); e.p[8] = R(r, .6f, .9f); e.p[7] = R(r, .2f, .8f); e.pitch = l.pitch + R(r, -12, 12); e.gainDb = -9; env(e, .2f, 200, 0, 100, l.durationMs);
}
void a_binScrambler(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::Spectral, r);
    l.p = { R(r, .8f, 1.f), R(r, .1f, .5f), R(r, .2f, .6f), R(r, .3f, .8f), R(r, .3f, .8f), R(r, .6f, .9f), R(r, .5f, .9f), R(r, .7f, 1.f), R(r, .35f, .65f), R(r, .4f, .8f), .5f, in.brightness, R(r, .2f, .8f), R(r, 0.f, .3f), R(r, .5f, .8f), R(r, .6f, .9f) };
    l.pitch = pitchNote(r, in, 52, 80); env(l, R(r, 20, 150), 300, R(r, .4f, .7f), R(r, 300, 800), dur(r, in, 600, 1600));
}
void a_hybrid(CandyDNA& d, Rng& r, const Intent& in)
{
    const int n = std::clamp(2 + (int) std::round(in.complexity * 2.0f * r.uniform() + 0.5f), 2, 4);
    EngineType used[4];
    for (int i = 0; i < n; ++i)
    {
        EngineType e; do { e = (EngineType) r.rangeInt(0, kNumEngines - 1); } while (i > 0 && e == used[i - 1]);
        used[i] = e; auto& l = layer(d, i, e, r); randomEngineParams(l, r, in); l.gainDb = -6.0f - 3.0f * (float) i; l.startOffsetMs = (float) i * R(r, 0, 90);
        l.pan = R(r, -.6f, .6f);
    }
}
void a_formantFold(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& l = layer(d, 0, EngineType::NoiseMotion, r);
    l.p = { R(r, .4f, .9f), R(r, .3f, .6f), R(r, .4f, .7f), R(r, .3f, .7f), R(r, .2f, .5f), R(r, 0.f, 1.f), R(r, .8f, 1.f), R(r, .2f, .5f), R(r, .2f, .6f), R(r, .3f, .7f), R(r, .2f, .5f), R(r, .3f, .6f), R(r, .2f, .5f), R(r, .4f, .8f), R(r, 0.f, .3f), R(r, .1f, .4f) };
    l.pitch = pitchNote(r, in, 52, 72); l.insert = InsertFx::Wavefolder; l.insertA = R(r, .5f, .9f); l.insertB = R(r, .3f, .7f);
    env(l, 30, 250, R(r, .4f, .7f), 400, dur(r, in, 500, 1500));
}
} // namespace

// ============================================================================ shared helpers
void initLayer(LayerConfig& l, EngineType e, uint32_t seed)
{
    l = LayerConfig(); l.active = true; l.engine = e; l.seed = seed ? seed : 1u; l.p.fill(0.5f);
    l.gainDb = -6.0f; l.pitch = 60.0f; l.attackMs = 1.0f; l.decayMs = 150.0f; l.sustain = 0.0f; l.releaseMs = 80.0f; l.durationMs = 300.0f;
}

float pitchNote(Rng& r, const Intent& in, float lo, float hi)
{
    const float shift = (in.brightness - 0.5f) * 10.0f;
    float n = R(r, lo + shift * 0.5f, hi + shift);
    const float strength = std::pow(in.musicality, 0.8f);
    n = quantizeToScale(n, in.root, in.scale, strength);
    if (in.musicality < 0.35f) n += r.bipolar() * (0.35f - in.musicality) * 3.0f;      // atonal drift
    return std::clamp(n, 24.0f, 108.0f);
}

void randomEngineParams(LayerConfig& l, Rng& r, const Intent& in)
{
    switch (l.engine)
    {
        case EngineType::MicroSynth:
            l.p = { R(r, 0, 1), R(r, .1f, .9f), R(r, 0, 1), R(r, 0, .6f) * (.4f + in.chaos), R(r, 0, .5f), R(r, 0, .4f) * in.chaos, R(r, .3f, .8f), R(r, .1f, .6f), brightCut(r, in, .4f, .9f), R(r, .1f, .6f),
                    R(r, 0, 1), R(r, 0, .3f), R(r, 0, .5f), R(r, .3f, .7f), R(r, 0, .4f), R(r, 0, .5f) };
            l.pitch = pitchNote(r, in, 60, 90); env(l, .5f, R(r, 40, 200), R(r, 0, .3f), 80, R(r, 80, 400)); break;
        case EngineType::Resonator:
            l.p = { R(r, 0, 1), R(r, .1f, .8f), R(r, .3f, .9f), R(r, .1f, .7f), R(r, 0, .4f), R(r, .3f, .9f), R(r, .1f, .5f), R(r, .2f, .8f), R(r, 0, .5f) * in.chaos, .5f, 0, R(r, 0, .3f), R(r, .3f, 1), R(r, .2f, .8f), R(r, .1f, .6f), R(r, 0, .8f) };
            l.pitch = pitchNote(r, in, 60, 92); env(l, .2f, R(r, 150, 700), 0, 120, R(r, 100, 500)); break;
        case EngineType::Granular:
            l.p = { R(r, 0, .5f), R(r, .3f, .7f), R(r, .1f, .7f), R(r, .3f, .8f), J(r, .5f, .08f), R(r, 0, .6f), R(r, 0, 1), R(r, .1f, .4f), R(r, 0, .5f), R(r, .3f, 1), R(r, 0, .6f), 0, R(r, .3f, .7f), R(r, .2f, .7f), in.brightness, R(r, 0, .5f) };
            l.pitch = pitchNote(r, in, 55, 80); env(l, R(r, 5, 200), 300, R(r, .3f, .7f), R(r, 200, 800), R(r, 300, 1500)); break;
        case EngineType::Glitch:
            { LayerConfig t = l; (void) t; l.p = { R(r, .1f, .5f), R(r, 0, .6f), R(r, .1f, .7f), R(r, .2f, .9f), R(r, 0, .6f), R(r, 0, .5f), r.chance(.5f) ? R(r, .2f, .9f) : 0.0f, R(r, .3f, .7f), R(r, .5f, 1), R(r, 0, .8f) * (r.chance(.4f) ? 1.0f : 0.0f),
                  R(r, 0, .45f), R(r, .3f, .8f), in.brightness, R(r, .2f, .6f), R(r, 0, .3f), R(r, .1f, .7f) };
              l.pitch = pitchNote(r, in, 60, 84); env(l, 1, 120, R(r, .3f, .7f), 80, R(r, 150, 700)); } break;
        case EngineType::Spectral:
            l.p = { R(r, .6f, 1), R(r, 0, .8f), R(r, 0, .8f), R(r, .2f, .8f), R(r, .3f, 1), R(r, 0, .8f) * in.chaos, R(r, .1f, .8f), R(r, 0, .8f) * (.3f + in.chaos), R(r, .3f, .7f), R(r, 0, .8f), .5f, in.brightness, R(r, .2f, .8f), R(r, 0, .4f), R(r, 0, .8f), R(r, .5f, .9f) };
            l.pitch = pitchNote(r, in, 52, 80); env(l, R(r, 10, 400), 300, R(r, .3f, .7f), R(r, 200, 900), R(r, 400, 1500)); break;
        case EngineType::NoiseMotion:
        default:
            l.p = { R(r, 0, 1), J(r, lerp(.25f, .75f, in.brightness), .1f), R(r, .2f, .8f), R(r, 0, 1), R(r, .1f, .6f), R(r, 0, 1), R(r, 0, 1), R(r, 0, .6f), R(r, 0, .7f) * in.chaos, R(r, 0, .9f), R(r, .1f, .7f), R(r, 0, .8f), R(r, .1f, .6f), R(r, 0, .8f), R(r, 0, .6f), R(r, 0, .6f) };
            l.pitch = pitchNote(r, in, 50, 80); env(l, R(r, 5, 300), 300, R(r, .3f, .7f), R(r, 200, 800), R(r, 300, 1500)); break;
    }
}

juce::String makeName(Rng& r, const char* archetype)
{
    static const char* adj[] = { "Glass", "Neon", "Lunar", "Feral", "Velvet", "Acid", "Solar", "Opal", "Spiral", "Plasma", "Mossy", "Liquid", "Cosmic", "Gummy", "Crystal",
                                 "Wobbly", "Ancient", "Electric", "Tidal", "Hollow", "Fractal", "Dusty", "Neural", "Jungle" };
    return juce::String(adj[r.rangeInt(0, 23)]) + " " + archetype;
}

// --------------------------------------------------------------------------------------------
// Modulation: number and character of routes follow Complexity and Motion; depths follow Motion.
// --------------------------------------------------------------------------------------------
void applyModulation(CandyDNA& d, Rng& r, const Intent& in)
{
    struct Opt { int param; float w; };
    static const std::vector<Opt> dests[kNumEngines] = {
        { { 8, .30f }, { 3, .20f }, { 12, .08f }, { 6, .08f }, { 7, .05f }, { 5, .05f }, { 1, .08f }, { 0, .06f }, { 14, .05f }, { 15, .05f } },
        { { 0, .20f }, { 3, .20f }, { 14, .15f }, { 2, .15f }, { 15, .10f }, { 8, .10f }, { 4, .10f } },
        { { 6, .25f }, { 4, .10f }, { 3, .15f }, { 2, .15f }, { 9, .10f }, { 5, .10f }, { 14, .10f }, { 8, .05f } },
        { { 0, .15f }, { 1, .10f }, { 2, .15f }, { 4, .15f }, { 5, .10f }, { 7, .10f }, { 9, .10f }, { 13, .05f }, { 14, .10f } },
        { { 3, .25f }, { 4, .10f }, { 2, .15f }, { 1, .10f }, { 5, .10f }, { 7, .10f }, { 8, .10f }, { 12, .10f } },
        { { 1, .30f }, { 5, .15f }, { 2, .10f }, { 3, .10f }, { 7, .05f }, { 11, .10f }, { 13, .05f }, { 9, .10f }, { 15, .05f } } };

    // keep routes already placed by the archetype (e.g. riser ramps)
    int used = 0; for (auto& rt : d.routes) if (rt.enabled) ++used;
    const int budget = std::clamp((int) std::round(0.5f + in.complexity * 3.0f + in.motion * 5.0f + r.bipolar() * 1.0f), 0, 14);
    int nLfo = 0, nMseg = 0, nSh = 0, nCh = 0, nStep = 0, nRw = 0;
    for (auto& rt : d.routes) if (rt.enabled) { if (rt.source >= kSrcMseg1 && rt.source <= kSrcMseg4) nMseg = std::max(nMseg, rt.source - kSrcMseg1 + 1); }
    const float durMs = std::max(60.0f, d.durationMs);
    int slot = used;

    for (int k = 0; k < budget && slot < kNumModRoutes; ++k)
    {
        // pick a target layer / destination
        const int li = r.rangeInt(0, std::max(0, d.numLayers - 1)); auto& L = d.layers[(size_t) li]; if (! L.active) continue;
        ModRoute rt; rt.enabled = true;
        const float tCtl = r.uniform();
        if (tCtl < 0.14f)
        {   const int ctl = (int) r.pickWeighted(std::vector<float> { 0.f, .3f, 0.f, .25f, .25f, .1f, .0f, .0f }); rt.dest = layerCtlDest(li, ctl); }
        else if (tCtl < 0.18f && L.insert != InsertFx::None) rt.dest = layerCtlDest(li, r.chance(.5f) ? kLdInsertA : kLdInsertB);
        else
        {
            const auto& opts = dests[(int) L.engine]; std::vector<float> w; for (auto& o : opts) w.push_back(o.w);
            rt.dest = layerParamDest(li, opts[(size_t) r.pickWeighted(w)].param);
        }
        // source choice
        std::vector<float> sw(kNumModSources, 0.0f);
        for (int s = kSrcLfo1; s <= kSrcLfo4; ++s) sw[(size_t) s] = .35f / 4.0f * 2.0f;
        for (int s = kSrcMseg1; s <= kSrcMseg4; ++s) sw[(size_t) s] = .20f / 4.0f * 2.0f;
        sw[kSrcSH1] = sw[kSrcSH2] = .05f; sw[kSrcChaos1] = sw[kSrcChaos2] = .02f + .12f * in.chaos; sw[kSrcRandomWalk] = .06f; sw[kSrcStep] = .06f;
        sw[kSrcVelocity] = .05f; sw[kSrcNote] = .02f; sw[kSrcMacro1] = sw[kSrcMacro2] = .02f;
        rt.source = r.pickWeighted(sw);
        const float sgn = r.chance(.5f) ? 1.0f : -1.0f;
        rt.depth = sgn * lerp(.08f, .55f, in.motion) * R(r, .5f, 1.2f);
        rt.curve = r.chance(.2f) ? r.rangeInt(1, 3) : 0; rt.slewMs = r.chance(.25f) ? R(r, 2, 40) : 0.0f; rt.syncDiv = 4;
        ModEngine::validate(rt);
        // configure the chosen modulator
        if (rt.source >= kSrcLfo1 && rt.source <= kSrcLfo4)
        {
            auto& m = d.mods.lfo[(size_t) (rt.source - kSrcLfo1)];
            static const float shapeW[] = { .30f, .20f, .10f, .05f, .10f, .20f, .05f };
            m.shape = r.pickWeighted(std::vector<float>(shapeW, shapeW + 7)); m.sync = r.chance(.55f);
            static const int divs[] = { 1, 2, 4, 8, 16 }; m.syncDiv = divs[r.rangeInt(0, 4)];
            m.rateHz = expRange(r, .3f, lerp(3.0f, 14.0f, in.motion)); m.phase = r.uniform();
            if (m.shape == 6) for (auto& c : m.curve) c = r.uniform();
        }
        else if (rt.source >= kSrcMseg1 && rt.source <= kSrcMseg4)
        {
            auto& m = d.mods.mseg[(size_t) (rt.source - kSrcMseg1)];
            if (m.numPoints < 2 || (m.t[1] == 0.0f && m.t[2] == 0.0f))
            {
                const int shape = r.rangeInt(0, 3);   // 0 decay, 1 rise, 2 hump, 3 dip-and-rise
                m.numPoints = shape >= 2 ? 3 : 2; m.t = { 0, shape >= 2 ? .5f : 1.f, 1, 1, 1, 1, 1, 1 };
                if (shape == 0) m.v = { 1, 0 }; else if (shape == 1) m.v = { 0, 1 }; else if (shape == 2) m.v = { 0, 1, 0 }; else m.v = { 1, 0, 1 };
                m.c = { 0, R(r, -.6f, .6f), R(r, -.6f, .6f) }; m.lengthMs = durMs * R(r, .5f, 1.2f); m.loop = false;
            }
        }
        else if (rt.source == kSrcSH1 || rt.source == kSrcSH2)
        { auto& m = d.mods.sh[(size_t) (rt.source - kSrcSH1)]; m.sync = r.chance(.5f); m.syncDiv = 4 << r.rangeInt(0, 2); m.rateHz = expRange(r, 2, 16); m.slewMs = r.chance(.5f) ? R(r, 5, 60) : 0.0f; }
        else if (rt.source == kSrcChaos1 || rt.source == kSrcChaos2)
        { auto& m = d.mods.chaos[(size_t) (rt.source - kSrcChaos1)]; m.rateHz = expRange(r, 1.5f, 20); m.r = R(r, 3.7f, 3.99f); m.type = r.chance(.3f) ? 1 : 0; }
        else if (rt.source == kSrcRandomWalk) { d.mods.rw.rateHz = expRange(r, 4, 30); d.mods.rw.step = R(r, .1f, .4f); }
        else if (rt.source == kSrcStep)
        { auto& s = d.mods.step; s.length = 4 << r.rangeInt(0, 2); for (auto& v : s.steps) v = r.uniform(); s.sync = r.chance(.7f); s.syncDiv = 4 << r.rangeInt(0, 2); s.rateHz = expRange(r, 3, 14); s.slewMs = r.chance(.6f) ? R(r, 2, 25) : 0.0f; }
        d.routes[(size_t) slot++] = rt; (void) nLfo; (void) nSh; (void) nCh; (void) nStep; (void) nRw; (void) nMseg;
    }
}

// --------------------------------------------------------------------------------------------
// Effects: Space controls send levels, delay / reverb character and stereo width.
// --------------------------------------------------------------------------------------------
void applyEffects(CandyDNA& d, Rng& r, const Intent& in)
{
    auto& fx = d.fx;
    const float sp = in.space;
    fx.delayDiv = r.chance(.6f) ? 1 : 2; fx.delayFeedback = std::clamp(lerp(.12f, .55f, sp) + r.bipolar() * .08f, .05f, .7f); fx.delayTone = R(r, .35f, .75f) * (.6f + .4f * in.brightness);
    fx.pingPong = r.chance(.75f); fx.delayLevel = .75f;
    fx.reverbSize = std::clamp(lerp(.2f, .85f, sp) + r.bipolar() * .08f, .1f, .95f); fx.reverbDamp = std::clamp(1.0f - in.brightness * .7f + r.bipolar() * .1f, .1f, .9f); fx.reverbLevel = .7f;
    fx.modType = r.rangeInt(0, 2); fx.modRate = R(r, .1f, .5f); fx.modDepth = R(r, .3f, .8f); fx.modFeedback = R(r, .35f, .65f); fx.modLevel = .6f;
    fx.width = lerp(.95f, 1.5f, sp); fx.masterGainDb = 0.0f;
    fx.lowCutHz = (d.category == Category::AtmosphericCandy || d.category == Category::OrganicTextures || d.category == Category::Transitions) ? 70.0f : 110.0f;
    for (int i = 0; i < d.numLayers; ++i)
    {
        auto& l = d.layers[(size_t) i]; if (! l.active) continue;
        const float roleBias = (d.role == MusicalRole::Texture || d.role == MusicalRole::Transition) ? 1.0f : (d.role == MusicalRole::Punctuation ? .35f : .6f);
        l.sendReverb = std::clamp(sp * roleBias * R(r, .3f, 1.0f) + (l.sendReverb > 0 ? l.sendReverb * .5f : 0.0f), 0.0f, .95f);
        l.sendDelay = std::clamp(sp * R(r, 0.0f, .9f) * (.5f + .5f * in.density), 0.0f, .9f);
        if (l.sendMod == 0.0f) l.sendMod = r.chance(.3f + .3f * in.motion) ? R(r, .1f, .5f) : 0.0f;
        if (i > 0 && l.pan == 0.0f) l.pan = R(r, -.5f, .5f) * (.4f + .6f * sp);
        // inserts
        if (l.insert == InsertFx::None && r.chance(.05f + .3f * in.chaos * in.complexity))
        {
            static const float w[] = { 0, .25f, .15f, .12f, .12f, .1f, .12f, .08f, .06f };
            l.insert = (InsertFx) r.pickWeighted(std::vector<float>(w, w + 9)); l.insertA = R(r, .2f, .8f); l.insertB = R(r, .2f, .8f);
        }
    }
}

// ============================================================================ archetype table
const std::vector<ArchetypeDef>& archetypeTable()
{
    using C = Category; using M = MusicalRole;
    static const std::vector<ArchetypeDef> t = {
        // Micro Details
        { "Liquid Droplet",       C::MicroDetails, M::Accent,      1.0f, .5f, a_droplet },
        { "Laser Zap",            C::MicroDetails, M::Accent,      1.0f, .6f, a_laser },
        { "Synthetic Blip",       C::MicroDetails, M::Accent,      1.2f, .4f, a_blip },
        { "Digital Chirp",        C::MicroDetails, M::Fill,        0.9f, .6f, a_chirp },
        { "Click Tick",           C::MicroDetails, M::Punctuation, 1.0f, .2f, a_click },
        { "Bleep Pair",           C::MicroDetails, M::Fill,        0.8f, .8f, a_bleepPair },
        // Psychedelic Movement
        { "Resonant Sweep",       C::PsychedelicMovement, M::Movement, 1.1f, 1.0f, a_sweep },
        { "Phase Swirl",          C::PsychedelicMovement, M::Movement, 1.0f, 1.0f, a_swirl },
        { "Pitch Spiral",         C::PsychedelicMovement, M::Movement, 1.0f, 0.8f, a_spiral },
        { "Morphing Bubble",      C::PsychedelicMovement, M::Fill,     1.0f, 0.6f, a_bubble },
        // Organic Textures
        { "Alien Breath",         C::OrganicTextures, M::Texture,  1.1f, 1.2f, a_breath },
        { "Insect Chirp",         C::OrganicTextures, M::Accent,   1.0f, .5f, a_insect },
        { "Wet Click Cluster",    C::OrganicTextures, M::Fill,     1.0f, .6f, a_clickCluster },
        { "Throat Pulse",         C::OrganicTextures, M::Texture,  0.9f, .9f, a_throat },
        // Metallic Accents
        { "Metal Tick",           C::MetallicAccents, M::Punctuation, 1.1f, .4f, a_metalTick },
        { "Bell Ping",            C::MetallicAccents, M::Accent,   1.0f, 1.4f, a_bell },
        { "Spring Boing",         C::MetallicAccents, M::Fill,     0.9f, .8f, a_spring },
        { "Alien Percussion",     C::MetallicAccents, M::Accent,   1.0f, .6f, a_alienPerc },
        // Rhythmic Glitches
        { "Stutter Burst",        C::RhythmicGlitches, M::Fill,    1.1f, .8f, a_stutter },
        { "Reverse Slice",        C::RhythmicGlitches, M::Fill,    1.0f, .9f, a_reverseSlice },
        { "Bit Shred",            C::RhythmicGlitches, M::Punctuation, 0.9f, .7f, a_bitShred },
        { "Micro-Loop Chatter",   C::RhythmicGlitches, M::Fill,    1.0f, .8f, a_microLoop },
        // Atmospheric Candy
        { "Spectral Wash",        C::AtmosphericCandy, M::Texture, 1.1f, 1.4f, a_wash },
        { "Granular Cloud",       C::AtmosphericCandy, M::Texture, 1.1f, 1.4f, a_cloud },
        { "Frozen Shimmer",       C::AtmosphericCandy, M::Texture, 1.0f, 1.5f, a_shimmer },
        { "Dark Breath",          C::AtmosphericCandy, M::Texture, 0.8f, 1.4f, a_darkBreath },
        // Transitions
        { "Reverse Riser",        C::Transitions, M::Transition,   1.2f, 1.6f, a_riser },
        { "Downlifter",           C::Transitions, M::Transition,   1.0f, 1.4f, a_downlifter },
        { "Glitch Fill Riser",    C::Transitions, M::Transition,   0.9f, 1.4f, a_glitchRiser },
        { "Spectral Impact Tail", C::Transitions, M::Transition,   0.9f, 1.6f, a_impactTail },
        // Experimental
        { "Ring Machine",         C::Experimental, M::Accent,      1.0f, .8f, a_ringMachine },
        { "Bin Scrambler",        C::Experimental, M::Movement,    1.0f, 1.0f, a_binScrambler },
        { "Chaos Hybrid",         C::Experimental, M::Fill,        1.2f, 1.0f, a_hybrid },
        { "Formant Fold",         C::Experimental, M::Movement,    0.9f, 1.0f, a_formantFold },
    };
    return t;
}

std::vector<int> archetypesOf(Category c)
{
    std::vector<int> v; const auto& t = archetypeTable();
    for (int i = 0; i < (int) t.size(); ++i) if (t[(size_t) i].category == c) v.push_back(i);
    return v;
}
} // namespace dali
