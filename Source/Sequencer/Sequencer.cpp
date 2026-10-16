#include "Sequencer.h"
#include <algorithm>
#include <numeric>

namespace dali
{
std::vector<bool> euclid(int steps, int hits, int rotate)
{
    steps = std::max(1, steps); hits = std::clamp(hits, 0, steps);
    std::vector<bool> p((size_t) steps, false);
    for (int i = 0; i < hits; ++i) p[(size_t) ((i * steps) / std::max(1, hits))] = true;       // evenly distributed (floor) form of Bjorklund
    std::vector<bool> q(p.size());
    for (size_t i = 0; i < p.size(); ++i) q[(i + (size_t) (((rotate % steps) + steps) % steps)) % p.size()] = p[i];
    return q;
}

namespace
{
struct Cand { double beat; float vel = 1.0f; float lenMul = 1.0f; int slot = -1; float pitch = 0.0f; float prob = 1.0f; bool noRatchet = false; };

struct Planner
{
    const SeqConfig& cfg; const SeqContext& ctx; Rng rng;
    double stepB, barB, total; int steps, stepsPerBar;
    std::vector<Cand> out;
    Planner(const SeqConfig& c, const SeqContext& x) : cfg(c), ctx(x), rng(hashCombine(c.seed, (uint32_t) c.mode * 2654435761u + 17u))
    {
        stepB = divisionBeats(cfg.division, cfg.feel); barB = (double) std::max(1, ctx.beatsPerBar);
        total = (double) std::clamp(cfg.bars, 1, 32) * barB; steps = std::max(1, (int) std::floor(total / stepB + 1e-6));
        stepsPerBar = std::max(1, (int) std::floor(barB / stepB + 1e-6));
    }
    double swung(int step) const { return (double) step * stepB + ((step & 1) ? cfg.swing * 0.5 * stepB : 0.0); }
    float metricWeight(int step) const
    {
        const double b = (double) step * stepB; const double inBar = std::fmod(b, barB); const double inBeat = std::fmod(b, 1.0);
        if (inBar < 1e-6) return 0.35f;               // bar downbeat: leave to the track
        if (inBeat < 1e-6) return 0.6f;               // other beats
        if (std::abs(inBeat - 0.5) < 1e-6) return 0.9f; // off-beats
        return 1.0f;
    }
    // analysis step for a beat position (analysis grid is 1/16 notes, 4 per beat)
    int aStep(double beat) const
    {
        if (! ctx.analysis || ! ctx.analysis->valid || ctx.analysis->numSteps <= 0) return -1;
        return (int) (((long long) std::floor(beat * 4.0 + 1e-6)) % ctx.analysis->numSteps);
    }
    float opportunity(double beat) const { const int a = aStep(beat); return a < 0 ? 0.5f : ctx.analysis->opportunity[(size_t) a]; }
    float hitScale() const { return lerp(0.12f, 0.62f, clamp01(ctx.density)); }

