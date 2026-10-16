#include "Effects.h"

namespace dali
{
//==============================================================================
// Delay
//==============================================================================
void DelayFx::prepare(double sr)
{
    sr_ = sr;
    for (auto& d : dl_) d.prepare((int) (sr * 3.0) + 8);
    time_.setup(sr, 60.0f); fb_.setup(sr, 20.0f); tone_.setup(sr, 20.0f);
    for (auto& d : dc_) d.setup(sr);
    reset();
}
void DelayFx::reset()
{
    for (auto& d : dl_) d.clear();
    lp_[0] = lp_[1] = 0; snap_ = true;     // smoothers jump to the first parameters after a reset (identical start every time)
    for (auto& d : dc_) d.reset();
}
void DelayFx::setParams(float timeSamples, float feedback, float tone, bool pingPong)
{
    time_.set(std::clamp(timeSamples, 32.0f, (float) (sr_ * 2.9)));
    fb_.set(std::clamp(feedback, 0.0f, 0.92f)); tone_.set(clamp01(tone)); pp_ = pingPong;
    if (snap_) { time_.reset(time_.target); fb_.reset(fb_.target); tone_.reset(tone_.target); snap_ = false; }
}
void DelayFx::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float t = time_.tick(), fb = fb_.tick(), tone = tone_.tick();
        const float a = 1.0f - std::exp(-kTwoPi * expMap(tone, 800.0f, 16000.0f) / (float) sr_);
        const float dL = dl_[0].read(t), dR = dl_[1].read(t);
        lp_[0] += a * (dL - lp_[0]); lp_[1] += a * (dR - lp_[1]);
        const float fL = softClip(dc_[0].process(lp_[0])) * fb, fR = softClip(dc_[1].process(lp_[1])) * fb;
        if (pp_)
        {   // ping-pong: mono-summed input into the left line; lines cross-feed
            dl_[0].push(killDenormal(sanitize(0.5f * (inL[i] + inR[i]) + fR)));
            dl_[1].push(killDenormal(sanitize(fL)));
        }
        else
        {
            dl_[0].push(killDenormal(sanitize(inL[i] + fL)));
            dl_[1].push(killDenormal(sanitize(inR[i] + fR)));
        }
        outL[i] = dL; outR[i] = dR;
    }
}

//==============================================================================
// Reverb (FDN)
//==============================================================================
void ReverbFx::prepare(double sr)
{
    sr_ = sr;
    static const float lens44[kLines] = { 1117, 1259, 1423, 1583, 1777, 1913, 2089, 2251 };
    for (int i = 0; i < kLines; ++i)
    {
        baseLen_[i] = lens44[i] * (float) (sr / 44100.0);
        line_[i].prepare((int) (baseLen_[i] * 3.2f) + 64);
        lfoPh_[i] = (float) i / (float) kLines;
    }
    size_.setup(sr, 80.0f); damp_.setup(sr, 40.0f); dcL_.setup(sr); dcR_.setup(sr);
    reset();
}
void ReverbFx::reset()
{
    for (auto& l : line_) l.clear();
    for (auto& v : lp_) v = 0;
    snap_ = true; dcL_.reset(); dcR_.reset(); ctl_ = 0;
    for (int i = 0; i < kLines; ++i) lfoPh_[i] = (float) i / (float) kLines;
}
void ReverbFx::setParams(float size, float damp)
{
    size_.set(clamp01(size)); damp_.set(clamp01(damp));
    if (snap_) { size_.reset(size_.target); damp_.reset(damp_.target); snap_ = false; }
}
void ReverbFx::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float size = size_.tick(), damp = damp_.tick();
        const float scale = 0.5f + 2.0f * size;                           // line length scale
        if ((ctl_++ & 15) == 0)
        {
            const float rt60 = 0.25f + 5.0f * size * size;                // seconds
            dampA_ = 1.0f - std::exp(-kTwoPi * expMap(1.0f - damp, 1200.0f, 16000.0f) / (float) sr_);
            for (int k = 0; k < kLines; ++k)
                g_[k] = std::pow(10.0f, -3.0f * baseLen_[k] * scale / ((float) sr_ * rt60));
        }
        float v[kLines];
        for (int k = 0; k < kLines; ++k)
        {
            lfoPh_[k] += (0.11f + 0.037f * (float) k) / (float) sr_; lfoPh_[k] -= std::floor(lfoPh_[k]);
            const float len = baseLen_[k] * scale + 1.5f * std::sin(kTwoPi * lfoPh_[k]);
            float x = line_[k].read(len);
            lp_[k] += dampA_ * (x - lp_[k]); x = lp_[k] * g_[k];
            v[k] = x;
        }
        // Hadamard feedback matrix (orthogonal, scaled)
        for (int s = 1; s < kLines; s <<= 1)
            for (int a = 0; a < kLines; a += 2 * s)
                for (int b = a; b < a + s; ++b) { const float x = v[b], y = v[b + s]; v[b] = x + y; v[b + s] = x - y; }
        const float norm = 0.35355339f;
        const float inM = 0.5f * (inL[i] + inR[i]), inS = 0.5f * (inL[i] - inR[i]);
        float oL = 0, oR = 0;
        for (int k = 0; k < kLines; ++k)
        {
            const float inj = (k & 1 ? inS : inM) * ((k & 2) ? 1.0f : -1.0f) + inM * 0.3f;
            line_[k].push(killDenormal(sanitize(v[k] * norm + inj)));
            if (k & 1) oR += v[k] * ((k & 2) ? 1.0f : -1.0f); else oL += v[k] * ((k & 2) ? 1.0f : -1.0f);
        }
        outL[i] = dcL_.process(oL * 0.28f); outR[i] = dcR_.process(oR * 0.28f);
    }
}

