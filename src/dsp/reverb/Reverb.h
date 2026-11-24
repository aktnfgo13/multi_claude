// Algorithmic reverbs.
//  Room / Hall / Plate / Ambient : 8-line feedback delay network (Householder mixing, per-line
//      frequency-dependent decay set from RT60, modulated lines), input diffusion all-passes,
//      pre-delay, early-reflection taps (Room/Hall).
//  Spring-style : two dispersive all-pass chains inside feedback loops ("chirp" / drip),
//      stylised, not a physical spring model.
#pragma once

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

class Reverb : public Effect {
public:
    enum P { kType, kDecay, kPreDelay, kSize, kDamping, kLowCut, kHighCut, kModulation, kMix, kWidth };
    enum Type { Room, Hall, Plate, Spring, Ambient };
    static constexpr int kLines = 8;
    static constexpr int kDiff = 4;
    Reverb();
    const char* typeId() const override { return "reverb"; }
    const char* displayName() const override { return "Reverb"; }
    const char* category() const override { return "Reverb"; }
    bool createsStereo() const override { return true; }

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    void processFdn(float inL, float inR, float& outL, float& outR);
    void processSpring(float inL, float inR, float& outL, float& outR);

    int type_ = Hall;
    // FDN
    DelayLine lines_[kLines];  // each a single-channel line (channel 0 used)
    float len_[kLines] = {};
    float gain_[kLines] = {};
    OnePoleLP damp_[kLines];
    LFO lfo_;
    float modAmt_ = 0.0f;
    // input diffusion
    DelayLine diff_[kDiff];
    int diffLen_[kDiff] = {};
    float diffG_ = 0.7f;
    // pre-delay + early reflections
    DelayLine pre_;
    int preSamples_ = 0;
    static constexpr int kER = 6;
    int erTap_[kER] = {};
    float erGain_[kER] = {};
    float erLevel_ = 0.0f;
    // spring
    static constexpr int kSpringAP = 16;
    float apZ_[2][kSpringAP] = {};
    float apA_ = -0.6f;
    DelayLine springLine_;
    int springLen_[2] = {0, 0};
    float springFb_ = 0.7f;
    OnePoleLP springLp_;
    // in/out
    Biquad inHp_, inLp_;
    LinearSmoothed mix_, width_;
};

} // namespace mfx
