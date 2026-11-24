// Noise gate, compressor, look-ahead limiter. Detection is stereo-linked.
#pragma once

#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class NoiseGate : public Effect {
public:
    enum P { kThreshold, kHysteresis, kAttack, kHold, kRelease, kRange };
    NoiseGate();
    const char* typeId() const override { return "noise_gate"; }
    const char* displayName() const override { return "Noise Gate"; }
    const char* category() const override { return "Dynamics"; }
    float currentGainDb() const { return gainToDb(gain_); }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override { env_ = 0.0f; gain_ = 0.0f; open_ = false; holdCount_ = 0; scHp_.reset(); }
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    Biquad scHp_;
    float env_ = 0, gain_ = 0, envA_ = 0, envR_ = 0, gA_ = 0, gR_ = 0;
    float openLin_ = 0, closeLin_ = 0, floor_ = 0;
    bool open_ = false;
    int holdSamples_ = 0, holdCount_ = 0;
};

/// Static gain computer (dB in -> gain dB), soft knee (Giannoulis/Massberg/Reiss 2012).
float compressorGainComputer(float xDb, float thresholdDb, float ratio, float kneeDb);

class Compressor : public Effect {
public:
    enum P { kThreshold, kRatio, kKnee, kAttack, kRelease, kMakeup, kMix };
    Compressor();
    const char* typeId() const override { return "compressor"; }
    const char* displayName() const override { return "Compressor"; }
    const char* category() const override { return "Dynamics"; }
    float gainReductionDb() const { return grDb_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override { yL_ = 0.0f; grDb_ = 0.0f; }
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    LinearSmoothed makeup_, mix_;
    float thr_ = -20, ratio_ = 4, knee_ = 6, aA_ = 0, aR_ = 0, yL_ = 0, grDb_ = 0;
};

class Limiter : public Effect {
public:
    enum P { kCeiling, kRelease, kInputGain };
    Limiter();
    const char* typeId() const override { return "limiter"; }
    const char* displayName() const override { return "Limiter"; }
    const char* category() const override { return "Dynamics"; }
    int latencySamples() const override { return look_; }
    static constexpr double kLookaheadSec = 0.0015;
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    std::vector<float> delay_[kMaxChannels];
    std::vector<float> req_;     // required gain history (look-ahead window)
    int look_ = 64, pos_ = 0, reqPos_ = 0;
    float g_ = 1.0f, ceil_ = 1.0f, aR_ = 0.0f, attackStep_ = 0.0f;
    LinearSmoothed in_;
};

} // namespace mfx