//==============================================================================
// Mod FX
//==============================================================================
void ModFx::prepare(double sr) { sr_ = sr; for (auto& d : dl_) d.prepare((int) (sr * 0.05) + 8); reset(); }
void ModFx::reset()
{
    for (auto& d : dl_) d.clear();
    ph_ = 0; fbState_[0] = fbState_[1] = 0;
    for (auto& c : apX_) for (auto& v : c) v = 0;
    for (auto& c : apY_) for (auto& v : c) v = 0;
}
void ModFx::setParams(int type, float rateHz, float depth, float feedback)
{ type_ = std::clamp(type, 0, 2); rate_ = std::clamp(rateHz, 0.02f, 12.0f); depth_ = clamp01(depth); fb_ = std::clamp(feedback, -0.88f, 0.88f); }
void ModFx::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const float* in[2] = { inL, inR }; float* out[2] = { outL, outR };
    for (int i = 0; i < n; ++i)
    {
        ph_ += rate_ / (float) sr_; ph_ -= std::floor(ph_);
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = std::sin(kTwoPi * (ph_ + (c ? 0.25f : 0.0f)));
            const float x = in[c][i];
            float y = 0.0f;
            if (type_ == 0)          // chorus: two detuned taps
            {
                const float d1 = (float) sr_ * 0.001f * (11.0f + 5.0f * depth_ * lfo);
                const float d2 = (float) sr_ * 0.001f * (17.0f + 6.0f * depth_ * std::sin(kTwoPi * (ph_ * 0.83f + 0.4f + (c ? 0.2f : 0.0f))));
                dl_[c].push(x);
                y = 0.5f * (dl_[c].read(d1) + dl_[c].read(d2));
            }
            else if (type_ == 1)     // flanger
            {
                const float d = (float) sr_ * 0.001f * (0.8f + 2.6f * (0.5f + 0.5f * lfo) * (0.3f + 0.7f * depth_));
                const float wet = dl_[c].read(d);
                dl_[c].push(killDenormal(sanitize(x + fb_ * wet)));
                y = wet;
            }
            else                     // phaser: 6 allpass stages
            {
                const float f = expMap(0.5f + 0.5f * lfo * depth_, 180.0f, 3800.0f);
                const float tn = std::tan(kPi * f / (float) sr_); const float a = (tn - 1.0f) / (tn + 1.0f);
                float s = x + fbState_[c] * fb_;
                for (int k = 0; k < 6; ++k)
                {   const float yk = a * s + apX_[c][k] - a * apY_[c][k]; apX_[c][k] = s; apY_[c][k] = killDenormal(yk); s = yk; }
                fbState_[c] = killDenormal(sanitize(s));
                y = 0.5f * (x + s);
            }
            out[c][i] = y;
        }
    }
}

