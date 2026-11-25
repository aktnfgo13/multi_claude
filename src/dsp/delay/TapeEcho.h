// Premium Tape Echo (v0.6, type "delay_tape_premium"). Independent of the existing "delay" block.
//
// Transport model: the tape moves at speed v(t) = vBase(t) * (1 + eps(t)) (gap units per sample). The record head
// writes the signal at the current time; playback head k sits k gap units downstream. Its delay tau_k(t) is the time
// the tape needed to travel k units:  P(t) - P(t - tau_k) = k,  P = integral of v (tape position).
// tau_k is tracked per sample by one Newton step on a decimated history of P, so
//  * wow/flutter produce the physically correct pitch ratio v(t)/v(t - tau) (playback vs record speed),
//  * a Time change is a motor-speed change with inertia: the pitch glides, no clicks, no cross-fades,
//  * multi-head echoes keep their exact 1:2:3 spacing while the speed moves.
// eps(t) is split into wow (slow, ~0.5 Hz periodic + < 2 Hz random), flutter (capstan ~9 Hz + harmonic, plus
// 30-60 Hz scrape noise) and an irregular drift (Modulation). Random parts use a seeded generator (reproducible).
//
// Signal path: input + feedback -> record pre-emphasis (high shelf) -> asymmetric soft saturation (Drive, Age)
// -> tape (fractional Hermite reads) -> playback de-emphasis -> gap-loss low-pass and head bump (both follow the
// actual tape speed: longer delay = slower tape = darker echo, lower bump) -> low cut -> Tone (tilt) -> wet / feedback.
// Pre-emphasis before the saturator makes high frequencies saturate first (frequency-dependent saturation); each
// repeat passes the whole chain again (repeat degradation). No hysteresis model (no measured quality evidence yet).
#pragma once

#include <cstdint>
#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/reverb/PremiumPlate.h" // MonoDelay

namespace mfx {

class TapeEcho : public Effect {
public:
    enum P { kTime, kSync, kFeedback, kMix, kTone, kDrive, kAge, kWow, kFlutter, kModulation, kHeads, kSpread, kLevel,
             kNumParams };
    static constexpr double kMinTimeMs = 40.0, kMaxTimeMs = 1200.0;
    static constexpr int kMaxHeads = 3;
    static constexpr int kHistDecim = 8;
    TapeEcho();
    const char* typeId() const override { return "delay_tape_premium"; }
    const char* displayName() const override { return "Tape Echo"; }
    const char* category() const override { return "Delay"; }
    bool createsStereo() const override { return true; }
    bool hasTail() const override { return true; }
    void setTempo(double bpm) override { bpm_ = bpm > 0 ? bpm : bpm_; }
    double effectiveTimeMs() const;
    /// Current delay of playback head k (0-based) in samples.
    double headDelaySamples(int k) const { return tau_[k]; }
    /// Current relative speed deviation eps (for diagnostics).
    double speedDeviation() const { return eps_; }
    size_t memoryBytes() const;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    double posAt(double t) const;      // tape position at absolute time t (<= now)
    double speedAt(double t) const;    // segment slope around t
    void updateSpeedFilters(double vBase);
    float noise();
    MonoDelay tape_;
    std::vector<double> hist_;         // decimated tape position
    int histMask_ = 0;
    int64_t n_ = 0;
    double pos_ = 0.0, vBase_ = 0.0, vTarget_ = 0.0, motorA_ = 0.0, eps_ = 0.0;
    double tau_[kMaxHeads] = {0, 0, 0};
    double filterSpeed_ = 0.0;
    // modulation sources
    double wowPh_ = 0, flutPh_ = 0, wowInc_ = 0, flutInc_ = 0;
    uint32_t rng_ = 1;
    float nWow1_ = 0, nWow2_ = 0, nFl1_ = 0, nFl2_ = 0, nDr1_ = 0, nDr2_ = 0;
    float aWow_ = 0, aFl_ = 0, aFlHp_ = 0, aDr_ = 0, gWow_ = 1, gFl_ = 1, gDr_ = 1;
    float wowAmt_ = 0, flutAmt_ = 0, modAmt_ = 0;
    // record / playback
    Biquad preEmph_, deEmph_, gapLp_, headBump_, lowCut_, toneLo_, toneHi_, fbHp_;
    float satGain_ = 1, satBias_ = 0, satNorm_ = 1, satOff_ = 0, age_ = 0;
    int heads_ = 1;
    float headGain_[kMaxHeads] = {1, 0, 0}, panL_[kMaxHeads] = {1, 1, 1}, panR_[kMaxHeads] = {1, 1, 1};
    LinearSmoothed mix_, fb_, level_;
    double bpm_ = 120.0;
};

} // namespace mfx
