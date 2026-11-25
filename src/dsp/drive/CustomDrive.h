// Custom Drive — the reference overdrive of the product.  (mapping v2, see docs/PARAMETER_SPECIFICATION.md)
//
// Not a clone of any commercial pedal. Each knob maps to ONE physically meaningful quantity
// (see docs/PARAMETER_SPECIFICATION.md, mapping v2 — v1 kept as customDriveMappingV1 for studies):
//
//   Drive        small-signal gain into the clipping stage          G   = -6..+30 dB (15 % output compensated)
//   Headroom     clipping threshold (onset level, ceiling)          Vth = -18..0 dBFS (50 % output compensated)
//   Tone         post-clip tilt (+/-6 dB @800 Hz) + fizz low-pass   2.5..14 kHz
//   Tight        pre-clip high-pass (bass into clipper)             30..900 Hz, 12 dB/oct
//   Crunch       knee hardness (analytic family p=2..6), residual slope kappa, 2nd stage
//   Grain        operating-point offset beta (even harmonics) + pre/de-emphasis 0..5 dB + bias depth
//   Mid Char.    pre-clip (and 30% post) mid peak/scoop             +/-10 dB, 500..990 Hz
//   Mix          parallel clean (latency aligned)
//   Level        output level
//   Oversampling 1x/2x/4x/8x (nonlinear section only)
//
// Signal flow (per channel):
//   x -> HPF1 20Hz -> TightHPF2 -> MidPeak(pre) -> [OS up] -> *G -> PreClipLPF 7.2k -> EmphasisShelf(+E)
//     -> bias shift (grid-conduction style dynamic state) -> knee(z+beta) + kappa*asinh(z+beta)
//     -> 2nd soft stage (crunch) -> DeEmphasisShelf(-E) -> [OS down] -> DC block
//     -> MidPeak(post) -> Tilt -> FizzLPF -> *makeup -> mix(dry delayed) -> *level
#pragma once

#include <atomic>
#include <cmath>
#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/nonlinear/Nonlinear.h"
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
    double kappa;         // v2: residual (logarithmic) slope above the knee -> dynamics are kept in clipping
    double beta;          // v2: asymmetry as an operating-point offset (in units of Vth), analytic
};

/// Mapping version 2 (2026-11-24, integration review). v1 is kept for reference / before-after studies.
CustomDrivePhysical customDriveMappingV1(const CustomDriveKnobs& k);

CustomDrivePhysical customDriveMapping(const CustomDriveKnobs& k);

class CustomDrive : public Effect {
public:
    enum P { kDrive, kHeadroom, kTone, kTight, kCrunch, kGrain, kMid, kMix, kLevel, kOversampling, kModel, kAdaaComp, kNumParams };
    /// Sound model (appended parameter, R6): 0 = v1 (v0.1 mapping + curve, bit-exact),
    /// 1 = v2 (default, v0.2 sound), 2 = v2 + 1st-order ADAA on both clipping stages (v0.4).
    enum Model { kModelV1 = 0, kModelV2 = 1, kModelV2Adaa = 2 };
    int model() const { return model_; }
    CustomDrive();
    const char* typeId() const override { return "custom_drive"; }
    const char* displayName() const override { return "Custom Drive"; }
    const char* category() const override { return "Drive"; }
    int latencySamples() const override { return Oversampler::latencyFor(osIndex_) + extraLatencyInt(); }
    /// v0.5: "adaa_comp" (appended) — with model v2 + ADAA: 3-tap symmetric FIR [-1/4, 3/2, -1/4] at the
    /// oversampled rate that undoes the ADAA small-signal high-frequency droop, and dry/wet alignment of the
    /// extra delay (wet padded to a whole base-rate sample, dry delayed by the same integer amount). New instances: on;
    /// saves without the parameter: off (v0.4 sound).
    bool legacyValueForMissingParam(int index, float& v) const override {
        if (index == kAdaaComp) { v = 0.0f; return true; }
        return false;
    }
    bool adaaCompensated() const { return model_ == kModelV2Adaa && adaaComp_; }

    /// Exposed for tests / documentation.
    const CustomDrivePhysical& physical() const { return phys_; }

    /// Gain-staging diagnostics (off by default; RT-safe relaxed atomics, written once per process()).
    /// Peaks are block maxima since the last readAndReset(); levels in dBFS / dB.
    struct Diagnostics {
        float inPeakDb, inRmsDb, clipInPeakOverVthDb, clipOutPeakDb, gainReductionDb;
        float appliedDriveDb, appliedVthDb, appliedMakeupDb, appliedKappa, appliedKnee;
    };
    void setDiagnosticsEnabled(bool on) noexcept { diagOn_.store(on, std::memory_order_relaxed); }
    bool diagnosticsEnabled() const noexcept { return diagOn_.load(std::memory_order_relaxed); }
    Diagnostics readDiagnostics(bool reset = true) noexcept;

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
    Biquad dcIn_, tightHp_, midPre_, emph_, deemph_, preLp_, midPost_, tiltLo_, tiltHi_, fizz_;
    DCBlocker dcOut_;
    Oversampler os_;
    static constexpr int kDryLen = 64; // > max OS latency (41)
    float dryBuf_[kMaxChannels][kDryLen] = {};
    int dryPos_[kMaxChannels] = {0, 0};
    int osIndex_ = 1;
    float bias_[kMaxChannels] = {0, 0};
    float biasCharge_ = 0, biasRelease_ = 0;
    float gLin_ = 1, vp_ = 1, vn_ = 1, p_ = 2, c2_ = 1, vth_ = 1, makeup_ = 1, depth_ = 0, kappa_ = 0, invKappa_ = 1;
    float beta_ = 0, f0_ = 0, slope0Inv_ = 1;
    nl::KneeWeights knee_;
    std::atomic<bool> diagOn_{false};
    std::atomic<float> dInPeak_{0}, dInSq_{0}, dInN_{0}, dClipIn_{0}, dClipOut_{0}, dLin_{0}, dOut_{0};
    std::atomic<float> dApplied_[5] = {};   // gLin, vth, makeup, kappa, knee p (audio thread writes)
    int fadeIn_ = 0, fadeLen_ = 1;
    std::vector<float> tmp_;
    int model_ = kModelV2;
    bool adaaComp_ = true;
    float fir1_[kMaxChannels] = {0, 0}, fir2_[kMaxChannels] = {0, 0};
    // ADAA + FIR add 2 oversampled samples; the wet path is padded to a whole base-rate sample so the dry
    // path stays an integer delay (no interpolation loss): pad = F*ceil(2/F) - 2 oversampled samples.
    static constexpr int kPadLen = 8;
    float pad_[kMaxChannels][kPadLen] = {};
    int padPos_[kMaxChannels] = {0, 0};
    int extraLatencyInt() const { const int F = 1 << osIndex_; return adaaCompensated() ? (2 + F - 1) / F : 0; }
    int padSamples() const { const int F = 1 << osIndex_; return F * ((2 + F - 1) / F) - 2; }
    // ADAA state (previous stage inputs and antiderivative values), per channel, double precision
    double adZ_[kMaxChannels] = {0, 0}, adG_[kMaxChannels] = {0, 0};
    double adW_[kMaxChannels] = {0, 0}, adG2_[kMaxChannels] = {0, 0};
    bool adInit_[kMaxChannels] = {false, false};
    void processNonlinearV1(float* u, int n, int c);
    void processNonlinearV2(float* u, int n, int c, bool diag, float& dZ, float& dY, float& dLin);
    void processNonlinearAdaa(float* u, int n, int c);
};

} // namespace mfx