//==============================================================================
// Insert FX
//==============================================================================
void InsertFxUnit::prepare(double sr)
{
    sr_ = sr;
    for (auto& o : os_) o.prepare(sr);
    for (auto& s : svf_) s.setSampleRate(sr);
    for (auto& c : comb_) c.prepare((int) (sr / 30.0) + 8);
    for (auto& g : gSm_) g.setup(sr, 2.0f);
}
void InsertFxUnit::start(InsertFx type, uint32_t)
{
    type_ = type;
    for (auto& o : os_) o.reset();
    for (auto& s : svf_) s.reset();
    for (auto& c : comb_) c.clear();
    for (auto& h : hil_) h.reset();
    for (auto& t : tone_) t.reset();
    ringPh_ = shPh_ = 0; holdL_ = holdR_ = 0; holdCount_ = 0;
    for (int c = 0; c < 2; ++c) { envFast_[c] = envSlow_[c] = 0; gSm_[c].reset(1.0f); }
}
void InsertFxUnit::process(float* L, float* R, int n, float a, float b, float hz, float)
{
    if (type_ == InsertFx::None) return;
    float* ch[2] = { L, R };
    a = clamp01(a); b = clamp01(b);
    switch (type_)
    {
        case InsertFx::Saturation:
        {
            const float drive = 1.0f + a * a * 24.0f; const float norm = 1.0f / std::sqrt(drive);
            for (auto& t : tone_) t.setLowpass(sr_, expMap(b, 1500.0f, 18000.0f), 0.707f);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
                ch[c][i] = tone_[c].process(os_[c].process(ch[c][i], [drive, norm](float x) { return fastTanh(x * drive) * norm; }));
            break;
        }
        case InsertFx::Wavefolder:
        {
            const float k = 1.0f + a * 7.0f, bias = (b - 0.5f) * 0.6f;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
                ch[c][i] = os_[c].process(ch[c][i], [k, bias](float x) { return std::sin(kPi * 0.5f * (x * k + bias)) * 0.9f; });
            break;
        }
        case InsertFx::Bitcrusher:
        {
            const float bits = 16.0f - a * 13.0f, step = std::pow(2.0f, 1.0f - bits);
            const int hold = 1 + (int) (b * b * 24.0f);
            for (int i = 0; i < n; ++i)
            {
                if (--holdCount_ <= 0) { holdCount_ = hold; holdL_ = std::floor(L[i] / step + 0.5f) * step; holdR_ = std::floor(R[i] / step + 0.5f) * step; }
                L[i] = holdL_; R[i] = holdR_;
            }
            break;
        }
        case InsertFx::RingMod:
        {
            const float f = expMap(a, 20.0f, 4000.0f); const float inc = f / (float) sr_;
            for (int i = 0; i < n; ++i)
            {
                ringPh_ += inc; ringPh_ -= std::floor(ringPh_);
                const float m = std::sin(kTwoPi * ringPh_);
                L[i] = lerp(L[i], L[i] * m, b); R[i] = lerp(R[i], R[i] * m, b);
            }
            break;
        }
        case InsertFx::FreqShift:
        {
            const float sh = (a - 0.5f) * 2000.0f; const float inc = sh / (float) sr_;
            for (int i = 0; i < n; ++i)
            {
                shPh_ += inc; shPh_ -= std::floor(shPh_);
                const float cs = std::cos(kTwoPi * shPh_), sn = std::sin(kTwoPi * shPh_);
                for (int c = 0; c < 2; ++c)
                {
                    float re, im; hil_[c].process(ch[c][i], re, im);
                    ch[c][i] = lerp(ch[c][i], re * cs - im * sn, b);
                }
            }
            break;
        }
        case InsertFx::Comb:
        {
            const float f = std::clamp(expMap(a, 40.0f, 2000.0f), 30.0f, (float) sr_ * 0.2f); const float d = (float) sr_ / f;
            const float fb = (b - 0.5f) * 1.8f;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
            {
                const float y = comb_[c].read(d);
                comb_[c].push(killDenormal(sanitize(softClip(ch[c][i] + fb * y))));
                ch[c][i] = 0.5f * (ch[c][i] + y);
            }
            break;
        }
        case InsertFx::Filter:
        {
            const float fc = expMap(a, 60.0f, 16000.0f), q = 0.6f + b * b * 14.0f;
            for (int c = 0; c < 2; ++c) { svf_[c].set(fc, q); for (int i = 0; i < n; ++i) ch[c][i] = svf_[c].tick(ch[c][i], 0); }
            break;
        }
        case InsertFx::Transient:
        {
            const float att = (a - 0.5f) * 2.0f, sus = (b - 0.5f) * 2.0f;
            const float fa = 1.0f - std::exp(-1.0f / (0.0006f * (float) sr_)), ss = 1.0f - std::exp(-1.0f / (0.02f * (float) sr_));
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i)
            {
                const float x = std::abs(ch[c][i]);
                envFast_[c] += fa * (x - envFast_[c]); envSlow_[c] += ss * (x - envSlow_[c]);
                const float d = (envFast_[c] - envSlow_[c]) / (envSlow_[c] + 1.0e-4f);
                float g = 1.0f + att * std::clamp(d, 0.0f, 2.0f) * 1.2f + sus * std::clamp(-d, 0.0f, 1.0f) * 0.8f;
                gSm_[c].set(std::clamp(g, 0.2f, 4.0f));
                ch[c][i] *= gSm_[c].tick();
            }
            break;
        }
        default: break;
    }
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) ch[c][i] = sanitize(ch[c][i]);
    (void) hz;
}

