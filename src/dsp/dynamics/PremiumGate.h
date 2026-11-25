// Premium Gate / Downward Expander (v0.7, type "gate_premium"). The existing "noise_gate" is unchanged.
//
//  * side-chain separate from the audio path: 2nd-order high-pass (hum / rumble does not hold the gate open),
//    the audio itself is never filtered,
//  * detector: Peak (instant attack, 20 ms decay) or RMS (3 ms mean square), per channel, optional stereo link,
//  * hysteresis: open at Threshold, close at Threshold - Hysteresis, plus Hold,
//  * Gate mode: reduction 0 dB or Range; Expander mode: soft-knee (6 dB) downward expansion with Ratio below the
//    (state-dependent) threshold, limited by Range - low-level notes fade instead of being cut,
//  * gain control in dB: Attack / Release are the times for a 60 dB change (linear-in-dB ramps = constant
//    perceived fade rate), followed by a 0.2 ms corner smoother; exact unity gain when fully open,
//  * Adaptive release (option): after Hold the release follows the measured decay rate of the signal level
//    (abrupt stops close up to 4x faster, slow sustains keep the set release).
#pragma once

#include "core/Effect.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class PremiumGate : public Effect {
public:
    enum P { kThreshold, kRange, kAttack, kHold, kRelease, kRatio, kHysteresis, kMode, kDetector, kScHpf, kLink,
             kAdaptive, kNumParams };
    enum Mode { kGate, kExpander };
    enum Detector { kPeak, kRms };
    PremiumGate();
    const char* typeId() const override { return "gate_premium"; }
    const char* displayName() const override { return "Premium Gate"; }
    const char* category() const override { return "Dynamics"; }
    /// Current gain reduction (dB, >= 0) of channel c, and number of open/close transitions since reset.
    float reductionDb(int c = 0) const { return (float)red_[c]; }
    long transitions() const { return transitions_; }
    /// Static expander/gate curve: reduction (dB) for a detector level (dBFS) in the closed state.
    double staticReduction(double levelDb, bool open) const;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    Biquad scHp_;
    float env_[kMaxChannels] = {0, 0};
    double red_[kMaxChannels] = {0, 0};
    float g_[kMaxChannels] = {1, 1};
    bool open_[kMaxChannels] = {false, false};
    int hold_[kMaxChannels] = {0, 0};
    double slowLevel_[kMaxChannels] = {-120, -120}, decayRate_[kMaxChannels] = {0, 0};
    long transitions_ = 0;
    double thr_ = -55, hyst_ = 6, range_ = 60, ratio_ = 4, attStep_ = 1, relStep_ = 0.01;
    float peakRel_ = 0, rmsA_ = 0, corner_ = 0, slowA_ = 0;
    int holdSamples_ = 0, mode_ = kExpander, det_ = kPeak;
    bool link_ = true, adaptive_ = false;
};

} // namespace mfx
