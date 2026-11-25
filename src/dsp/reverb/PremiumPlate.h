// Premium Plate v1 (v0.5, type "reverb_plate_premium"): a figure-of-eight plate tank after J. Dattorro,
// "Effect Design Part 1" (JAES 1997, published topology and reference delay lengths at 29761 Hz), extended with
//  * early reflections (stereo tapped delay, decoupled from the late tank) and pre-delay,
//  * frequency-dependent decay: per-segment RT60-derived gains with a separate low-band multiplier
//    (crossover one-pole split) and high-frequency damping (one-pole low-pass), both inside the loop,
//  * modulated tank all-passes (fractional Hermite reads, quadrature LFOs) for decorrelation,
//  * size scaling with smoothed fractional delay lengths (no clicks on size changes),
//  * input band-limiting (low cut / bandwidth), stereo width (M/S on the wet signal).
// Independent of the existing "reverb" block (its 5 types and presets are untouched).
// All buffers are allocated in prepare() for the largest size at the current sample rate (mono lines).
#pragma once

#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

/// Single-channel power-of-two delay line with integer and Hermite reads.
class MonoDelay {
public:
    void prepare(int maxDelay);
    void reset();
    inline void push(float x) { buf_[(size_t)w_] = x; w_ = (w_ + 1) & mask_; }
    /// delay d >= 1 relative to the next write position (d = 1: last pushed sample)
    inline float readInt(int d) const { return buf_[(size_t)((w_ - d) & mask_)]; }
    float read(float d) const;
    size_t bytes() const { return buf_.size() * sizeof(float); }
private:
    std::vector<float> buf_;
    int mask_ = 0, w_ = 0;
};

class PremiumPlate : public Effect {
public:
    enum P { kDecay, kPreDelay, kSize, kDamping, kBassMult, kCrossover, kDiffusion, kModDepth, kModRate,
             kEarly, kLowCut, kWidth, kMix, kNumParams };
    PremiumPlate();
    const char* typeId() const override { return "reverb_plate_premium"; }
    const char* displayName() const override { return "Premium Plate"; }
    const char* category() const override { return "Reverb"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    /// Memory held by the delay lines (bytes), for hardware budgeting.
    size_t memoryBytes() const;

    static constexpr double kRefRate = 29761.0;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    void updateGains();
    float allpass(MonoDelay& d, float len, float g, float x) const;

    double fs_ = 48000.0, scaleRef_ = 1.0; // fs / 29761
    // input
    Biquad lowCut_;
    float bw_ = 0.9995f, bwState_ = 0.0f;
    MonoDelay pre_;
    MonoDelay inAp_[4];
    static constexpr float kInLen[4] = {142.f, 107.f, 379.f, 277.f};
    // tank: [0] = left half, [1] = right half
    MonoDelay modAp_[2], del1_[2], ap2_[2], del2_[2];
    static constexpr float kModLen[2] = {672.f, 908.f}, kDel1[2] = {4453.f, 4217.f};
    static constexpr float kAp2[2] = {1800.f, 2656.f}, kDel2[2] = {3720.f, 3163.f};
    float dampState_[2] = {0, 0}, lowState1_[2] = {0, 0}, lowState2_[2] = {0, 0};
    float dampA_ = 0.0f, xoverA_ = 0.0f;
    float g1_[2] = {0.5f, 0.5f}, g2_[2] = {0.5f, 0.5f};       // mid-band segment gains
    float g1Low_[2] = {0.5f, 0.5f}, g2Low_[2] = {0.5f, 0.5f}; // low-band segment gains
    float tankOut_[2] = {0, 0};
    // early reflections
    MonoDelay er_;
    static constexpr int kEr = 8;
    float erTapL_[kEr] = {}, erTapR_[kEr] = {}, erGain_[kEr] = {};
    // modulation
    double lfoPhase_ = 0.0, lfoInc_ = 0.0;
    // smoothed controls
    LinearSmoothed size_, mix_, width_, early_, preDelay_;
    float inDiff1_ = 0.75f, inDiff2_ = 0.625f, decDiff1_ = 0.7f, decDiff2_ = 0.5f, modDepth_ = 16.0f;
    float maxSize_ = 1.5f;
};

} // namespace mfx
