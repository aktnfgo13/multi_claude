// Tube guitar amplifier simulation (block-level physical approximation).
//
// Structure (each element individually parameterised per model, see AmpModelDef):
//   Input stage (grid stopper LPF, input HPF, bright cap)
//   N preamp triode stages:  gain -> coupling-cap HPF -> cathode-bypass low shelf -> Miller LPF
//                            -> dynamic grid-conduction bias shift -> asymmetric triode curve (inverting)
//   Tone stack after stage K (simplified 3-band: low shelf / mid peak with model scoop / high shelf)
//   Phase inverter + push-pull power stage: symmetric saturation with supply sag (rectifier/filter cap)
//   Negative feedback approximated by Presence (HF) / Depth (LF resonance) shelving into the power stage
//   Output transformer (LF/HF band limits) + speaker impedance interaction (bass resonance, rising highs)
//
// SIMPLIFICATIONS (documented in docs/DSP_SPECIFICATION.md):
//   - triodes use a static asymmetric curve + one dynamic bias state, not Koren/Child-Langmuir equations
//   - the tone stack is not the passive TMB network (no control interaction)
//   - NFB is not solved as a closed loop; its frequency-dependent effect is approximated by shelving filters
//   - no measured reference amplifier: model names describe a character, not a specific product
#pragma once

#include "dsp/drive/Drives.h"
#include "dsp/utility/DelayLine.h"

namespace mfx {

struct AmpModelDef {
    const char* name;
    int stages;
    double stageGainDb[4];
    double couplingHz[4];
    double cathodeShelfDb[4];
    double cathodeHz[4];
    double millerHz;
    double gridLimit;   // positive (grid conduction) saturation level
    double cutoffLimit; // negative (cutoff) saturation level
    int toneStackAfter; // stage index (0-based) after which the tone stack sits
    double midHz, midScoopDb;
    double powerHeadroom;
    double sagMax;
    double brightHz;
    double outputTrimDb; // loudness calibration (synthetic DI, default knobs) - not a measurement of a real amp
};

const AmpModelDef& ampModel(int index);

class AmpSim : public OversampledDrive {
public:
    enum P { kModel, kGain, kBass, kMid, kTreble, kPresence, kDepth, kMaster, kSag, kBright };
    static constexpr int kNumModels = 4;
    AmpSim();
    const char* typeId() const override { return "amp"; }
    const char* displayName() const override { return "Amp"; }
    const char* category() const override { return "Amp"; }

protected:
    void prepareDsp(double fs, double fsOs) override;
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;

private:
    static constexpr int kMaxStages = 4;
    int model_ = 1;
    const AmpModelDef* def_ = nullptr;
    Biquad inHp_, inLp_, bright_;
    Biquad coup_[kMaxStages], cath_[kMaxStages], miller_[kMaxStages];
    Biquad tsLow_, tsMid_, tsHigh_, presence_, depth_;
    Biquad otHp_, otLp_, spkRes_, spkHigh_;
    float stageGain_[kMaxStages] = {1, 1, 1, 1};
    float bias_[kMaxChannels][kMaxStages] = {};
    float sagEnv_[kMaxChannels] = {0, 0};
    float biasCharge_ = 0, biasRelease_ = 0, sagA_ = 0, sagR_ = 0;
    float master_ = 1, sagDepth_ = 0, powerA_ = 1, outNorm_ = 1;
};

} // namespace mfx