    void sparse()
    {
        int last = -100;
        for (int s = 0; s < steps; ++s)
        {
            if (s - last < 2) continue;
            const float p = clamp01(cfg.probability) * metricWeight(s) * hitScale() * 2.2f * (0.5f + opportunity(s * stepB));
            if (rng.chance(std::min(p, 0.9f))) { out.push_back({ swung(s) }); last = s; }
        }
    }
    void groove()
    {
        const int perBar = std::max(1, (int) std::round(lerp(1.0f, 5.0f, ctx.density) * (0.5f + clamp01(cfg.probability))));
        const int nBars = std::clamp(cfg.bars, 1, 32);
        static const float fallback[16] = { .15f, .2f, .55f, .9f, .2f, .3f, .5f, .85f, .15f, .25f, .8f, .6f, .2f, .4f, .5f, .95f };
        for (int b = 0; b < nBars; ++b)
        {
            std::vector<std::pair<float, int>> sc;
            for (int k = 0; k < stepsPerBar; ++k)
            {
                const int s = b * stepsPerBar + k; const double beat = (double) s * stepB;
                float w = ctx.analysis ? opportunity(beat) : fallback[k % 16];
                if (ctx.analysis)
                {   // ghost notes just after strong onsets of the track
                    const int a = aStep(beat - stepB); if (a >= 0) w += 0.25f * ctx.analysis->onset[(size_t) a];
                }
                w *= metricWeight(s) * 0.5f + 0.5f; w += rng.uniform() * 0.25f;
                sc.push_back({ w, s });
            }
            std::sort(sc.begin(), sc.end(), [](auto& x, auto& y) { return x.first > y.first; });
            std::vector<int> chosen;
            for (auto& [w, s] : sc) { if ((int) chosen.size() >= perBar) break; bool near = false; for (int c : chosen) if (std::abs(c - s) < 2) near = true; if (! near) chosen.push_back(s); }
            for (int s : chosen) out.push_back({ swung(s) });
        }
    }
    void callResponse()
    {
        const int phrases = std::max(1, std::clamp(cfg.bars, 1, 32) / 2);
        for (int ph = 0; ph < phrases; ++ph)
        {
            const int base = ph * 2 * stepsPerBar;
            const int nCall = rng.rangeInt(1, 1 + (int) std::round(ctx.density * 3.0f));
            std::vector<int> call;
            int pos = rng.rangeInt(stepsPerBar / 4, stepsPerBar / 2);
            for (int i = 0; i < nCall; ++i) { call.push_back(pos); pos += rng.rangeInt(1, 3) * std::max(1, stepsPerBar / 8); }
            const int shift = rng.rangeInt(-1, 2);
            const float semis = (float) (rng.chance(.5f) ? rng.rangeInt(3, 7) : -rng.rangeInt(3, 7));
            for (size_t i = 0; i < call.size(); ++i)
            {
                int c = base + call[i];
                if (c >= steps) continue;
                if (ctx.analysis && opportunity(c * stepB) < 0.12f && rng.chance(0.7f)) c = std::min(steps - 1, c + 1);
                Cand a { swung(c) }; a.slot = 0 % std::max(1, ctx.numSlots); out.push_back(a);
                int r = base + stepsPerBar + call[i] + shift;
                if (r >= 0 && r < steps && rng.chance(clamp01(cfg.probability) * 0.5f + 0.5f)) { Cand b { swung(r) }; b.pitch = semis; b.slot = ctx.numSlots > 1 ? 1 : 0; b.vel = 0.85f; out.push_back(b); }
            }
        }
    }
    void chaos()
    {
        int s = rng.rangeInt(0, std::max(0, stepsPerBar / 2));
        const float mean = lerp(10.0f, 2.5f, clamp01(ctx.density)) * lerp(1.4f, 0.7f, clamp01(cfg.probability));
        while (s < steps)
        {
            if (rng.chance(0.15f + 0.4f * ctx.chaos))
            {   // burst
                const int n = rng.rangeInt(2, 4);
                for (int i = 0; i < n && s + i < steps; ++i) { Cand c { swung(s + i) }; c.vel = 0.6f + 0.1f * (float) i; c.lenMul = 0.6f; out.push_back(c); }
                s += n;
            }
            else out.push_back({ swung(s) });
            s += std::max(1, (int) std::round(-std::log(std::max(1e-4f, rng.uniform())) * mean));
        }
    }
    void phraseEvolution()
    {
        const int nBars = std::clamp(cfg.bars, 1, 32);
        std::vector<int> motif;
        {   // bar 0 motif
            const int n = std::clamp((int) std::round(lerp(2.0f, 6.0f, ctx.density)), 1, stepsPerBar);
            std::vector<int> pool; for (int k = 0; k < stepsPerBar; ++k) pool.push_back(k);
            for (int i = 0; i < n && ! pool.empty(); ++i)
            {
                std::vector<float> w; for (int k : pool) w.push_back(metricWeight(k) * 0.9f + 0.1f);
                const int idx = rng.pickWeighted(w); motif.push_back(pool[(size_t) idx]); pool.erase(pool.begin() + idx);
            }
            std::sort(motif.begin(), motif.end());
        }
        std::vector<int> cur = motif;
        const float ev = clamp01(cfg.evolveAmount);
        for (int b = 0; b < nBars; ++b)
        {
            if (b > 0)
            {
                if (b % 8 == 0) cur = motif;                                              // return to the theme every 8 bars
                else
                {
                    std::vector<int> nxt;
                    for (int k : cur)
                    {
                        if (rng.chance(ev * 0.35f) && cur.size() > 1) continue;            // drop
                        int kk = k; if (rng.chance(ev * 0.6f)) kk = std::clamp(k + rng.rangeInt(-2, 2), 0, stepsPerBar - 1);
                        nxt.push_back(kk);
                    }
                    if (rng.chance(ev * 0.5f)) nxt.push_back(rng.rangeInt(0, stepsPerBar - 1));
                    std::sort(nxt.begin(), nxt.end()); nxt.erase(std::unique(nxt.begin(), nxt.end()), nxt.end());
                    if (nxt.empty()) nxt = motif;
                    cur = nxt;
                }
            }
            for (int k : cur) { Cand c { swung(b * stepsPerBar + k) }; c.pitch = (b % 4 == 3 && rng.chance(ev)) ? (float) rng.rangeInt(-5, 5) : 0.0f; out.push_back(c); }
            if (b % 4 == 3 && nBars > 1 && rng.chance(0.4f + 0.5f * ev))                    // phrase-end fill
            {
                const int f0 = b * stepsPerBar + stepsPerBar - 4;
                for (int i = 0; i < 4; ++i) { Cand c { swung(f0 + i) }; c.vel = 0.55f + 0.1f * (float) i; c.lenMul = 0.7f; out.push_back(c); }
            }
        }
    }
    void euclidean()
    {
        const int n = std::clamp(cfg.euclidSteps, 1, 64);
        const auto pat = euclid(n, cfg.euclidHits, cfg.euclidRotate);
        for (int s = 0; s < steps; ++s) if (pat[(size_t) (s % n)]) out.push_back({ swung(s) });
    }
    void probability()
    {
        std::vector<float> prof((size_t) stepsPerBar);
        for (int k = 0; k < stepsPerBar; ++k) prof[(size_t) k] = std::clamp(metricWeight(k) * (0.25f + 0.75f * rng.uniform()) + (rng.chance(0.15f) ? 0.4f : 0.0f), 0.0f, 1.0f);
        for (int s = 0; s < steps; ++s)
        {
            const float p = std::clamp(prof[(size_t) (s % stepsPerBar)] * clamp01(cfg.probability) * lerp(0.3f, 1.3f, ctx.density), 0.0f, 0.95f);
            if (p < 0.04f) continue;
            Cand c { swung(s) }; c.prob = p; out.push_back(c);
        }
    }
    void transition()
    {
        double pos = 0.0; int guard = 0;
        while (pos < total - 1e-9 && guard++ < 20000)
        {
            const double t = pos / total;
            const double mulRaw = t < 0.5 ? 1.0 : t < 0.75 ? 0.5 : t < 0.9 ? 0.25 : 0.125;
            const double mul = std::max(mulRaw, 0.0625 / stepB);       // never faster than 1/64 note
            const float p = lerp(0.12f, 1.0f, (float) std::pow(t, 1.6)) * (0.5f + clamp01(cfg.probability) * 0.6f);
            if (rng.chance(std::min(p, 1.0f)) || t > 0.92)
            {
                Cand c { pos }; c.vel = lerp(0.4f, 1.0f, (float) t); c.pitch = (float) std::round(t * cfg.pitchVariation * 3.0f); c.lenMul = (float) std::max(0.25, mul * 2.0);
                c.noRatchet = t > 0.5; out.push_back(c);
            }
            pos += stepB * mul;
        }
    }
};

float planVelocity(const SeqConfig& cfg, Rng& r, double beat, float accent)
{
    float v = cfg.velBase + r.bipolar() * cfg.velRange;
    if (std::abs(std::fmod(beat, 1.0)) < 1e-6) v += 0.06f;
    return std::clamp(v * accent, 0.05f, 1.0f);
}
} // namespace

std::vector<SeqEvent> planEvents(const SeqConfig& cfg, const SeqContext& ctx)
{
    Planner pl(cfg, ctx);
    const double total = pl.total;
    std::vector<SeqEvent> keep;
    for (auto& e : cfg.events) if ((cfg.freezeAll || e.frozen || e.locked) && e.position < total) keep.push_back(e);
    if (cfg.freezeAll) { std::sort(keep.begin(), keep.end(), [](auto& a, auto& b) { return a.position < b.position; }); return keep; }

    switch (cfg.mode)
    {
        case SeqMode::Sparse:              pl.sparse(); break;
        case SeqMode::GrooveFollower:      pl.groove(); break;
        case SeqMode::CallResponse:        pl.callResponse(); break;
        case SeqMode::ControlledChaos:     pl.chaos(); break;
        case SeqMode::PhraseEvolution:     pl.phraseEvolution(); break;
        case SeqMode::Euclidean:           pl.euclidean(); break;
        case SeqMode::ProbabilitySequence: pl.probability(); break;
        case SeqMode::TransitionBuilder:   pl.transition(); break;
        default: pl.sparse(); break;
    }
    std::sort(pl.out.begin(), pl.out.end(), [](const Cand& a, const Cand& b) { return a.beat < b.beat; });

    Rng r(hashCombine(cfg.seed, 0xE7A1u));
    std::vector<SeqEvent> res = keep;
    std::vector<int> plannedSteps; for (auto& e : keep) plannedSteps.push_back((int) std::floor(e.position * 4.0));
    const float sens = cfg.mode == SeqMode::TransitionBuilder ? ctx.negSpaceSensitivity * 0.4f : ctx.negSpaceSensitivity;
    const auto exists = [&](double b) { for (auto& e : res) if (std::abs(e.position - b) < 1e-6) return true; return false; };

    for (auto& c : pl.out)
    {
        if (c.beat >= total - 1e-9 || exists(c.beat)) continue;
        // negative space: deliberately leave room
        const int a4 = (int) std::floor(c.beat * 4.0 + 1e-6);
        NegSpaceContext nc;
        if (ctx.analysis && ctx.analysis->valid && ctx.analysis->numSteps > 0)
            nc = contextAt(*ctx.analysis, a4 % ctx.analysis->numSteps, ctx.candyBands, plannedSteps);
        else
        {
            int near = 0; for (int p : plannedSteps) if (std::abs(p - a4) <= 4) ++near;
            nc.eventDensity = clamp01((float) near / 3.0f);
            nc.overlap = 0.7f * nc.eventDensity;                // neighbouring candy overlaps in time / spectrum
            const double inBar = std::fmod(c.beat, pl.barB);
            nc.phraseCrowding = inBar < 1e-6 ? 0.6f : 0.0f;
        }
        Rng rn = r.derive((uint32_t) a4 * 7919u + 3u);
        if (NegativeSpace::shouldOmit(nc, sens, ctx.density, rn)) continue;
        plannedSteps.push_back(a4);

        // ratchets split an event into 2..ratchetMax quick repeats
        int reps = 1;
        if (! c.noRatchet && cfg.ratchetMax >= 2 && r.chance(clamp01(cfg.ratchetProb))) reps = r.rangeInt(2, std::clamp(cfg.ratchetMax, 2, 8));
        const double slice = pl.stepB / (double) reps;
        for (int j = 0; j < reps; ++j)
        {
            SeqEvent e;
            e.position = c.beat + slice * (double) j;
            if (e.position >= total - 1e-9) break;
            e.velocity = planVelocity(cfg, r, e.position, c.vel) * (float) std::pow(0.86, (double) j);
            e.slot = c.slot >= 0 ? c.slot : (ctx.numSlots > 1 ? r.rangeInt(0, ctx.numSlots - 1) : 0);
            e.slot = std::clamp(e.slot, 0, std::max(0, ctx.numSlots - 1));
            e.pitchOffset = (float) std::round(c.pitch + r.normal() * cfg.pitchVariation * 0.7f);
            e.pitchOffset = std::clamp(e.pitchOffset, -24.0f, 24.0f);
            e.lengthBeats = (float) std::max(0.01, (double) cfg.lengthBeats * c.lenMul * (reps > 1 ? 1.0 / (double) reps : 1.0));
            e.modSeed = r.next();
            e.probability = c.prob;
            e.microMs = r.bipolar() * cfg.microtimingMs * (cfg.mode == SeqMode::ControlledChaos ? 2.0f * (0.5f + ctx.chaos) : 1.0f);
            res.push_back(e);
        }
    }
    std::stable_sort(res.begin(), res.end(), [](auto& a, auto& b) { return a.position < b.position; });
    return res;
}

std::vector<SeqEvent> evolveEvents(const SeqConfig& cfg, const SeqContext& ctx, uint32_t evolveSeed)
{
    Rng r(hashCombine(cfg.seed, evolveSeed));
    const double stepB = divisionBeats(cfg.division, cfg.feel);
    const double total = (double) std::clamp(cfg.bars, 1, 32) * (double) std::max(1, ctx.beatsPerBar);
    const float ev = clamp01(cfg.evolveAmount);
    std::vector<SeqEvent> out;
    for (auto e : cfg.events)
    {
        if (cfg.freezeAll || e.frozen || e.locked) { out.push_back(e); continue; }
        if (r.chance(ev * 0.18f) && cfg.events.size() > 1) continue;
        if (r.chance(ev * 0.3f)) e.position = std::clamp(e.position + (double) r.rangeInt(-1, 1) * stepB, 0.0, total - 1e-6);
        e.velocity = std::clamp(e.velocity + r.bipolar() * ev * 0.12f, 0.05f, 1.0f);
        if (r.chance(ev * 0.25f)) e.pitchOffset = std::clamp(e.pitchOffset + (float) r.rangeInt(-2, 2), -24.0f, 24.0f);
        if (r.chance(ev * 0.3f)) e.modSeed = r.next();
        out.push_back(e);
    }
    if (r.chance(ev * 0.4f))
    {
        SeqEvent e; e.position = std::floor(r.uniform() * (float) (total / stepB)) * stepB; e.velocity = cfg.velBase * 0.8f;
        e.slot = ctx.numSlots > 1 ? r.rangeInt(0, ctx.numSlots - 1) : 0; e.lengthBeats = cfg.lengthBeats; e.modSeed = r.next();
        bool dup = false; for (auto& o : out) if (std::abs(o.position - e.position) < 1e-6) dup = true;
        if (! dup && e.position < total) out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.position < b.position; });
    return out;
}

