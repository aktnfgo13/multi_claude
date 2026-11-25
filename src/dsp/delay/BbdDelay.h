// Premium BBD Analog Delay (v0.6, type "delay_bbd_premium"). Independent of the existing "delay" block.
//
// Clocked bucket-brigade model (chosen over a filter-only approximation, see docs/DSP_SPECIFICATION.md):
//  * a 4096-stage BBD passes kBuckets = 2048 samples per transit; the clock rate is r = kBuckets / T,
//    so a 20 ms delay samples at 102 kHz and an 800 ms delay at 2.6 kHz,
//  * the input is sampled at the clock instants (linear interpolation between audio samples; content above r/2
//    that the anti-aliasing filter lets through aliases - the characteristic long-delay "grit"),
//  * the output stream is reconstructed at the audio rate (Hermite over the bucket sequence) and low-passed,
//  * delay-time changes and clock modulation change r (pitch glide / vibrato), never the buffer read position,
//    so there are no clicks; the delay is exactly kBuckets clock periods.
// Around the chip: NE570-style compander (2:1 compressor before, 1:2 expander after, rectified-average detectors
// that see different signals -> program-dependent breathing on transients), 4th-order anti-aliasing and
// reconstruction filters (Tone), bucket overload soft-clip (Drive), saturating feedback amplifier, feedback DC block.
// Memory is independent of the sample rate: suited to ARM hardware.
//
// v0.7 "model" (appended): v1 = the v0.6 single-chip model above, bit-identical (old saves load v1);
// v2 (new instances) adds
//  * Chips 1/2/4 in series: buckets per pass N = 2048 * chips, clock r = N / T (same delay, N/1 times faster clock,
//    sampled-stream Nyquist r/2 = 1024 * chips / T),
//  * clock-tracking anti-alias and reconstruction filters: cut-off = min(Tone, 0.4 r) - content the bucket
//    stream cannot carry is removed before sampling instead of folding back (long delays stay dark, not aliased),
//  * compander floor match: below the compressor's detector floor the expander gain is held at the matching
//    value, so very quiet repeats are not expanded downward (v1 loses up to ~20 dB at -70 dBFS),
//  * in-loop feedback level regulation: a slow envelope of the recirculating signal lowers the loop gain above
//    -6 dBFS before the soft saturator, so self-oscillation settles smoothly instead of living in the clipper.
#pragma once

#include <cstdint>
#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class BbdDelay : public Effect {
public:
    enum P { kTime, kSync, kFeedback, kMix, kTone, kModDepth, kModRate, kCompander, kDrive, kSpread, kLevel,
             kModel, kChips, kNumParams };
    enum Model { kV1, kV2 };
    static constexpr int kBuckets = 2048;
    static constexpr double kMinTimeMs = 20.0, kMaxTimeMs = 800.0;
    BbdDelay();
    const char* typeId() const override { return "delay_bbd_premium"; }
    const char* displayName() const override { return "BBD Analog Delay"; }
    const char* category() const override { return "Delay"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    double effectiveTimeMs() const;
    /// Current clock rate of channel c (Hz).
    double clockHz(int c) const { return spec_.sampleRate / spc_[c]; }
    size_t memoryBytes() const { return sizeof(ring_); }
    int bucketsPerPass() const { return buckets_; }
    /// Current anti-alias / reconstruction cut-off (Hz).
    double filterCutoffHz() const { return faCur_; }
    bool legacyValueForMissingParam(int index, float& v) const override {
        if (index == kModel) { v = (float)kV1; return true; } // saves without "model" are v0.6 = v1
        return false;
    }

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    static constexpr int kRing = 16384, kMask = kRing - 1; // up to 4 chips (8192 buckets)
    void designFilters(double fa);
    float processChannel(int c, float x, double sPerTick);
    float ring_[2][kRing];
    int64_t count_[2] = {0, 0};
    double phase_[2] = {0, 0};   // audio samples since the last clock tick
    double spc_[2] = {1, 1};     // audio samples per clock tick (smoothed)
    float xPrev_[2] = {0, 0};
    Biquad aa1_, aa2_, rc1_, rc2_, fbHp_;
    float envC_[2] = {0, 0}, envE_[2] = {0, 0}, envA_ = 0, envR_ = 0;
    float compA_ = 1, expPow_ = 1;
    float driveIn_ = 1, driveOut_ = 1;
    float fbState_[2] = {0, 0};
    double timeA_ = 0, lfoPh_ = 0, lfoInc_ = 0, spcTarget_[2] = {1, 1}, spcBase_[2] = {1, 1};
    float modDepth_ = 0, spread_ = 0;
    int model_ = kV2, buckets_ = kBuckets, ctl_ = 0;
    double faTone_ = 4000, faCur_ = 4000;
    float envFb_[2] = {0, 0}, fbEnvA_ = 0, fbEnvR_ = 0;
    LinearSmoothed mix_, fb_, level_;
    double bpm_ = 120.0;
};

} // namespace mfx
