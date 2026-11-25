// Premium Modulation (v0.8). New types only; the existing chorus / flanger / phaser / tremolo / vibrato are unchanged.
//
// Shared building blocks (kept small on purpose):
//  * ModLfo     - sample-accurate phase (double), rate smoothed on the increment (no phase jumps), shapes
//                 Sine / Triangle / Asymmetric triangle (skew) / Smooth random (hashed, block-size independent),
//                 per-channel phase offsets, tempo sync helper.
//  * DelayLine  - existing Hermite fractional delay (chosen after the interpolator comparison in the tests).
//  * ApTpt      - first-order TPT all-pass (zero-delay, stable under per-sample coefficient ramps).
//  * Lr4        - Linkwitz-Riley 24 dB crossover (low + high sum to an all-pass: flat magnitude).
#pragma once

#include <cstdint>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

class ModLfo {
public:
    enum Shape { kSine, kTriangle, kAsym, kRandom };
    void prepare(double fs) { fs_ = fs; a_ = 1.0 - std::exp(-1.0 / (0.05 * fs)); }
    void setRate(double hz, bool immediate) { target_ = hz / fs_; if (immediate) inc_ = target_; }
    void setShape(int s, double skew = 0.5) { shape_ = s; skew_ = clampv(skew, 0.05, 0.95); }
    void reset(double phase = 0.0) { cyc_ = phase; }
    void advance() { inc_ += a_ * (target_ - inc_); cyc_ += inc_; if (cyc_ > 1048576.0) cyc_ -= 1048576.0; }
    double rateHz() const { return inc_ * fs_; }
    double increment() const { return inc_; }
    /// value in [-1, 1] at phase offset (cycles)
    double value(double offset) const {
        const double c = cyc_ + offset;
        const double p = c - std::floor(c);
        switch (shape_) {
            case kTriangle: return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
            case kAsym: return p < skew_ ? -1.0 + 2.0 * p / skew_ : 1.0 - 2.0 * (p - skew_) / (1.0 - skew_);
            case kRandom: {
                const double k = std::floor(c);
                const double a = hash((int64_t)k), b = hash((int64_t)k + 1);
                const double t = p * p * (3.0 - 2.0 * p); // smoothstep: continuous value and slope 0 at the knots
                return a + (b - a) * t;
            }
            default: return std::sin(kTwoPi * p);
        }
    }
    /// max |d value / d phase| of the shape (for depth calibration)
    double maxSlope() const {
        switch (shape_) {
            case kTriangle: return 4.0;
            case kAsym: return 2.0 / std::min(skew_, 1.0 - skew_);
            case kRandom: return 3.0; // smoothstep between knots in [-1, 1]: <= 1.5 * 2
            default: return kTwoPi;
        }
    }
private:
    static double hash(int64_t k) {
        uint64_t x = (uint64_t)k * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
        x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 31;
        return (double)(x >> 11) / 4503599627370496.0 - 1.0; // [-1, 1)
    }
    double fs_ = 48000, a_ = 0.001, inc_ = 0, target_ = 0, cyc_ = 0, skew_ = 0.5;
    int shape_ = kSine;
};

/// First-order TPT all-pass: H = (1 - s/w) / (1 + s/w) (trapezoidal), G = g / (1 + g), g = tan(pi f / fs)
struct ApTpt {
    float s[kMaxChannels] = {0, 0};
    inline float process(float x, float G, int c) {
        const float v = (x - s[c]) * G;
        const float lp = v + s[c];
        s[c] = lp + v;
        return 2.0f * lp - x;
    }
    void reset() { s[0] = s[1] = 0.0f; }
};

