#pragma once
// Modular effects: send-bus effects (delay / ping-pong, reverb, modulation FX),
// per-layer insert effects, master section.  All processing is allocation free after prepare().
#include "Core/Types.h"
#include "Core/CandyDNA.h"
#include "Core/DspUtil.h"
#include "DSP/Engine.h"

namespace dali
{
//------------------------------------------------------------------------------
class DelayFx    // tempo-synced stereo / ping-pong delay (wet output only)
{
public:
    void prepare(double sr);
    void reset();
    // timeSamples: delay time; feedback 0..0.92; tone 0..1 (lowpass in feedback)
    void setParams(float timeSamples, float feedback, float tone, bool pingPong);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    double sr_ = 44100.0;
    DelayLine dl_[2];
    Smoother time_, fb_, tone_; bool snap_ = true;
    float lp_[2] { 0, 0 }; bool pp_ = true; DcBlocker dc_[2];
};

//------------------------------------------------------------------------------
class ReverbFx   // 8-line feedback delay network with modulated lines (wet only)
{
public:
    void prepare(double sr);
    void reset();
    void setParams(float size, float damp);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    static constexpr int kLines = 8;
    double sr_ = 44100.0;
    DelayLine line_[kLines];
    float baseLen_[kLines] {}, lp_[kLines] {}, lfoPh_[kLines] {}, g_[kLines] {}, dampA_ = 0.5f; uint32_t ctl_ = 0;
    Smoother size_, damp_; bool snap_ = true;
    DcBlocker dcL_, dcR_;
};

//------------------------------------------------------------------------------
class ModFx      // chorus / flanger / phaser (wet only)
{
public:
    void prepare(double sr);
    void reset();
    void setParams(int type, float rateHz, float depth, float feedback);
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);
private:
    double sr_ = 44100.0;
    DelayLine dl_[2];
    float ph_ = 0, fbState_[2] { 0, 0 };
    int type_ = 0; float rate_ = 0.4f, depth_ = 0.5f, fb_ = 0.3f;
    float apX_[2][6] {}, apY_[2][6] {};
};

//------------------------------------------------------------------------------
class InsertFxUnit  // per-layer insert; type selected per event
{
public:
    void prepare(double sr);
    void start(InsertFx type, uint32_t seed);
    // a, b are the (already modulated) normalised insert parameters
    void process(float* L, float* R, int n, float a, float b, float hz, float bpm);
    InsertFx type() const { return type_; }
private:
    double sr_ = 44100.0; InsertFx type_ = InsertFx::None;
    Os2 os_[2]; Svf svf_[2]; DelayLine comb_[2]; Hilbert hil_[2]; Biquad tone_[2];
    float ringPh_ = 0, shPh_ = 0, holdL_ = 0, holdR_ = 0; int holdCount_ = 0;
    float envFast_[2] { 0, 0 }, envSlow_[2] { 0, 0 };
    Smoother gSm_[2];
};

//------------------------------------------------------------------------------
class MasterFx   // low cut, stereo width, gain, output protection
{
public:
    void prepare(double sr);
    void reset();
    void setParams(float widthAmt, float gainDb, float lowCutHz);
    void process(float* L, float* R, int n);
    uint64_t protectCount() const { return protect_; }
    void resetStats() { protect_ = 0; peak_ = 0; }
    float peak() const { return peak_; }
private:
    double sr_ = 44100.0; Biquad hp_[2]; Smoother width_, gain_;
    uint64_t protect_ = 0; float peak_ = 0; float lowCut_ = -1.0f; bool fresh_ = true;
};

} // namespace dali
