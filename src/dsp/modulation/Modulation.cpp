#include "dsp/modulation/Modulation.h"

#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

const char* kSyncChoices[] = {"Off", "1/1", "1/2", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16"};
double syncDivisionBeats(int i) {
    static const double beats[] = {0, 4, 2, 1, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25};
    return beats[clampv(i, 0, kNumSyncChoices - 1)];
}

// ---------------------------------------------------------------- ModDelayEffect
void ModDelayEffect::onPrepare(const ProcessSpec& spec) {
    line_.prepare((int)(0.05 * spec.sampleRate) + 8);
    lfo_.prepare(spec.sampleRate);
    for (auto* s : {&base_, &depth_, &mix_, &fb_}) s->reset(spec.sampleRate, 0.05);
    fbLp_.design(FilterType::LowPass1, spec.sampleRate, 8000.0);
}
void ModDelayEffect::onReset() {
    line_.reset(); lfo_.reset(); fbLp_.reset();
    fbState_[0] = fbState_[1] = 0.0f;
}
void ModDelayEffect::onParametersChanged(bool immediate) {
    const Cfg c = cfg();
    lfo_.setRate(c.rateHz);
    spread_ = c.spread;
    const float fs = (float)spec_.sampleRate;
    auto set = [immediate](LinearSmoothed& s, float v) { if (immediate) s.setImmediate(v); else s.setTarget(v); };
    set(base_, c.baseMs * 0.001f * fs);
    set(depth_, c.depthMs * 0.001f * fs);
    set(mix_, c.mix);
    set(fb_, c.feedback);
}
void ModDelayEffect::onProcess(AudioBlock& b) {
    const float maxD = (float)line_.maxDelay() - 2.0f;
    for (int i = 0; i < b.numSamples; ++i) {
        const float base = base_.next(), depth = depth_.next(), mix = mix_.next(), fb = fb_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float m = lfo_.valueAt(c * spread_);
            const float d = clampv(base + depth * 0.5f * (1.0f + m), 2.0f, maxD);
            const float wet = line_.read(d, c);
            const float x = b.ch[c][i];
            const float fbv = fbLp_.process(wet, c) * fb;
            line_.push(x + nl::fastTanh(fbv), c);
            b.ch[c][i] = x + mix * (wet - x);
        }
        line_.advance();
        lfo_.advance();
    }
}

Chorus::Chorus() {
    params_.add({"rate_hz", "Rate", 0.05f, 8, 0.8f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 50, ParamUnit::Percent});
    params_.add({"delay_ms", "Delay", 3, 30, 12, ParamUnit::Milliseconds});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    params_.add({"spread", "Stereo Spread", 0, 100, 50, ParamUnit::Percent});
}
ModDelayEffect::Cfg Chorus::cfg() const {
    return {params_.get(2), params_.get(1) / 100.0f * 8.0f, params_.get(0), 0.0f, params_.get(3) / 100.0f, params_.get(4) / 200.0f};
}

