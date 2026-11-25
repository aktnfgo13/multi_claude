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
// Model M2 (v0.5, parameter "model" = 1, default for NEW instances; saves without "model" load as M1):
//   same preamp, then
//   -> long-tailed-pair phase inverter (differential soft limiting, 82k/100k plate-load imbalance,
//      NFB injected at the second grid)
//   -> 2 x coupling 0.1uF into 220k grid leaks with grid conduction (blocking / bias-shift state)
//   -> 6V6 pair, fixed bias -34 V, Koren beam-tetrode model, plate voltage = B+ -/+ half primary swing
//      (pentode knee -> power-tube saturation), class AB crossover from each tube cutting off
//   -> push-pull difference current -> output transformer (Raa 6.6k : 8 ohm, primary-inductance HPF,
//      leakage LPF) -> reactive speaker load Z(s) = Re + lossy Le + parallel-RLC resonance (current drive:
//      the load voltage follows the impedance curve)
//   -> negative feedback from the 8-ohm tap (820 / 47 ohm divider) to the phase inverter
//   -> sag: tube-rectifier B+ node (charging only through the rectifier resistance, discharged by the
//      tube currents, 20 uF reservoir); B+ sets plate/screen voltages and scales the preamp (slow RC)
//   All loops use a one-(oversampled-)sample delay; every state is bounded and finite-checked.
//   Approximations (not component-level): the phase inverter transfer curve, screen current (10 % of
//   plate), rectifier as a resistor + diode, preamp B+ scaling of the static tables.
// Not modelled (yet): rectifier ripple, cathode-bypass dynamics of the preamp, tone-stack loading by the
// volume pot, transformer core saturation, speaker cone (acoustic) -> Cabinet block.
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
    static constexpr int kModelParam = kNumOwn + 3; // after mix, level, oversampling (appended, R6)
    enum Model { kM1 = 0, kM2 = 1 };
    int model() const { return model_; }
    bool legacyValueForMissingParam(int index, float& v) const override {
        if (index == kModelParam) { v = (float)kM1; return true; } // saves from v0.4 keep the M1 sound
        return false;
    }
    /// M2 diagnostics for tests (audio thread writes, relaxed): B+ [V], min plate voltage [V].
    float lastSupplyVolts() const { return bPlus_[0]; }
    AB763Amp();
    const char* typeId() const override { return "amp_ab763"; }
    const char* displayName() const override { return "AB763 Normal (M1)"; }
    const char* category() const override { return "Amp"; }

    static constexpr double kVoltsPerFullScale = 1.0;  // 0 dBFS at the input = 1 V peak at the input jack
    static constexpr double kMixingLoss = 0.5;         // channel mixing network, approx. -6 dB
    static constexpr double kPowerClipVolts = 25.0;    // power-section knee (referred to the recovery-stage output)
    static double volumeTaper(double knob0to10);       // 1M audio taper
    static double bassTaper(double knob0to10);         // audio taper (Yeh: exp((x-1)*3.4))

    static constexpr double kSupplyIdle = 420.0;   // B+ at idle with M2 (unloaded source ~ 450 V)
    static constexpr double kRaa = 6600.0, kTurns = 28.72; // primary impedance, turns ratio (6.6k : 8)
    static constexpr double kNfb = 47.0 / (820.0 + 47.0);
    static constexpr double kPiInput = 0.14;        // recovery stage -> phase inverter network (approx.)

protected:
    void onParametersChanged(bool immediate) override;
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
    int model_ = kM2;
    float m1Power(float v) const;
    float m2Power(float v, int ch);
    float m2Forward(float vd, int ch);    // PI -> grids -> 6V6 pair -> secondary current (commits states)
    // ---- M2 state (per channel)
    Biquad gridA_, gridB_;                 // 0.1uF / 220k coupling (applied to the PI outputs)
    Biquad otHp_, otLp_;                   // transformer primary inductance / leakage
    Biquad spkRes_, spkLe_;                // speaker impedance parts
    float bias_[kMaxChannels][2] = {};     // grid-conduction charge on the coupling caps (blocking)
    float vPrim_[kMaxChannels] = {};       // primary voltage, previous sample
    float vSpk_[kMaxChannels] = {};        // speaker voltage, previous sample (NFB)
    float bPlus_[kMaxChannels] = {420.0f, 420.0f};
    float preScale_[kMaxChannels] = {1.0f, 1.0f};
    float dtOs_ = 1.0f / 96000.0f, gridDecay_ = 0.0f, preAlpha_ = 0.0f;
    // delay-free NFB loop: linearised forward transconductance (secant, bounded) and last operating point
    float g0_ = -1.0f, gEst_[kMaxChannels] = {-1.0f, -1.0f}, vdPrev_[kMaxChannels] = {}, iPrev_[kMaxChannels] = {};
    float iSecLast_[kMaxChannels] = {};
};

} // namespace mfx