std::shared_ptr<SeqPattern> makePattern(const SeqConfig& cfg, const SeqContext& ctx, bool replan)
{
    auto p = std::make_shared<SeqPattern>();
    p->cfg = cfg; p->cfg.bars = std::clamp(cfg.bars, 1, 32);
    p->lengthBeats = (double) p->cfg.bars * (double) std::max(1, ctx.beatsPerBar);
    p->events = replan ? planEvents(p->cfg, ctx) : cfg.events;
    for (auto& e : p->events) e.slot = std::clamp(e.slot, 0, kNumSoundSlots - 1);
    std::stable_sort(p->events.begin(), p->events.end(), [](auto& a, auto& b) { return a.position < b.position; });
    p->cfg.events = p->events;
    return p;
}

// --------------------------------------------------------------------------------------------------------------
void SeqPlayer::publish(std::shared_ptr<SeqPattern> p)
{
    std::lock_guard<std::mutex> lk(m_);
    graveyard_.push_back(p);
    if (graveyard_.size() > 16) graveyard_.erase(graveyard_.begin());
    cur_.store(p.get(), std::memory_order_release);
}

bool SeqPlayer::eventFires(const SeqPattern& p, size_t i, int64_t loop)
{
    const auto& e = p.events[i];
    if (e.muted) return false;
    if (e.frozen || e.locked || e.probability >= 0.9999f) return true;
    const uint32_t h = hashCombine(hashCombine(p.cfg.seed, (uint32_t) i * 2654435761u), (uint32_t) loop);
    return (float) (h >> 8) * (1.0f / 16777216.0f) < e.probability;
}

