// Delay: Digital (click-free time changes by cross-fading taps), Analog-style (BBD: dark, saturating
// loop, slight clock modulation), Tape-style (wow/flutter, record saturation, head-bump, slewing time).
// Routing: Mono / Stereo (offset right time) / Ping-Pong. Tempo sync, tap tempo through setTempo().
#pragma once

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

class Delay : public Effect {
public:
    enum P { kMode, kRouting, kTime, kSync, kFeedback, kMix, kLowCut, kHighCut, kModDepth, kModRate, kAge, kOffset };
    enum Mode { Digital, Analog, Tape };
    enum Routing { Mono, Stereo, PingPong };
    static constexpr double kMaxSeconds = 2.5;
    Delay();
    const char* typeId() const override { return "delay"; }
    const char* displayName() const override { return "Delay"; }
    const char* category() const override { return "Delay"; }
    bool createsStereo() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    /// Effective delay time (ms) for the left/main tap after sync resolution.
    double effectiveTimeMs() const;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    float readTap(int ch, float target, float& cur, float& prev, float& xf, int& xfLeft, float modSamples);
    DelayLine line_;
    LFO wow_, flutter_, mod_;
    Biquad loopHp_[1], loopLp_[1], headBump_;
    LinearSmoothed mix_, fb_;
    float cur_[kMaxChannels] = {0, 0}, prev_[kMaxChannels] = {0, 0}, xf_[kMaxChannels] = {1, 1};
    int xfLeft_[kMaxChannels] = {0, 0};
    int xfLen_ = 2400;
    float fbState_[kMaxChannels] = {0, 0};
    double bpm_ = 120.0;
    int mode_ = Digital, routing_ = Stereo;
    float age_ = 0.3f, modDepth_ = 0.0f, slew_ = 0.0f;
};

} // namespace mfx
