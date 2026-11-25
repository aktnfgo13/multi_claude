// Cabinet: user IR (zero-latency partitioned convolution) or built-in filter-based cabinet simulation.
// IR loading/resampling happens on the message thread; the kernel is handed to the audio thread
// through an atomic pointer and swapped behind a short fade (no allocation / locks on the audio thread).
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/ControlRateEffect.h"
#include "dsp/cabinet/Convolver.h"
#include "dsp/filters/Biquad.h"

namespace mfx {

class Cabinet : public ControlRateEffect {
public:
    enum P { kSource, kSimModel, kLowCut, kHighCut, kIrLevel, kMix, kNormalize };
    static constexpr int kMaxIrSeconds = 2;
    Cabinet();
    ~Cabinet() override;
    const char* typeId() const override { return "cabinet"; }
    const char* displayName() const override { return "Cabinet / IR"; }
    const char* category() const override { return "Cabinet"; }

    /// Message thread. IR in any sample rate (first channel used). Resampled to engine rate.
    bool loadIR(const std::vector<float>& ir, double irSampleRate, const std::string& sourceName = {});
    bool loadIRFile(const std::string& path, std::string* error = nullptr);
    bool hasIR() const { return !rawIr_.empty(); }
    const std::string& irName() const { return irName_; }

    /// IR reference: "mfx-ir:1" header + path / file name / FNV-1a 64 of the samples / sample rate / frames.
    /// A plain path (v0.1/v0.2 presets) is still accepted. IR samples are never stored in the preset.
    std::string getExtraState() const override;
    bool setExtraState(const std::string& state, std::string* error) override;
    bool setExtraStateWithContext(const std::string& state, const StateContext& ctx, std::string* error) override;
    /// FNV-1a 64-bit hash of the loaded IR samples (as read from the file, before resampling); 0 = none.
    uint64_t irHash() const { return irHash_; }
    static uint64_t hashSamples(const std::vector<float>& x);
    void collectGarbage() override;

protected:
    void prepareDsp(const ProcessSpec& spec) override;
    void resetDsp() override;
    void updateDsp() override;
    void processSub(AudioBlock& b) override;

private:
    void publishKernel();
    std::vector<float> rawIr_;
    double rawSr_ = 48000.0;
    std::string irPath_, irName_;
    uint64_t irHash_ = 0;
    std::string unresolvedState_; // reference that could not be resolved: kept so re-saving does not lose it
    bool prepared_ = false;

    Convolver conv_[kMaxChannels];
    ConvolutionKernel* active_ = nullptr;
    std::atomic<ConvolutionKernel*> pending_{nullptr};
    static constexpr int kRetired = 8;
    std::atomic<ConvolutionKernel*> retired_[kRetired];
    int fadeState_ = 0; // 0 = steady, 1 = fading out before swap, 2 = fading in
    float fade_ = 1.0f, fadeStep_ = 0.01f;

    Biquad lowCut_, highCut_;
    Biquad sim_[6];
    int simCount_ = 0;
    float level_ = 1.0f, mix_ = 1.0f;
};

} // namespace mfx
