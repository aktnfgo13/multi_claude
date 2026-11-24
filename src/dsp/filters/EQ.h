// EQ blocks: single filter, 7-band parametric EQ, 10-band graphic EQ.
#pragma once

#include "core/ControlRateEffect.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class FilterBlock : public ControlRateEffect {
public:
    enum P { kType, kFreq, kQ, kGain };
    FilterBlock();
    const char* typeId() const override { return "filter"; }
    const char* displayName() const override { return "Filter"; }
    const char* category() const override { return "EQ"; }
protected:
    void prepareDsp(const ProcessSpec&) override {}
    void resetDsp() override { f_.reset(); f2_.reset(); }
    void updateDsp() override;
    void processSub(AudioBlock& b) override;
private:
    Biquad f_, f2_;
    bool cascade_ = false;
};

class ParametricEQ : public ControlRateEffect {
public:
    enum P { kLowCut, kLowF, kLowG, kP1F, kP1G, kP1Q, kP2F, kP2G, kP2Q, kP3F, kP3G, kP3Q, kHighF, kHighG, kHighCut, kOut };
    ParametricEQ();
    const char* typeId() const override { return "parametric_eq"; }
    const char* displayName() const override { return "Parametric EQ"; }
    const char* category() const override { return "EQ"; }
    /// Combined magnitude response (dB) of the current design, for UI / tests.
    double responseDb(double f) const;
protected:
    void prepareDsp(const ProcessSpec&) override {}
    void resetDsp() override { for (auto& b : bands_) b.reset(); }
    void updateDsp() override;
    void processSub(AudioBlock& b) override;
private:
    static constexpr int kBands = 7;
    Biquad bands_[kBands];
    float out_ = 1.0f;
};

class GraphicEQ : public ControlRateEffect {
public:
    static constexpr int kNumBands = 10;
    static const double kCentres[kNumBands];
    GraphicEQ();
    const char* typeId() const override { return "graphic_eq"; }
    const char* displayName() const override { return "Graphic EQ"; }
    const char* category() const override { return "EQ"; }
protected:
    void prepareDsp(const ProcessSpec&) override {}
    void resetDsp() override { for (auto& b : bands_) b.reset(); }
    void updateDsp() override;
    void processSub(AudioBlock& b) override;
private:
    Biquad bands_[kNumBands];
    float out_ = 1.0f;
};

} // namespace mfx