Flanger::Flanger() {
    params_.add({"rate_hz", "Rate", 0.02f, 5, 0.25f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 70, ParamUnit::Percent});
    params_.add({"delay_ms", "Manual", 0.3f, 8, 1.5f, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"feedback", "Feedback", -95, 95, 50, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    params_.add({"spread", "Stereo Spread", 0, 100, 25, ParamUnit::Percent});
}
ModDelayEffect::Cfg Flanger::cfg() const {
    return {params_.get(2), params_.get(1) / 100.0f * 4.0f, params_.get(0), params_.get(3) / 100.0f, params_.get(4) / 100.0f, params_.get(5) / 200.0f};
}

Vibrato::Vibrato() {
    params_.add({"rate_hz", "Rate", 0.1f, 12, 5, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 40, ParamUnit::Percent});
}
ModDelayEffect::Cfg Vibrato::cfg() const {
    // 100 % wet pitch modulation; depth up to 3 ms peak-to-peak
    return {1.0f, params_.get(1) / 100.0f * 3.0f, params_.get(0), 0.0f, 1.0f, 0.0f};
}

// ---------------------------------------------------------------- Phaser
static const char* kStageChoices[] = {"4", "6", "8", "12"};
Phaser::Phaser() {
    ParamInfo s{"stages", "Stages", 0, 3, 1, ParamUnit::Choice}; s.choices = kStageChoices; s.numChoices = 4;
    params_.add(s);
    params_.add({"rate_hz", "Rate", 0.02f, 8, 0.5f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 70, ParamUnit::Percent});
    params_.add({"centre_hz", "Centre", 150, 3000, 700, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"feedback", "Feedback", -90, 90, 30, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    params_.add({"stereo", "Stereo", 0, 100, 50, ParamUnit::Percent});
}
void Phaser::onPrepare(const ProcessSpec& spec) { lfo_.prepare(spec.sampleRate); mix_.reset(spec.sampleRate, 0.03); }
void Phaser::onReset() {
    for (auto& ch : z_) for (auto& v : ch) v = 0.0f;
    fb_[0] = fb_[1] = 0.0f;
    lfo_.reset();
    counter_ = 0;
}
void Phaser::onParametersChanged(bool immediate) {
    static const int st[] = {4, 6, 8, 12};
    stages_ = st[clampv(params_.getInt(kStages), 0, 3)];
    lfo_.setRate(params_.get(kRate));
    depth_ = params_.get(kDepth) / 100.0f;
    centre_ = params_.get(kCentre);
    feedback_ = params_.get(kFeedback) / 100.0f;
    stereo_ = params_.get(kStereo) / 200.0f;
    if (immediate) mix_.setImmediate(params_.get(kMix) / 100.0f); else mix_.setTarget(params_.get(kMix) / 100.0f);
}
void Phaser::onProcess(AudioBlock& b) {
    const double fs = spec_.sampleRate;
    for (int i = 0; i < b.numSamples; ++i) {
        if ((counter_++ & 7u) == 0) {
            for (int c = 0; c < b.numChannels; ++c) {
                // sweep +/- 2 octaves * depth around the centre (exponential)
                const double f = centre_ * std::pow(2.0, 2.0 * depth_ * lfo_.valueAt(c * stereo_));
                const double t = std::tan(kPi * std::min(f, 0.45 * fs) / fs);
                coef_[c] = (float)((t - 1.0) / (t + 1.0));
            }
        }
        const float mix = mix_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = b.ch[c][i];
            float v = x + feedback_ * fb_[c];
            const float a = coef_[c];
            for (int s = 0; s < stages_; ++s) {
                const float y = a * v + z_[c][s];
                z_[c][s] = v - a * y;
                v = y;
            }
            fb_[c] = flushDenorm(nl::fastTanh(v));
            // mix = 100 % gives the classic 50/50 dry + all-pass sum (deepest notches)
            b.ch[c][i] = (1.0f - 0.5f * mix) * x + 0.5f * mix * v;
        }
        lfo_.advance();
    }
}

// ---------------------------------------------------------------- Tremolo
static const char* kTremShapes[] = {"Sine", "Triangle", "Square (soft)"};
Tremolo::Tremolo() {
    params_.add({"rate_hz", "Rate", 0.2f, 16, 4, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 50, ParamUnit::Percent});
    ParamInfo s{"shape", "Shape", 0, 2, 0, ParamUnit::Choice}; s.choices = kTremShapes; s.numChoices = 3;
    params_.add(s);
    params_.add({"stereo", "Stereo (Pan)", 0, 1, 0, ParamUnit::Toggle});
    ParamInfo sy{"sync", "Sync", 0, (float)(kNumSyncChoices - 1), 0, ParamUnit::Choice}; sy.choices = kSyncChoices; sy.numChoices = kNumSyncChoices;
    params_.add(sy);
}
void Tremolo::onPrepare(const ProcessSpec& spec) { lfo_.prepare(spec.sampleRate); depth_.reset(spec.sampleRate, 0.03); }
void Tremolo::onParametersChanged(bool immediate) {
    lfo_.setShape(params_.getInt(kShape) == 1 ? LFO::Shape::Triangle : LFO::Shape::Sine);
    if (immediate) depth_.setImmediate(params_.get(kDepth) / 100.0f); else depth_.setTarget(params_.get(kDepth) / 100.0f);
}
void Tremolo::onProcess(AudioBlock& b) {
    const int sync = params_.getInt(kSync);
    const double rate = sync > 0 ? (bpm_ / 60.0) / syncDivisionBeats(sync) : params_.get(kRate);
    lfo_.setRate(rate);
    const bool square = params_.getInt(kShape) == 2;
    const bool pan = params_.getInt(kStereo) != 0 && b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float d = depth_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            float m = lfo_.valueAt(pan && c == 1 ? 0.5 : 0.0);
            if (square) m = nl::fastTanh(4.0f * m);
            const float g = 1.0f - d * 0.5f * (1.0f - m); // 1 .. 1-d
            b.ch[c][i] *= g;
        }
        lfo_.advance();
    }
}

} // namespace mfx