int SeqPlayer::render(const HostInfo& host, double sr, int n, CandyEngine& engine, float basePitch)
{
    const SeqPattern* p = cur_.load(std::memory_order_acquire);
    if (p == nullptr || ! p->cfg.enabled || ! host.playing || p->events.empty() || n <= 0) return 0;
    const double bpm = std::clamp(host.bpm, 20.0, 400.0);
    const double beatsPerSample = bpm / 60.0 / sr;
    // Contiguous blocks: continue exactly from the previous block's end so no event is missed or doubled by host ppq rounding.
    double t0 = host.ppq;
    if (haveEnd_ && std::abs(host.ppq - lastEnd_) < beatsPerSample) t0 = lastEnd_;
    const double t1 = t0 + (double) n * beatsPerSample;
    lastEnd_ = t1; haveEnd_ = true;
    constexpr double kEps = 1.0e-9;
    const double L = p->lengthBeats;
    const int64_t k0 = (int64_t) std::floor(t0 / L);
    int queued = 0;
    for (int64_t k = k0 - 1; k <= k0 + 1; ++k)
    {
        const double base = (double) k * L;
        for (size_t i = 0; i < p->events.size(); ++i)
        {
            const auto& e = p->events[i];
            const double t = base + e.position + (double) e.microMs * 0.001 * bpm / 60.0;
            if (t < t0 - kEps || t >= t1 - kEps) continue;      // half-open window shifted by eps: immune to tiny ppq rounding
            if (! eventFires(*p, i, k)) { ++skipped_; continue; }
            if (liveOmit_ > 0.0f && ! e.frozen && ! e.locked)
            {
                const uint32_t h2 = hashCombine(hashCombine(p->cfg.seed ^ 0x5A5Au, (uint32_t) i * 2246822519u), (uint32_t) k);
                if ((float) (h2 >> 8) * (1.0f / 16777216.0f) < liveOmit_) { ++skipped_; continue; }
            }
            EventRequest r;
            r.sampleOffset = std::clamp((int) std::floor((t - t0) / beatsPerSample + 0.5), 0, n - 1);
            r.slot = e.slot; r.velocity = e.velocity; r.pitchOffset = e.pitchOffset; r.midiNote = basePitch;
            r.eventSeed = e.modSeed; r.lengthScale = e.lengthBeats / std::max(1.0e-3f, p->cfg.lengthBeats);
            if (engine.queueEvent(r)) { ++queued; ++played_; } else ++skipped_;
        }
    }
    return queued;
}
} // namespace dali
