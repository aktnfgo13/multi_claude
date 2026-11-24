#include "dsp/dynamics/Dynamics.h"

namespace mfx {

// ---------------------------------------------------------------- NoiseGate
NoiseGate::NoiseGate() {
    params_.add({"threshold_db", "Threshold", -96, 0, -60, ParamUnit::Decibels});
    params_.add({"hysteresis_db", "Hysteresis", 0, 20, 6, ParamUnit::Decibels});
    params_.add({"attack_ms", "Attack", 0.1f, 50, 1, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"hold_ms", "Hold", 0, 500, 20, ParamUnit::Milliseconds});
    params_.add({"release_ms", "Release", 5, 2000, 120, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"range_db", "Range", -96, 0, -80, ParamUnit::Decibels});
}

void NoiseGate::onPrepare(const ProcessSpec& spec) {
    scHp_.design(FilterType::HighPass, spec.sampleRate, 60.0, 0.7071); // ignore DC / rumble in detector
}

void NoiseGate::onParametersChanged(bool) {
    const double fs = spec_.sampleRate;
    openLin_ = dbToGain(params_.get(kThreshold));
    closeLin_ = dbToGain(params_.get(kThreshold) - params_.get(kHysteresis));
    floor_ = dbToGain(params_.get(kRange));
    envA_ = onePoleCoeff(0.0002, fs);
    envR_ = onePoleCoeff(0.02, fs);
    gA_ = onePoleCoeff(params_.get(kAttack) * 0.001, fs);
    gR_ = onePoleCoeff(params_.get(kRelease) * 0.001, fs);
    holdSamples_ = (int)(params_.get(kHold) * 0.001 * fs);
}

void NoiseGate::onProcess(AudioBlock& b) {
    for (int i = 0; i < b.numSamples; ++i) {
        float a = 0.0f;
        for (int c = 0; c < b.numChannels; ++c) a = std::max(a, std::fabs(scHp_.process(b.ch[c][i], c)));
        env_ = a > env_ ? a + envA_ * (env_ - a) : a + envR_ * (env_ - a);
        if (!open_ && env_ > openLin_) { open_ = true; holdCount_ = holdSamples_; }
        else if (open_) {
            if (env_ > closeLin_) holdCount_ = holdSamples_;
            else if (holdCount_ > 0) --holdCount_;
            else open_ = false;
        }
        const float target = open_ ? 1.0f : floor_;
        gain_ = target + (target > gain_ ? gA_ : gR_) * (gain_ - target);
        for (int c = 0; c < b.numChannels; ++c) b.ch[c][i] *= gain_;
    }
}

// ---------------------------------------------------------------- Compressor
float compressorGainComputer(float x, float T, float R, float W) {
    if (2.0f * (x - T) < -W) return 0.0f;
    if (W > 0.0f && 2.0f * std::fabs(x - T) <= W) {
        const float d = x - T + W * 0.5f;
        return (1.0f / R - 1.0f) * d * d / (2.0f * W);
    }
    return (T + (x - T) / R) - x;
}

Compressor::Compressor() {
    params_.add({"threshold_db", "Threshold", -60, 0, -20, ParamUnit::Decibels});
    params_.add({"ratio", "Ratio", 1, 20, 4, ParamUnit::Ratio, ParamScale::Log});
    params_.add({"knee_db", "Knee", 0, 24, 6, ParamUnit::Decibels});
    params_.add({"attack_ms", "Attack", 0.1f, 200, 10, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"release_ms", "Release", 10, 2000, 150, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"makeup_db", "Makeup", 0, 30, 0, ParamUnit::Decibels});
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
}

void Compressor::onPrepare(const ProcessSpec& spec) {
    makeup_.reset(spec.sampleRate, 0.02);
    mix_.reset(spec.sampleRate, 0.02);
}

void Compressor::onParametersChanged(bool immediate) {
    thr_ = params_.get(kThreshold);
    ratio_ = params_.get(kRatio);
    knee_ = params_.get(kKnee);
    aA_ = onePoleCoeff(params_.get(kAttack) * 0.001, spec_.sampleRate);
    aR_ = onePoleCoeff(params_.get(kRelease) * 0.001, spec_.sampleRate);
    const float mk = dbToGain(params_.get(kMakeup)), mx = params_.get(kMix) / 100.0f;
    if (immediate) { makeup_.setImmediate(mk); mix_.setImmediate(mx); }
    else { makeup_.setTarget(mk); mix_.setTarget(mx); }
}

void Compressor::onProcess(AudioBlock& b) {
    for (int i = 0; i < b.numSamples; ++i) {
        float a = 0.0f;
        for (int c = 0; c < b.numChannels; ++c) a = std::max(a, std::fabs(b.ch[c][i]));
        const float xDb = gainToDb(a, -120.0f);
        const float xL = -compressorGainComputer(xDb, thr_, ratio_, knee_); // >= 0 : required reduction
        // smooth branching peak detector in the log domain
        yL_ = xL > yL_ ? xL + aA_ * (yL_ - xL) : xL + aR_ * (yL_ - xL);
        grDb_ = yL_;
        const float g = dbToGain(-yL_) * makeup_.next();
        const float m = mix_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = b.ch[c][i];
            b.ch[c][i] = x + m * (x * g - x);
        }
    }
}

