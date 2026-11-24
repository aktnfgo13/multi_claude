// Utility blocks: Gain/Volume/Phase (mono-safe) and Pan (stereo).
#pragma once

#include "core/ControlRateEffect.h"
#include "core/Smoothed.h"

namespace mfx {

class GainUtility : public Effect {
public:
    enum P { kGain, kVolume, kInvert, kInvertRight };
    GainUtility();
    const char* typeId() const override { return "utility"; }
    const char* displayName() const override { return "Gain / Volume"; }
    const char* category() const override { return "Utility"; }
protected:
    void onPrepare(const ProcessSpec& spec) override { g_.reset(spec.sampleRate, 0.02); }
    void onReset() override {}
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    LinearSmoothed g_;
    float sign_[2] = {1, 1};
};

class Pan : public Effect {
public:
    Pan();
    const char* typeId() const override { return "pan"; }
    const char* displayName() const override { return "Pan / Width"; }
    const char* category() const override { return "Utility"; }
    bool createsStereo() const override { return true; }
protected:
    void onPrepare(const ProcessSpec& spec) override { pan_.reset(spec.sampleRate, 0.02); width_.reset(spec.sampleRate, 0.02); }
    void onReset() override {}
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    LinearSmoothed pan_, width_;
};

/// Volume taper used by the Volume parameter (0..100 %): -inf at 0, 0 dB at 100, ~-20 dB at 50.
float volumeTaper(float percent);

} // namespace mfx
