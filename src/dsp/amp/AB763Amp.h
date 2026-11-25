// "AB763 Normal" amp, model M1 (v0.4): circuit-flow model of the Normal channel of a mid-60s
// blackface 2x6V6 combo (AB763 '65 Deluxe Reverb family). Reference: published AB763 schematics; values
// below are literature values, not measurements of a unit. Effect type "amp_ab763" (new type: the legacy
// "amp" block, its Choice list and all presets are unchanged).
//
// Signal flow (per channel, nonlinear part oversampled):
//   x [dBFS] -> volts (kVoltsPerFullScale) -> V1A 12AX7 gain stage (Koren triode, 100k plate, 1.5k cathode
//   fully bypassed, grid conduction through the 68k input resistor; static load-line table)
//   -> blackface tone stack (Yeh-Smith 3rd-order analog transfer, slope 100k, 250pF / 0.1uF / 0.047uF,
//   treble 250k, bass 250k (audio taper), fixed 6.8k "mid"; factored into 1st + 2nd order sections,
//   bilinear at the oversampled rate -> sample-rate independent)
//   -> Volume (1M audio taper) -> channel mixing network (~ -6 dB) -> recovery 12AX7 stage (same model,
//   grid conduction through 220k) -> coupling 0.022uF / 1M high-pass
//   -> power section M1: phase inverter + 6V6 pair as one symmetric soft saturation (knee family p=3)
//   -> output scaling -> [down-sample] -> DC block -> Mix / Level (common drive parameters)
//
// Deferred to M2 (documented in DSP_SPECIFICATION): long-tailed-pair phase inverter, 6V6 push-pull
// model with crossover, sag (rectifier/B+), negative feedback loop, output transformer, cathode-bypass
// blocking dynamics, tone-stack loading by the volume pot.
#pragma once

#include <vector>

#include "dsp/drive/Drives.h"

namespace mfx {

/// Static 12AX7 gain stage: input voltage at the coupling node -> AC plate voltage, from the load line
/// (Koren triode model, cathode fully bypassed, grid conduction through a source resistance).
class TriodeStageTable {
public:
    struct Circuit { double bPlus = 250.0, ra = 100e3, rk = 1.5e3, rs = 68e3; };
    void build(const Circuit& c, double vinMin = -30.0, double vinMax = 30.0, int points = 6001);
    float process(float vin) const;
    double quiescentPlate() const { return vp0_; }
    double quiescentCathode() const { return vk0_; }
    /// Koren 12AX7 plate current [A].
    static double plateCurrent(double vpk, double vgk);
private:
    std::vector<float> table_;
    float vMin_ = -30.0f, invStep_ = 1.0f;
    double vp0_ = 0.0, vk0_ = 0.0;
};

/// Blackface tone stack (Yeh & Smith 2006 transfer function) as a 1st + 2nd order section cascade.
class BlackfaceToneStack {
public:
    struct Values {
        double c1 = 250e-12, c2 = 100e-9, c3 = 47e-9;
        double r1 = 250e3 /*treble*/, r2 = 250e3 /*bass*/, r3 = 6.8e3 /*fixed mid*/, r4 = 100e3 /*slope*/;
    };
    /// t = treble wiper (0..1, linear), l = bass position (0..1, already tapered).
    static void analog(const Values& v, double t, double l, double b[4], double a[4]);
    /// |H(j2pi f)| of the analog prototype (tests / documentation).
    static double analogMagnitude(const Values& v, double t, double l, double f);
    void design(const Values& v, double t, double l, double fs);
    float process(float x, int ch) { return s2_.process(s1_.process(x, ch), ch); }
    void reset() { s1_.reset(); s2_.reset(); }
    /// |H| of the designed digital cascade at f (tests).
    double digitalMagnitude(double f, double fs) const;
private:
    Biquad s1_, s2_;
};

class AB763Amp : public OversampledDrive {
public:
    enum P { kVolume, kTreble, kBass, kNumOwn };
    AB763Amp();
    const char* typeId() const override { return "amp_ab763"; }
    const char* displayName() const override { return "AB763 Normal (M1)"; }
    const char* category() const override { return "Amp"; }

    static constexpr double kVoltsPerFullScale = 1.0;  // 0 dBFS at the input = 1 V peak at the input jack
    static constexpr double kMixingLoss = 0.5;         // channel mixing network, approx. -6 dB
    static constexpr double kPowerClipVolts = 25.0;    // power-section knee (referred to the recovery-stage output)
    static double volumeTaper(double knob0to10);       // 1M audio taper
    static double bassTaper(double knob0to10);         // audio taper (Yeh: exp((x-1)*3.4))

protected:
    void prepareDsp(double fs, double fsOs) override;
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;

private:
    TriodeStageTable v1_, v2_;
    BlackfaceToneStack stack_;
    Biquad couple_, dcIn_;
    DCBlocker dcOut_;
    float volGain_ = 0.0f;
    nl::KneeWeights powerKnee_;
    bool tablesBuilt_ = false;
};

} // namespace mfx
