// NAM Amp block: loads a .nam model with the official NeuralAmpModelerCore (MIT) and runs it in
// real time. Built only when MFX_WITH_NAM=ON; otherwise the block reports "NAM support not built"
// and passes audio through unchanged (it is NOT presented as a working amp in that case).
//
// - Model loading, prewarm, Reset(sampleRate, maxBlock) happen on the message thread.
// - The prepared model is swapped in through RtPublisher (no allocation / free on the audio thread).
// - Sample-rate mismatch: models that do not support arbitrary rates are bypassed (status reported);
//   real-time resampling is a documented TODO.
#pragma once

#include <memory>
#include <string>

#include "core/Effect.h"
#include "core/Smoothed.h"

namespace mfx {

struct NamModelInfo {
    bool loaded = false;
    std::string path, status;
    double expectedSampleRate = 0.0;
    bool hasLoudness = false;
    double loudnessDb = 0.0;
    bool rateOk = false;
};

class NamAmp : public Effect {
public:
    enum P { kInput, kOutput, kNormalize };
    NamAmp();
    ~NamAmp() override;
    const char* typeId() const override { return "nam_amp"; }
    const char* displayName() const override { return "NAM Amp"; }
    const char* category() const override { return "Amp"; }

    static bool isAvailable();
    bool loadModel(const std::string& path, std::string* error = nullptr);
    const NamModelInfo& info() const { return info_; }

    std::string getExtraState() const override { return info_.path; }
    bool setExtraState(const std::string& state, std::string* error) override;
    void collectGarbage() override;

protected:
    void onPrepare(const ProcessSpec& spec) override;
    void onReset() override {}
    void onParametersChanged(bool immediate) override;
    void onProcess(AudioBlock& b) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    NamModelInfo info_;
    LinearSmoothed in_, out_;
};

} // namespace mfx
