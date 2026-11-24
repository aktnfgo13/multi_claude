// Helper base: every continuous parameter is smoothed (30 ms ramps) and the derived class
// recomputes its coefficients at control rate (every 32 samples) only while something moves.
#pragma once

#include "core/Effect.h"
#include "core/Smoothed.h"

namespace mfx {

class ControlRateEffect : public Effect {
public:
    static constexpr int kControlBlock = 32;

protected:
    /// Smoothed parameter value (plain units). Discrete params are not smoothed.
    float p(int i) const { return sm_[i].current(); }
    int pi(int i) const { return (int)std::lround(sm_[i].current()); }

    virtual void prepareDsp(const ProcessSpec& spec) = 0;
    virtual void resetDsp() = 0;
    virtual void updateDsp() = 0;
    virtual void processSub(AudioBlock& sub) = 0;
    virtual double smoothingSeconds() const { return 0.03; }

    void onPrepare(const ProcessSpec& spec) override {
        for (auto& s : sm_) s.reset(spec.sampleRate, smoothingSeconds());
        prepareDsp(spec);
    }
    void onReset() override { resetDsp(); }
    void onParametersChanged(bool immediate) override {
        for (int i = 0; i < params_.size(); ++i) {
            const float v = params_.get(i);
            if (immediate || params_.info(i).isDiscrete()) sm_[i].setImmediate(v);
            else sm_[i].setTarget(v);
        }
        updateDsp();
    }
    void onProcess(AudioBlock& block) override {
        for (int start = 0; start < block.numSamples; start += kControlBlock) {
            const int L = std::min(kControlBlock, block.numSamples - start);
            bool moving = false;
            for (int i = 0; i < params_.size(); ++i)
                if (sm_[i].isSmoothing()) { sm_[i].skip(L); moving = true; }
            if (moving) updateDsp();
            AudioBlock sub = block.subBlock(start, L);
            processSub(sub);
        }
    }

private:
    LinearSmoothed sm_[ParameterSet::kMaxParams];
};

} // namespace mfx
