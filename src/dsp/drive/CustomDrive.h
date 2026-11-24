// Custom Drive — the reference overdrive of the product.
//
// Not a clone of any commercial pedal. Each knob maps to ONE physically meaningful quantity
// (see docs/DSP_SPECIFICATION.md §CustomDrive and CustomDriveMapping below):
//
//   Drive        small-signal gain into the clipping stage          G   = 0..48 dB
//   Headroom     clipping threshold (output ceiling, onset level)   Vth = -24..0 dBFS
//   Tone         post-clip tilt (+/-6 dB @800 Hz) + fizz low-pass   2.5..14 kHz
//   Tight        pre-clip high-pass (bass into clipper)             30..900 Hz, 12 dB/oct
//   Crunch       knee hardness p of the clipping curve (+2nd stage) p = 1.5..10
//   Grain        asymmetry + pre/de-emphasis around the clipper     a = 0..0.45, E = 0..15 dB
//   Mid Char.    pre-clip (and 30% post) mid peak/scoop             +/-10 dB, 500..990 Hz
//   Mix          parallel clean (latency aligned)
//   Level        output level
//   Oversampling 1x/2x/4x/8x (nonlinear section only)
//
// Signal flow (per channel):
//   x -> HPF1 20Hz -> TightHPF2 -> MidPeak(pre) -> [OS up] -> *G -> EmphasisShelf(+E)
//     -> bias shift (grid-conduction style dynamic state) -> asymmetric algebraic sigmoid (p, Vp, Vn)
//     -> 2nd soft stage (crunch) -> DeEmphasisShelf(-E) -> [OS down] -> DC block
//     -> MidPeak(post) -> Tilt -> FizzLPF -> *makeup -> mix(dry delayed) -> *level
#pragma once

#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/oversampling/Oversampler.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

/// Knob (0..100) -> physical parameter mapping. Pure function, mirrored in Python (research/common/mfx_dsp.py).
struct CustomDriveKnobs {
    float drive = 25, headroom = 50, tone = 50, tight = 50, crunch = 50, grain = 50, mid = 50;
};

struct CustomDrivePhysical {
    double gainDb, vth, p, vp, vn, emphasisDb, emphasisHz;
    double tightHz, tightQ;
    double midPreHz, midPreDb, midPreQ, midPostDb;
    double tiltDb, tiltHz, fizzHz;
    double secondStage;   // c2 >= 1
    double biasDepth;     // in units of Vth
    double makeupDb;
};

CustomDrivePhysical customDriveMapping(const CustomDriveKnobs& k);

class CustomDrive : public Effect {
public:
    enum P { kDrive, kHeadroom, kTone, kTight, kCrunch, kGrain, kMid, kMix, kLevel, kOversampling, kNumParams };
    CustomDrive();
    const char* typeId() const override { return "custom_drive"; }
    const char* displayName() const override { return "Custom Drive"; }
    const char* category() const override { return "Drive"; }
    int latencySamples() const override { return Oversampler::latencyFor(osIndex_); }

    /// Exposed for tests / documentation.
    const CustomDrivePhysical& physical() const { return phys_; }

    static constexpr int kControlBlock = 32;
    static constexpr double kBiasChargeSec = 0.002;
    static constexpr double kBiasReleaseSec = 0.150;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& block) override;

private:
    void updateCoefficients();
    void setOversampling(int idx);

    CustomDrivePhysical phys_{};
    LinearSmoothed knob_[7];
    LinearSmoothed mix_, level_;
    Biquad dcIn_, tightHp_, midPre_, emph_, deemph_, midPost_, tiltLo_, tiltHi_, fizz_;
    DCBlocker dcOut_;
    Oversampler os_;
    static constexpr int kDryLen = 64; // > max OS latency (41)
    float dryBuf_[kMaxChannels][kDryLen] = {};
    int dryPos_[kMaxChannels] = {0, 0};
    int osIndex_ = 1;
    float bias_[kMaxChannels] = {0, 0};
    float biasCharge_ = 0, biasRelease_ = 0;
    float gLin_ = 1, vp_ = 1, vn_ = 1, p_ = 2, c2_ = 1, vth_ = 1, makeup_ = 1, depth_ = 0;
    int fadeIn_ = 0, fadeLen_ = 1;
    std::vector<float> tmp_;
};

} // namespace mfx