// ---------------------------------------------------------------- Limiter
Limiter::Limiter() {
    params_.add({"ceiling_db", "Ceiling", -24, 0, -0.3f, ParamUnit::Decibels});
    params_.add({"release_ms", "Release", 5, 1000, 80, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"input_db", "Input", -12, 24, 0, ParamUnit::Decibels});
}

void Limiter::onPrepare(const ProcessSpec& spec) {
    look_ = std::max(1, (int)std::lround(kLookaheadSec * spec.sampleRate));
    for (auto& d : delay_) d.assign((size_t)look_, 0.0f); // ring of length L -> delay of exactly L samples
    req_.assign((size_t)look_ + 1, 1.0f);
    in_.reset(spec.sampleRate, 0.02);
}

void Limiter::onReset() {
    for (auto& d : delay_) std::fill(d.begin(), d.end(), 0.0f);
    std::fill(req_.begin(), req_.end(), 1.0f);
    pos_ = reqPos_ = 0;
    g_ = 1.0f;
}

void Limiter::onParametersChanged(bool immediate) {
    ceil_ = dbToGain(params_.get(kCeiling));
    aR_ = onePoleCoeff(params_.get(kRelease) * 0.001, spec_.sampleRate);
    attackStep_ = onePoleCoeff((double)look_ / 4.0 / spec_.sampleRate, spec_.sampleRate);
    const float ig = dbToGain(params_.get(kInputGain));
    if (immediate) in_.setImmediate(ig); else in_.setTarget(ig);
}

void Limiter::onProcess(AudioBlock& b) {
    const int N = look_ + 1;
    for (int i = 0; i < b.numSamples; ++i) {
        const float ig = in_.next();
        float a = 0.0f;
        for (int c = 0; c < b.numChannels; ++c) a = std::max(a, std::fabs(b.ch[c][i] * ig));
        req_[(size_t)reqPos_] = a > ceil_ ? ceil_ / a : 1.0f;
        reqPos_ = (reqPos_ + 1) % N;
        float target = 1.0f;
        for (int k = 0; k < N; ++k) target = std::min(target, req_[(size_t)k]);
        g_ = target + (target < g_ ? attackStep_ : aR_) * (g_ - target);
        for (int c = 0; c < b.numChannels; ++c) {
            auto& d = delay_[c];
            const float delayed = d[(size_t)pos_];
            d[(size_t)pos_] = b.ch[c][i] * ig;
            float y = delayed * g_;
            // safety: guarantee the ceiling (only active on residual overs after the ramp)
            b.ch[c][i] = clampv(y, -ceil_, ceil_);
        }
        pos_ = (pos_ + 1) % look_;
    }
}

} // namespace mfx
