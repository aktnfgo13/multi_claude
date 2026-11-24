// Additional drive blocks built on a shared oversampled-nonlinearity skeleton.
//
//  CleanBoost  : linear gain + optional low cut, soft ceiling only above 0 dBFS (protects next stage)
//  Overdrive   : op-amp non-inverting stage with antiparallel diodes in FEEDBACK (asinh law),
//                clipping path high-passed (bass stays clean) + clean path summed, tone LPF.
//  Distortion  : op-amp gain + RC + diode pair to ground as a Wave Digital Filter (explicit Wright-omega diode pair).
//  Fuzz        : phenomenological 2-stage asymmetric fuzz with bias (starve) and gating.
//                (NOT a transistor-level model; documented as such.)
//  DdspDrive   : the exact structure used by research/ddsp (HPF -> gain -> asym sigmoid -> LPF),
//                parameters are physical so trained results can be loaded directly from JSON.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Effect.h"
#include "core/Smoothed.h"
#include "dsp/filters/Biquad.h"
#include "dsp/nonlinear/Nonlinear.h"
#include "dsp/oversampling/Oversampler.h"

namespace mfx {

/// Skeleton: base-rate pre filter -> oversampled nonlinearity -> base-rate post filter, mix, level.
/// Derived classes MUST add their parameters first, then call addCommonParams() last
/// (mix, level, oversampling are the three last parameters).
class OversampledDrive : public Effect {
public:
    int latencySamples() const override { return Oversampler::latencyFor(osIndex_); }
    static constexpr int kControlBlock = 32;

protected:
    void addCommonParams(float defaultLevelDb, int defaultOs);
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override;
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& block) override;

    // hooks
    virtual void beginBlock() {}
    /// Return true to drive mix (0..1) and level (linear) from the derived class (e.g. a knob LUT).
    virtual bool overrideMixLevel(float& /*mix*/, float& /*level*/) { return false; }
    virtual void prepareDsp(double /*fs*/, double /*fsOs*/) {}
    virtual void resetDsp() = 0;
    virtual void updateDsp(double fs, double fsOs) = 0; // recompute coefficients from smoothed knobs
    virtual float pre(float x, int ch) = 0;
    virtual float nonlinear(float u, int ch) = 0;
    virtual float post(float v, int ch) = 0;

    /// Smoothed copies of the derived class' continuous parameters (indices < firstCommon_).
    float knob(int i) const { return knobs_[i].current(); }

    int firstCommon_ = 0;
    int osIndex_ = 2;

private:
    static constexpr int kMaxKnobs = 16;
    LinearSmoothed knobs_[kMaxKnobs];
    LinearSmoothed mix_, level_;
    Oversampler os_;
    std::vector<float> tmp_;
    static constexpr int kDryLen = 64;
    float dryBuf_[kMaxChannels][kDryLen] = {};
    int dryPos_[kMaxChannels] = {0, 0};
    int fadeIn_ = 0, fadeLen_ = 1;
};

class CleanBoost : public Effect {
public:
    CleanBoost();
    const char* typeId() const override { return "clean_boost"; }
    const char* displayName() const override { return "Clean Boost"; }
    const char* category() const override { return "Drive"; }
protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override { lowCut_.reset(); bright_.reset(); }
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& block) override;
private:
    LinearSmoothed gain_;
    Biquad lowCut_, bright_;
};

class Overdrive : public OversampledDrive {
public:
    Overdrive();
    const char* typeId() const override { return "overdrive"; }
    const char* displayName() const override { return "Overdrive"; }
    const char* category() const override { return "Drive"; }
protected:
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;
private:
    Biquad clipHp_, clipLp_, toneLp_, toneShelf_;
    float gain_ = 1.0f;
    float clean_[kMaxChannels] = {0, 0};
};

class Distortion : public OversampledDrive {
public:
    Distortion();
    const char* typeId() const override { return "distortion"; }
    const char* displayName() const override { return "Distortion"; }
    const char* category() const override { return "Drive"; }
protected:
    void prepareDsp(double fs, double fsOs) override;
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;
private:
    nl::WDFDiodeClipper clip_[kMaxChannels]; // WDF chosen over the Newton ODE solver: same accuracy vs SPICE, ~4x cheaper (E-004)
    Biquad inHp_, toneA_, toneB_, outLp_;
    float gain_ = 1.0f, outScale_ = 1.0f;
    double lastFsOs_ = 0.0;
};

class Fuzz : public OversampledDrive {
public:
    Fuzz();
    const char* typeId() const override { return "fuzz"; }
    const char* displayName() const override { return "Fuzz"; }
    const char* category() const override { return "Drive"; }
protected:
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;
private:
    Biquad inHp_, interHp_, toneLp_, toneHp_;
    float gain_ = 1.0f, bias_ = 0.0f, gate_ = 0.0f, blend_ = 0.5f;
    float sag_[kMaxChannels] = {0, 0};
    float sagCoef_ = 0.0f;
};

/// Knob -> physical parameter look-up table produced by research/parameter_mapping/build_lut.py.
struct KnobLut {
    static constexpr int kMaxPoints = 16;
    static constexpr int kNumParams = 9; // hpf, gain, thr+, thr-, knee, lpf, out, mix, level
    int n = 0;
    float knob[kMaxPoints] = {};
    float value[kNumParams][kMaxPoints] = {};
    bool logDomain[kNumParams] = {};
    /// Interpolate parameter p at knob k (0..100); log-domain parameters interpolate in log space.
    float eval(int p, float k) const;
    static bool fromJsonFile(const std::string& path, KnobLut& out, std::string* error);
};

class DdspDrive : public OversampledDrive {
public:
    DdspDrive();
    ~DdspDrive() override;
    bool loadLut(const std::string& path, std::string* error = nullptr);
    bool hasLut() const { return !lutPath_.empty(); }
    std::string getExtraState() const override { return lutPath_; }
    bool setExtraState(const std::string& s, std::string* error) override { return s.empty() ? true : loadLut(s, error); }
    void collectGarbage() override;
    const char* typeId() const override { return "ddsp_drive"; }
    const char* displayName() const override { return "DDSP Drive (research)"; }
    const char* category() const override { return "Drive"; }
protected:
    void resetDsp() override;
    void updateDsp(double fs, double fsOs) override;
    float pre(float x, int ch) override;
    float nonlinear(float u, int ch) override;
    float post(float v, int ch) override;
    void beginBlock() override;
    bool overrideMixLevel(float& mix, float& level) override;
private:
    struct LutHolder;
    std::unique_ptr<LutHolder> lut_;
    std::string lutPath_;
    Biquad hp_, lp_;
    float g_ = 1.0f, vp_ = 1.0f, vn_ = 1.0f, p_ = 2.0f, out_ = 1.0f, lutMix_ = 1.0f, lutLevel_ = 1.0f;
};

} // namespace mfx