/// Linkwitz-Riley 4th-order crossover (two Butterworth sections each side)
struct Lr4 {
    Biquad lp1, lp2, hp1, hp2;
    void design(double fs, double fc) {
        lp1.design(FilterType::LowPass, fs, fc, 0.7071); lp2.design(FilterType::LowPass, fs, fc, 0.7071);
        hp1.design(FilterType::HighPass, fs, fc, 0.7071); hp2.design(FilterType::HighPass, fs, fc, 0.7071);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process(float x, int c, float& lo, float& hi) {
        lo = lp2.process(lp1.process(x, c), c);
        hi = hp2.process(hp1.process(x, c), c);
    }
};

// ------------------------------------------------------------------------------------------------- Chorus
class PremiumChorus : public Effect {
public:
    enum P { kRate, kDepth, kDelay, kFeedback, kTone, kMix, kSpread, kVoices, kCharacter, kDrift, kShape, kSync, kLevel, kNumParams };
    PremiumChorus();
    const char* typeId() const override { return "chorus_premium"; }
    const char* displayName() const override { return "Premium Chorus"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    DelayLine line_;
    ModLfo lfo_, drift_;
    LinearSmoothed base_, swing_, mix_, fb_, level_, driftAmt_;
    Biquad pre1_, pre2_, post1_, post2_;
    float fbState_[kMaxChannels] = {0, 0};
    float spread_ = 0.25f;
    int voices_ = 1;
    bool bbd_ = true;
    double bpm_ = 120;
};

// ------------------------------------------------------------------------------------------------- Vibrato
class PremiumVibrato : public Effect {
public:
    enum P { kRate, kDepthCents, kShape, kSkew, kDrift, kStereo, kSync, kLevel, kMix, kNumParams };
    PremiumVibrato();
    const char* typeId() const override { return "vibrato_premium"; }
    const char* displayName() const override { return "Premium Vibrato"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    int latencySamples() const override { return baseInt_; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    /// delay swing amplitude (samples) that gives the requested peak pitch deviation at the current rate
    double swingSamples() const { return swingTarget_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    void updateSwing();
    DelayLine line_;
    ModLfo lfo_, drift_;
    LinearSmoothed swing_, mix_, level_;
    double swingTarget_ = 0, cents_ = 25, rate_ = 5, bpm_ = 120;
    float stereo_ = 0, drift_amt_ = 0;
    int baseInt_ = 240;
};

// ------------------------------------------------------------------------------------------------- Phaser
class PremiumPhaser : public Effect {
public:
    enum P { kRate, kDepth, kFreqLo, kFreqHi, kFeedback, kMix, kStages, kStereo, kCharacter, kShape, kSync, kLevel, kNumParams };
    static constexpr int kMaxStages = 12;
    PremiumPhaser();
    const char* typeId() const override { return "phaser_premium"; }
    const char* displayName() const override { return "Premium Phaser"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    /// all-pass corner (Hz) of stage k on channel c at the moment
    double stageHz(int c, int k) const;
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    ApTpt ap_[kMaxStages];
    ModLfo lfo_;
    LinearSmoothed mix_, level_, fb_;
    float G0_[kMaxChannels][kMaxStages] = {}, G1_[kMaxChannels][kMaxStages] = {};
    float fbState_[kMaxChannels] = {0, 0};
    float spreadK_[kMaxStages] = {};
    double depth_ = 0.8, lo_ = 200, hi_ = 2500, stereo_ = 0.25, sat_ = 0.3, bpm_ = 120;
    double tDepth_ = 0.8, tLogLo_ = 0, tLogHi_ = 0, sA_ = 0.1; // targets, smoothed at control rate
    int stages_ = 6, ctl_ = 0;
    static constexpr int kCtl = 8;
};

// ------------------------------------------------------------------------------------------------- Uni-Vibe
class PremiumUnivibe : public Effect {
public:
    enum P { kSpeed, kIntensity, kMode, kVolume, kSync, kNumParams };
    enum Mode { kChorus, kVibrato };
    PremiumUnivibe();
    const char* typeId() const override { return "univibe_premium"; }
    const char* displayName() const override { return "Premium Uni-Vibe"; }
    const char* category() const override { return "Modulation"; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    double stageHz(int k) const { return stageHz_[k]; }
    double ldrOhms() const { return ldr_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    ApTpt ap_[4];
    ModLfo lfo_;
    LinearSmoothed level_, mixWet_;
    double lamp_ = 0, cell_ = 0, ldr_ = 1e5, intensity_ = 0.7, bpm_ = 120;
    double lampUp_ = 0, lampDn_ = 0, cellUp_ = 0, cellDn_ = 0;
    double stageHz_[4] = {};
    float G0_[4] = {}, G1_[4] = {};
    int ctl_ = 0;
    static constexpr int kCtl = 8;
};

// ------------------------------------------------------------------------------------------------- Flanger
class PremiumFlanger : public Effect {
public:
    enum P { kRate, kDepth, kManual, kFeedback, kMix, kMode, kStereo, kShape, kSync, kLevel, kNumParams };
    enum Mode { kClassic, kThroughZero };
    static constexpr double kDryDelayMs = 4.5; // fixed dry tap (through-zero reference), reported as latency
    PremiumFlanger();
    const char* typeId() const override { return "flanger_premium"; }
    const char* displayName() const override { return "Premium Flanger"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    int latencySamples() const override { return dryInt_; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    /// relative delay wet - dry (samples) of channel c at the moment
    double relativeDelay(int c) const { return rel_[c]; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    DelayLine line_, fbl_;
    ModLfo lfo_;
    LinearSmoothed manual_, depth_, mix_, fb_, level_;
    float fbState_[kMaxChannels] = {0, 0};
    double rel_[kMaxChannels] = {0, 0};
    double stereo_ = 0.25, bpm_ = 120;
    int dryInt_ = 216, mode_ = kThroughZero;
};

// ------------------------------------------------------------------------------------------------- Tremolo
class PremiumTremolo : public Effect {
public:
    enum P { kRate, kDepth, kModel, kShape, kSync, kStereo, kCrossover, kCharacter, kLevel, kNumParams };
    enum Model { kOptical, kBias, kHarmonic };
    PremiumTremolo();
    const char* typeId() const override { return "tremolo_premium"; }
    const char* displayName() const override { return "Premium Tremolo"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    ModLfo lfo_;
    LinearSmoothed depth_, level_;
    Lr4 xo_;
    Biquad dc_;
    double lamp_[kMaxChannels] = {1, 1}, lampUp_ = 0, lampDn_ = 0, bpm_ = 120;
    int model_ = kOptical, shape_ = 0;
    float character_ = 0.5f;
};

// ------------------------------------------------------------------------------------------------- Rotary
class PremiumRotary : public Effect {
public:
    enum P { kSpeed, kAccel, kHorn, kDrum, kDepth, kWidth, kMix, kDrive, kCrossover, kLevel, kNumParams };
    enum Speed { kSlow, kFast, kBrake };
    static constexpr double kHornSlow = 0.80, kHornFast = 6.80, kDrumSlow = 0.66, kDrumFast = 5.90; // rev/s
    static constexpr double kHornRadius = 0.15, kDrumRadius = 0.10, kSoundSpeed = 343.0;           // m, m/s
    PremiumRotary();
    const char* typeId() const override { return "rotary_premium"; }
    const char* displayName() const override { return "Premium Rotary"; }
    const char* category() const override { return "Modulation"; }
    bool createsStereo() const override { return true; }
    int latencySamples() const override { return baseInt_; }
    double hornHz() const { return hornW_; }
    double drumHz() const { return drumW_; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;
private:
    DelayLine horn_, drum_;
    Lr4 xo_;
    LinearSmoothed hornLv_, drumLv_, mix_, level_, depth_, width_;
    double hornW_ = kHornSlow, drumW_ = kDrumSlow, hornTh_ = 0, drumTh_ = 0.37;
    double hornT_ = kHornSlow, drumT_ = kDrumSlow, accel_ = 1.0;
    float drive_ = 1.0f, driveNorm_ = 1.0f;
    int baseInt_ = 96;
};

} // namespace mfx
