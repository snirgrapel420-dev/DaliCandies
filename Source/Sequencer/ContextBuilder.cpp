#include "ContextBuilder.h"
#include "Core/Rng.h"
#include "Generator/Scale.h"
#include <algorithm>
#include <cmath>

namespace dali
{
static bool busyAt(const TrackAnalysis& a, int t) { return a.onset[(size_t) t] > 0.45f || a.transient[(size_t) t] > 0.55f; }

int gapStepsAt(const TrackAnalysis& a, int step)
{
    if (! a.valid || step < 0 || step >= a.numSteps) return 0;
    int n = 0;
    for (int t = step + 1; t < a.numSteps && n < 16; ++t, ++n) if (busyAt(a, t)) break;
    return n;
}

static bool sectionStartNext(const TrackAnalysis& a, int step)
{
    const int bar = step / std::max(1, a.stepsPerBar), k = step % std::max(1, a.stepsPerBar);
    if (k < a.stepsPerBar - 4) return false;
    for (int sb : a.sectionBars) if (sb > 0 && sb == bar + 1) return true;
    return false;
}

std::vector<std::pair<int, int>> findDeadRegions(const TrackAnalysis& a)
{
    std::vector<std::pair<int, int>> out; if (! a.valid || a.numSteps < 16) return out;
    std::vector<float> s(a.rms.begin(), a.rms.end()); std::sort(s.begin(), s.end());
    const float ref = s[(size_t) ((float) (s.size() - 1) * 0.75f)];                    // typical level of the track when it plays
    if (ref < 0.01f) return out;                                                       // no real signal to compare against
    const float thr = 0.22f * ref;
    int start = -1;
    for (int t = 0; t <= a.numSteps; ++t)
    {
        const bool dead = t < a.numSteps && a.rms[(size_t) t] < thr && ! busyAt(a, t);
        if (dead && start < 0) start = t;
        if (! dead && start >= 0) { if (t - start >= 8) out.push_back({ start, t }); start = -1; }
    }
    return out;
}

float placementCost(const TrackAnalysis& a, int step, const PaletteEntry& p)
{
    const double stepMs = 60000.0 / std::max(20.0, a.bpm) / 4.0;
    const float gap = (float) gapStepsAt(a, step);
    float mask = 0.0f; for (int b = 0; b < kNumBands; ++b) mask += p.bands[(size_t) b] * a.band[(size_t) b][(size_t) step];
    const float L = (float) (p.durationMs / stepMs);
    float len = 0.0f;
    if (L > gap + 1.0f) len = std::min(1.0f, (L - gap - 1.0f) / (gap + 3.0f));          // would run into the next hit
    else if (gap >= 8.0f && L < 2.0f) len = 0.15f;                                      // a tick in a wide-open space: allowed, mildly wasteful
    float role = 0.0f;
    const bool toSection = sectionStartNext(a, step);
    if (toSection) role += p.role == MusicalRole::Transition ? -0.45f : 0.08f;
    else if (p.role == MusicalRole::Transition) role += 1.2f;                            // risers only where a section changes
    if (gap >= 8.0f) { if (p.role == MusicalRole::Texture || p.role == MusicalRole::Movement) role -= 0.15f; }
    else if (gap <= 2.0f) { if (p.role == MusicalRole::Punctuation || p.role == MusicalRole::Accent) role -= 0.12f; if (p.role == MusicalRole::Texture) role += 0.25f; }
    else if (p.role == MusicalRole::Fill) role -= 0.05f;
    return 1.6f * mask + len + role;
}

std::vector<SeqEvent> buildContextEvents(const TrackAnalysis& a, const std::vector<PaletteEntry>& palette, const ContextOptions& o, ContextReport* rep)
{
    std::vector<SeqEvent> out; ContextReport r;
    if (! a.valid || palette.empty()) { if (rep) *rep = r; return out; }
    const double stepMs = 60000.0 / std::max(20.0, a.bpm) / 4.0;
    std::vector<int> steps; for (auto& s : a.suggestions) if (! s.rejected && s.step >= 0 && s.step < a.numSteps) steps.push_back(s.step);
    std::sort(steps.begin(), steps.end());
    int prev1 = -1, prev2 = -1; std::vector<int> uses(palette.size(), 0); int placed = 0;
    enum Mode { kNormal = 0, kTransition = 1, kDead = 2, kAnswer = 3 };
    auto place = [&](int step, int mode)
    {
        std::vector<float> cost(palette.size()); float best = 1e9f;
        for (size_t i = 0; i < palette.size(); ++i)
        {
            float c = placementCost(a, step, palette[i]);
            const auto role = palette[i].role;
            if (mode == kTransition) { if (role != MusicalRole::Transition) c += 1.0f; else c -= 1.2f; }          // cancels the "risers only at section changes" penalty
            if (mode == kDead) { if (role == MusicalRole::Texture || role == MusicalRole::Movement) c -= 0.5f; else if (role == MusicalRole::Punctuation) c += 0.4f; if (palette[i].durationMs > 600.0f) c -= 0.2f; }
            if (mode == kAnswer) { if (role == MusicalRole::Accent || role == MusicalRole::Punctuation || role == MusicalRole::Fill) c -= 0.25f; else if (role == MusicalRole::Texture) c += 0.3f; }
            if ((int) i == prev1) c += 0.9f * o.variety; else if ((int) i == prev2) c += 0.4f * o.variety;
            if (placed > 0) c += 0.5f * o.variety * ((float) uses[i] / (float) placed);
            cost[i] = c; best = std::min(best, c);
        }
        Rng rng(hashCombine(o.seed, (uint32_t) step * 2654435761u + 17u));
        const float T = 0.04f + 0.3f * o.variety;
        std::vector<float> w(palette.size()); for (size_t i = 0; i < w.size(); ++i) w[i] = std::exp(-(cost[i] - best) / T);
        const int pick = rng.pickWeighted(w);
        const auto& pe = palette[(size_t) pick];
        SeqEvent e; e.position = (double) step * 0.25; e.slot = pe.slot; e.frozen = true; e.modSeed = rng.next();
        const float rms = a.rms[(size_t) step];
        e.velocity = std::clamp(o.velBase * (1.0f - 0.35f * rms) + rng.bipolar() * o.velRange * 0.5f, 0.12f, 1.0f);
        const float gap = (float) gapStepsAt(a, step);
        const float scale = std::clamp((float) (((gap + 1.0f) * stepMs) / std::max(1.0f, pe.durationMs)), 0.4f, 1.0f);
        e.lengthBeats = 0.25f * scale;                                              // 0.25 = the sound's natural length
        const float raw = std::round(rng.normal() * o.pitchVariation * 0.7f);
        e.pitchOffset = std::clamp(quantizeToScale(60.0f + raw, o.rootNote, o.scale, o.musicality) - 60.0f, -12.0f, 12.0f);
        out.push_back(e); prev2 = prev1; prev1 = pick; ++uses[(size_t) pick]; ++placed; ++r.placed; ++r.perSlot[(size_t) std::clamp(pe.slot, 0, kNumSoundSlots - 1)];
    };
    const int budget = std::max(1, (int) std::round((float) a.bars * (0.5f + 5.5f * std::clamp(o.amount, 0.0f, 1.0f))));
    std::vector<char> taken((size_t) a.numSteps, 0);
    auto freeAround = [&](int t, int r) { for (int d = -r; d <= r; ++d) { const int u = t + d; if (u >= 0 && u < a.numSteps && taken[(size_t) u]) return false; } return true; };
    auto commit = [&](int t, int mode) { place(t, mode); taken[(size_t) t] = 1; };

    // 1. dead parts: a long texture where the track has gone quiet, and a pick-up that leads back into the track
    const auto dead = findDeadRegions(a); r.deadRegions = (int) dead.size();
    for (auto [d0, d1] : dead)
    {
        const int len = d1 - d0; const int count = std::clamp(len / 16, 1, 3);
        for (int k = 0; k < count; ++k)
        {   const int t = std::min(d1 - 1, d0 + 1 + k * 16); if (! busyAt(a, t) && freeAround(t, 3)) { commit(t, kDead); ++r.deadFills; } }
        if (d1 < a.numSteps - 1)
        {   const int t = std::max(d0, d1 - 4); bool hasTr = false; for (auto& p : palette) hasTr = hasTr || p.role == MusicalRole::Transition;
            if (hasTr && ! busyAt(a, t) && freeAround(t, 2)) { commit(t, kTransition); ++r.transitions; ++r.deadFills; } }
    }
    // 2. answers: a short sound a couple of steps after a strong hit, in the room the hit leaves (call and response)
    {
        std::vector<int> hits; for (int t = 0; t < a.numSteps; ++t) if (a.transient[(size_t) t] > 0.7f) hits.push_back(t);
        std::vector<std::pair<float, int>> cand;
        for (int h : hits)
            for (int dlt : { 2, 3 })
            {   const int t = h + dlt; if (t >= a.numSteps || busyAt(a, t) || gapStepsAt(a, t) < 1) continue;
                cand.push_back({ a.opportunity[(size_t) t] + (dlt == 2 ? 0.08f : 0.0f), t }); }
        std::sort(cand.begin(), cand.end(), [](auto& x, auto& y) { return x.first > y.first; });
        const int maxAnswers = std::max(1, (int) std::round(0.4f * (float) budget));
        for (auto& [sc, t] : cand)
        {   if (r.answers >= maxAnswers) break; if (sc < 0.45f || ! freeAround(t, 3)) continue; commit(t, kAnswer); ++r.answers; }
    }
    // 3. the rest of the budget: the best-scoring free spots found by the analysis
    for (int s : steps)
    {   // never on top of a hit: nudge by one step to a free neighbour, otherwise leave this spot out
        if (r.placed >= budget + r.deadFills) break;
        int st = s;
        if (busyAt(a, st))
        {   st = -1;
            for (int d : { 1, -1, 2, -2, 3, -3 }) { const int t = s + d; if (t >= 0 && t < a.numSteps && ! busyAt(a, t)) { st = t; break; } }
        }
        if (st < 0) { ++r.dropped; continue; }
        if (! freeAround(st, 1)) continue;
        commit(st, kNormal);
    }
    if (o.addTransitions)
    {   // a transition sound leads into every estimated section change that has none nearby
        bool anyTransition = false; for (auto& p : palette) anyTransition = anyTransition || p.role == MusicalRole::Transition;
        if (anyTransition)
            for (int sb : a.sectionBars)
            {
                if (sb <= 0 || (sb - 1) * a.stepsPerBar >= a.numSteps) continue;
                const int step = sb * a.stepsPerBar - 4; if (step < 0 || step >= a.numSteps) continue;
                bool near = false; for (auto& e : out) if (std::abs(e.position - (double) step * 0.25) < 1.0) near = true;
                if (! near) { place(step, kTransition); ++r.transitions; }
            }
    }
    std::stable_sort(out.begin(), out.end(), [](const SeqEvent& x, const SeqEvent& y) { return x.position < y.position; });
    if (rep) *rep = r;
    return out;
}
}