//==============================================================================
// Master
//==============================================================================
void MasterFx::prepare(double sr) { sr_ = sr; width_.setup(sr, 20.0f); gain_.setup(sr, 20.0f); reset(); }
void MasterFx::reset() { for (auto& h : hp_) h.reset(); width_.reset(1.0f); gain_.reset(1.0f); protect_ = 0; peak_ = 0; lowCut_ = -1.0f; fresh_ = true; }
void MasterFx::setParams(float widthAmt, float gainDb, float lowCutHz)
{
    const float w = std::clamp(widthAmt, 0.0f, 2.0f), g = dbToGain(std::clamp(gainDb, -60.0f, 12.0f));
    if (fresh_) { width_.reset(w); gain_.reset(g); fresh_ = false; }     // no start-up ramp from unity: first event is at its true level
    width_.set(w); gain_.set(g);
    if (std::abs(lowCutHz - lowCut_) > 0.01f)
    { lowCut_ = lowCutHz; for (auto& h : hp_) { const auto z1 = h.z1, z2 = h.z2; h.setHighpass(sr_, std::clamp(lowCutHz, 10.0f, 400.0f), 0.707f); h.z1 = z1; h.z2 = z2; } }
}
void MasterFx::process(float* L, float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float l = hp_[0].process(L[i]), r = hp_[1].process(R[i]);
        const float w = width_.tick(), g = gain_.tick();
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
        l = (m + s) * g; r = (m - s) * g;
        // output protection: transparent below 0.95, smooth knee above; counts every intervention
        auto prot = [this](float x) {
            const float a = std::abs(x);
            if (! (a < 1.0e6f)) { ++protect_; return 0.0f; }
            if (a <= 0.95f) return x;
            ++protect_;
            const float over = a - 0.95f;
            return (x < 0 ? -1.0f : 1.0f) * (0.95f + 0.05f * fastTanh(over / 0.05f));
        };
        l = prot(l); r = prot(r);
        peak_ = std::max(peak_, std::max(std::abs(l), std::abs(r)));
        L[i] = l; R[i] = r;
    }
}
} // namespace dali
