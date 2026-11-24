#include "neural/NamAmp.h"

#include <vector>

#include "engine/RtPublisher.h"

#if MFX_WITH_NAM
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#endif

namespace mfx {

struct NamAmp::Impl {
#if MFX_WITH_NAM
    struct Model {
        std::unique_ptr<nam::DSP> dsp;
        bool rateOk = false;
        float normGain = 1.0f;
    };
    RtPublisher<Model> pub;
    std::vector<float> outBuf;
#endif
};

NamAmp::NamAmp() : impl_(std::make_unique<Impl>()) {
    params_.add({"input_db", "Input", -24, 24, 0, ParamUnit::Decibels});
    params_.add({"output_db", "Output", -40, 24, 0, ParamUnit::Decibels});
    params_.add({"normalize", "Normalize Loudness", 0, 1, 1, ParamUnit::Toggle});
    info_.status = isAvailable() ? "no model loaded" : "NAM support not built (MFX_WITH_NAM=OFF)";
}

NamAmp::~NamAmp() = default;

bool NamAmp::isAvailable() {
#if MFX_WITH_NAM
    return true;
#else
    return false;
#endif
}

void NamAmp::collectGarbage() {
#if MFX_WITH_NAM
    impl_->pub.collect();
#endif
}

bool NamAmp::setExtraState(const std::string& state, std::string* error) {
    if (state.empty()) return true;
    return loadModel(state, error);
}

bool NamAmp::loadModel(const std::string& path, std::string* error) {
#if MFX_WITH_NAM
    try {
        auto m = std::make_unique<Impl::Model>();
        m->dsp = nam::get_dsp(std::filesystem::path(path));
        if (!m->dsp) throw std::runtime_error("get_dsp returned null");
        if (m->dsp->NumInputChannels() != 1 || m->dsp->NumOutputChannels() != 1)
            throw std::runtime_error("only mono-in/mono-out models are supported");
        const double expected = m->dsp->GetExpectedSampleRate();
        m->rateOk = expected <= 0 || std::fabs(expected - spec_.sampleRate) < 1.0 || m->dsp->SupportsArbitrarySampleRate();
        m->dsp->Reset(spec_.sampleRate, spec_.maxBlockSize); // allocates + prewarms (message thread)
        info_ = {};
        info_.loaded = true;
        info_.path = path;
        info_.expectedSampleRate = expected;
        info_.hasLoudness = m->dsp->HasLoudness();
        info_.loudnessDb = info_.hasLoudness ? m->dsp->GetLoudness() : 0.0;
        info_.rateOk = m->rateOk;
        // normalise to -18 dB loudness (NAM convention used by the reference plug-in)
        m->normGain = info_.hasLoudness ? (float)dbToGainD(-18.0 - info_.loudnessDb) : 1.0f;
        info_.status = m->rateOk ? "ok" : "sample-rate mismatch: model bypassed (resampling not implemented)";
        impl_->pub.publish(std::move(m));
        return true;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        info_.status = std::string("load failed: ") + e.what();
        return false;
    }
#else
    (void)path;
    if (error) *error = "NAM support not built";
    return false;
#endif
}

void NamAmp::onPrepare(const ProcessSpec& spec) {
    in_.reset(spec.sampleRate, 0.02);
    out_.reset(spec.sampleRate, 0.02);
#if MFX_WITH_NAM
    impl_->outBuf.assign((size_t)spec.maxBlockSize, 0.0f);
    if (info_.loaded) { std::string p = info_.path; loadModel(p); impl_->pub.swapIn(); }
#endif
}

void NamAmp::onParametersChanged(bool immediate) {
    const float i = dbToGain(params_.get(kInput)), o = dbToGain(params_.get(kOutput));
    if (immediate) { in_.setImmediate(i); out_.setImmediate(o); }
    else { in_.setTarget(i); out_.setTarget(o); }
}

void NamAmp::onProcess(AudioBlock& b) {
#if MFX_WITH_NAM
    if (impl_->pub.hasPending()) impl_->pub.swapIn();
    auto* m = impl_->pub.active();
    if (!m || !m->rateOk) return; // pass-through (status reported to UI)
    float* x = b.ch[0];
    if (b.numChannels == 2)
        for (int i = 0; i < b.numSamples; ++i) x[i] = 0.5f * (x[i] + b.ch[1][i]);
    for (int i = 0; i < b.numSamples; ++i) x[i] *= in_.next();
    float* outp = impl_->outBuf.data();
    float* inPtrs[1] = {x};
    float* outPtrs[1] = {outp};
    m->dsp->process(inPtrs, outPtrs, b.numSamples);
    const float norm = params_.getInt(kNormalize) ? m->normGain : 1.0f;
    for (int i = 0; i < b.numSamples; ++i) {
        const float y = outp[i] * norm * out_.next();
        x[i] = std::isfinite(y) ? y : 0.0f;
    }
    if (b.numChannels == 2) std::copy(x, x + b.numSamples, b.ch[1]);
#else
    (void)b;
#endif
}

} // namespace mfx
