// Premium FET Compressor (v0.6, type "comp_fet"). The existing "compressor" block is unchanged.
//
// FET limiting-amplifier style (1176-inspired, no proprietary code or schematics copied):
//  * fixed threshold, Input drives the signal into it, Output is make-up gain (gain staging like the hardware),
//  * feedback detection: the side-chain measures the compressor OUTPUT level. The loop is solved delay-free each
//    sample (Newton on the smoothed gain-reduction equation gr = a*gr' + (1-a)*C(x - gr)), so very fast attack
//    (20 us) stays stable at every sample rate,
//  * per-sample peak rectifier (stereo-linked), attack 20-800 us, release 50-1100 ms,
//  * program-dependent release: a "charge" state builds up during sustained reduction and slows the release
//    (short transients recover fast, long passages slowly),
//  * ratio buttons 4/8/12/20 with ratio-dependent threshold and knee, plus an "All" mode (approximation:
//    higher ratio and knee, faster internal timing, more FET distortion),
//  * FET nonlinearity: a second-order term proportional to the gain reduction (the FET's resistance depends on
//    the signal across it) and a class-A style output stage; distortion also arises naturally when fast
//    release lets the gain follow the low-frequency waveform,
//  * optional side-chain high-pass, parallel Mix, input stage soft limit (bounded output).
#pragma once

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class FetCompressor : public Effect {
public:
    enum P { kInput, kOutput, kAttack, kRelease, kRatio, kScHpf, kCharacter, kMix, kNumParams };
    enum Ratio { k4, k8, k12, k20, kAll };
    FetCompressor();
    const char* typeId() const override { return "comp_fet"; }
    const char* displayName() const override { return "FET Compressor"; }
    const char* category() const override { return "Dynamics"; }
    float gainReductionDb() const { return (float)gr_; }
    /// Static characteristic for the current ratio: reduction (dB) needed for output level yDb (dBFS peak).
    double reductionForOutput(double yDb) const;
    double thresholdDb() const { return thr_; }
    double ratio() const { return ratio_; }

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    double slopeForOutput(double yDb) const;
    double solve(double xDb, double a) const;
    Biquad scHp_, dcBlock_;
    LinearSmoothed in_, out_, mix_;
    double gr_ = 0.0, charge_ = 0.0, aA_ = 0.0, aR_ = 0.0, chargeUp_ = 0.0, chargeDown_ = 0.0;
    double ctlNeed_ = 0.0;
    double thr_ = -24.0, ratio_ = 4.0, knee_ = 8.0, relSec_ = 0.25;
    float fet_ = 0.0f, outSat_ = 0.0f, satBias_ = 0.0f, satOff_ = 0.0f, satNorm_ = 1.0f;
    bool scOn_ = false;
    int ctl_ = 0;
};

} // namespace mfx
