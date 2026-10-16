#include "LiveAnalyzer.h"

namespace dali
{
void LiveAnalyzer::prepare(double sr)
{
    sr_ = sr; lp_.setSampleRate(sr); hp_.setSampleRate(sr); lp_.set(180.0f, 0.707f); hp_.set(2500.0f, 0.707f); reset();
}
void LiveAnalyzer::reset()
{
    envFast_ = envSlow_ = fol_ = rmsAcc_ = lowE_ = midE_ = highE_ = zcr_ = lastSample_ = ll_ = rr_ = lr_ = 0; lp_.reset(); hp_.reset();
    refractory_ = 0; densityAcc_ = 0; densityCount_ = 0; onsets_.store(0);
    for (auto& h : hist_) { h.store(0.0f); }
    histPos_.store(0);
    rms_ = 0; peak_ = 0; follower_ = 0; transient_ = 0; low_ = 0; mid_ = 0; high_ = 0; bright_ = 0; density_ = 0; corr_ = 1;
}

void LiveAnalyzer::process(const float* L, const float* R, int n)
{
    if (n <= 0) return;
    const float aFast = 1.0f - std::exp(-1.0f / (0.0008f * (float) sr_)), aSlow = 1.0f - std::exp(-1.0f / (0.030f * (float) sr_));
    const float aAtk = 1.0f - std::exp(-1.0f / (0.002f * (float) sr_)), aRel = 1.0f - std::exp(-1.0f / (0.12f * (float) sr_));
    const float aBand = 1.0f - std::exp(-1.0f / (0.10f * (float) sr_));
    float pk = 0, e = 0, trMax = 0; int zc = 0; float ll = 0, rr = 0, lr = 0;
    for (int i = 0; i < n; ++i)
    {
        const float l = sanitize(L[i]), r = sanitize(R ? R[i] : L[i]); const float m = 0.5f * (l + r);
        const float a = std::abs(m); pk = std::max(pk, std::max(std::abs(l), std::abs(r))); e += m * m;
        envFast_ += aFast * (a - envFast_); envSlow_ += aSlow * (a - envSlow_);
        fol_ += (a > fol_ ? aAtk : aRel) * (a - fol_);
        const float tr = clamp01((envFast_ - envSlow_) / (envSlow_ + 0.01f) * 0.5f); trMax = std::max(trMax, tr);
        const auto lo = lp_.process(m); const auto hi = hp_.process(m);
        const float lowS = lo.lp, highS = hi.hp, midS = m - lowS - highS;
        lowE_ += aBand * (lowS * lowS - lowE_); midE_ += aBand * (midS * midS - midE_); highE_ += aBand * (highS * highS - highE_);
        if ((m > 0) != (lastSample_ > 0)) { ++zc; }
        lastSample_ = m;
        ll += l * l; rr += r * r; lr += l * r;
    }
    const float blockRms = std::sqrt(e / (float) n);
    rms_.store(blockRms, std::memory_order_relaxed); peak_.store(pk, std::memory_order_relaxed);
    follower_.store(clamp01(fol_ * 2.0f), std::memory_order_relaxed);
    transient_.store(trMax, std::memory_order_relaxed);
    const float tot = lowE_ + midE_ + highE_ + 1.0e-12f;
    low_.store(clamp01(std::sqrt(lowE_) * 3.0f), std::memory_order_relaxed); mid_.store(clamp01(std::sqrt(midE_) * 3.0f), std::memory_order_relaxed); high_.store(clamp01(std::sqrt(highE_) * 3.0f), std::memory_order_relaxed);
    const float zcrHz = 0.5f * (float) zc * (float) sr_ / (float) n;
    zcr_ += 0.1f * (zcrHz - zcr_);
    bright_.store(clamp01(0.5f * (highE_ + 0.4f * midE_) / tot * 2.0f + 0.5f * clamp01(zcr_ / 6000.0f)), std::memory_order_relaxed);
    corr_.store((ll > 1.0e-12f && rr > 1.0e-12f) ? lr / std::sqrt(ll * rr) : 1.0f, std::memory_order_relaxed);

    // onset detection with refractory period
    refractory_ = std::max(0, refractory_ - n);
    if (trMax > 0.45f && refractory_ == 0 && blockRms > 0.003f) { onsets_.fetch_add(1, std::memory_order_relaxed); refractory_ = (int) (0.06 * sr_); densityAcc_ += 1.0f; }
    densityCount_ += n;
    if (densityCount_ >= (int) (2.0 * sr_)) { density_.store(densityAcc_ / 2.0f, std::memory_order_relaxed); densityAcc_ = 0; densityCount_ = 0; }
    const int hp = histPos_.load(std::memory_order_relaxed);
    hist_[(size_t) (hp & (kHistory - 1))].store(clamp01(blockRms * 3.0f), std::memory_order_relaxed); histPos_.store(hp + 1, std::memory_order_relaxed);
}
} // namespace dali
