#include "dsp/delay/Delay.h"

#include "dsp/modulation/Modulation.h"
#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

static const char* kModes[] = {"Digital", "Analog", "Tape"};
static const char* kRoutings[] = {"Mono", "Stereo", "Ping Pong"};

Delay::Delay() {
    ParamInfo m{"mode", "Mode", 0, 2, 0, ParamUnit::Choice}; m.choices = kModes; m.numChoices = 3; params_.add(m);
    ParamInfo r{"routing", "Routing", 0, 2, 1, ParamUnit::Choice}; r.choices = kRoutings; r.numChoices = 3; params_.add(r);
    params_.add({"time_ms", "Time", 10, 2000, 380, ParamUnit::Milliseconds, ParamScale::Log});
    ParamInfo s{"sync", "Sync", 0, (float)(kNumSyncChoices - 1), 0, ParamUnit::Choice}; s.choices = kSyncChoices; s.numChoices = kNumSyncChoices; params_.add(s);
    params_.add({"feedback", "Feedback", 0, 100, 35, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 30, ParamUnit::Percent});
    params_.add({"low_cut_hz", "Low Cut", 20, 1000, 80, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"high_cut_hz", "High Cut", 1000, 20000, 9000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"mod_depth", "Mod Depth", 0, 100, 10, ParamUnit::Percent});
    params_.add({"mod_rate_hz", "Mod Rate", 0.05f, 5, 0.6f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"age", "Age", 0, 100, 30, ParamUnit::Percent});
    params_.add({"offset", "Stereo Offset", -50, 50, 12, ParamUnit::Percent});
    // v0.6 (appended, neutral defaults)
    params_.add({"cross_fb", "Cross Feedback", 0, 100, 0, ParamUnit::Percent});
    params_.add({"fb_lr_offset", "Feedback L/R", -50, 50, 0, ParamUnit::Percent});
    params_.add({"width", "Width", 0, 200, 100, ParamUnit::Percent});
    static const char* kRModes[] = {"Offset", "Free"};
    ParamInfo rm{"time_r_mode", "Right Time", 0, 1, 0, ParamUnit::Choice}; rm.choices = kRModes; rm.numChoices = 2; params_.add(rm);
    params_.add({"time_r_ms", "Time R", 10, 2000, 380, ParamUnit::Milliseconds, ParamScale::Log});
}

double Delay::effectiveTimeMs() const {
    const int sync = params_.getInt(kSync);
    if (sync > 0) return std::min(kMaxSeconds * 1000.0 - 50.0, 60000.0 / bpm_ * syncDivisionBeats(sync));
    return params_.get(kTime);
}

void Delay::onPrepare(const ProcessSpec& spec) {
    line_.prepare((int)(kMaxSeconds * spec.sampleRate * 1.6) + 64);
    wow_.prepare(spec.sampleRate); flutter_.prepare(spec.sampleRate); mod_.prepare(spec.sampleRate);
    wow_.setRate(0.45); flutter_.setRate(6.3);
    mix_.reset(spec.sampleRate, 0.03); fb_.reset(spec.sampleRate, 0.03); width_.reset(spec.sampleRate, 0.03);
    xfLen_ = (int)(0.05 * spec.sampleRate);
    slew_ = 1.0f - onePoleCoeff(0.12, spec.sampleRate);
}

void Delay::onReset() {
    line_.reset();
    loopHp_[0].reset(); loopLp_[0].reset(); headBump_.reset();
    for (int c = 0; c < kMaxChannels; ++c) { fbState_[c] = 0; xfLeft_[c] = 0; xf_[c] = 1; }
    wow_.reset(); flutter_.reset(0.3); mod_.reset();
    const float d = (float)(effectiveTimeMs() * 0.001 * spec_.sampleRate);
    for (int c = 0; c < kMaxChannels; ++c) cur_[c] = prev_[c] = d;
}

void Delay::onParametersChanged(bool immediate) {
    mode_ = params_.getInt(kMode);
    routing_ = params_.getInt(kRouting);
    const double fs = spec_.sampleRate;
    age_ = params_.get(kAge) / 100.0f;
    double hc = params_.get(kHighCut);
    if (mode_ == Analog) hc = std::min(hc, 6500.0 - 4000.0 * age_); // BBD bandwidth shrinks with "age"
    if (mode_ == Tape) hc = std::min(hc, 12000.0 - 7000.0 * age_);
    loopHp_[0].design(FilterType::HighPass1, fs, params_.get(kLowCut));
    loopLp_[0].design(FilterType::LowPass, fs, std::min(hc, 0.45 * fs), 0.6);
    headBump_.design(FilterType::Peak, fs, 110.0, 1.0, mode_ == Tape ? 2.5 * age_ : 0.0);
    mod_.setRate(params_.get(kModRate));
    modDepth_ = params_.get(kModDepth) / 100.0f;
    const float fb = params_.get(kFeedback) / 100.0f * (mode_ == Digital ? 0.98f : 1.08f);
    const float mx = params_.get(kMix) / 100.0f;
    cross_ = params_.get(kCrossFb) / 100.0f;
    fbOff_ = params_.get(kFbOffset) / 100.0f;
    rFree_ = params_.getInt(kTimeRMode) == RFree;
    const float wd = params_.get(kWidth) / 100.0f;
    if (immediate) { fb_.setImmediate(fb); mix_.setImmediate(mx); width_.setImmediate(wd); }
    else { fb_.setTarget(fb); mix_.setTarget(mx); width_.setTarget(wd); }
}

float Delay::readTap(int ch, float target, float& cur, float& prev, float& xf, int& xfLeft, float modS) {
    const float maxD = (float)line_.maxDelay() - 4.0f;
    if (mode_ == Digital) {
        if (xfLeft == 0 && std::fabs(target - cur) > 0.5f) { prev = cur; cur = target; xfLeft = xfLen_; }
        float y = line_.read(clampv(cur + modS, 2.0f, maxD), ch);
        if (xfLeft > 0) {
            const float t = 1.0f - (float)xfLeft / (float)xfLen_;
            const float yo = line_.read(clampv(prev + modS, 2.0f, maxD), ch);
            y = yo + t * (y - yo);
            --xfLeft;
        }
        (void)xf;
        return y;
    }
    // analog / tape: the time slews (pitch bend), like a varying clock / motor speed
    cur += slew_ * (target - cur);
    return line_.read(clampv(cur + modS, 2.0f, maxD), ch);
}

void Delay::onProcess(AudioBlock& b) {
    const double fs = spec_.sampleRate;
    const float base = (float)(effectiveTimeMs() * 0.001 * fs);
    const float off = params_.get(kOffset) / 100.0f;
    const float tL = base;
    const float tR = routing_ != Stereo ? base
                   : (rFree_ ? (float)(std::min((double)params_.get(kTimeR), kMaxSeconds * 1000.0 - 50.0) * 0.001 * fs)
                             : base * (1.0f + off));
    const bool fbMods = fbOff_ != 0.0f || cross_ > 0.0f;
    const float modAmp = modDepth_ * 0.002f * (float)fs * (mode_ == Digital ? 0.5f : 1.0f); // up to 2 ms
    const float wowAmp = mode_ == Tape ? age_ * 0.0025f * (float)fs : 0.0f;
    const float flutAmp = mode_ == Tape ? age_ * 0.00025f * (float)fs : 0.0f;
    const bool stereoIn = b.numChannels == 2;

    for (int i = 0; i < b.numSamples; ++i) {
        const float mix = mix_.next(), fb = fb_.next();
        const float modS = modAmp * mod_.valueAt(0.0) + wowAmp * wow_.valueAt(0.0) + flutAmp * flutter_.valueAt(0.0);
        const float modR = modAmp * mod_.valueAt(0.25) + wowAmp * wow_.valueAt(0.1) + flutAmp * flutter_.valueAt(0.2);
        float wet[2];
        wet[0] = readTap(0, tL, cur_[0], prev_[0], xf_[0], xfLeft_[0], modS);
        wet[1] = stereoIn ? readTap(1, tR, cur_[1], prev_[1], xf_[1], xfLeft_[1], modR) : wet[0];
        const float inL = b.ch[0][i], inR = stereoIn ? b.ch[1][i] : inL;
        // feedback path conditioning
        float f[2];
        for (int c = 0; c < (stereoIn ? 2 : 1); ++c) {
            float v = loopLp_[0].process(loopHp_[0].process(wet[c], c), c);
            if (mode_ == Tape) v = headBump_.process(v, c);
            f[c] = v * fb;
        }
        if (!stereoIn) f[1] = f[0];
        else if (fbMods) { // v0.6: L/R feedback offset and cross feedback (applied only when set)
            const float fl = f[0] * (1.0f - fbOff_), fr = f[1] * (1.0f + fbOff_);
            const float x = routing_ == PingPong ? 0.0f : cross_;
            f[0] = fl + x * (fr - fl);
            f[1] = fr + x * (fl - fr);
        }
        float wL, wR;
        if (routing_ == PingPong && stereoIn) {
            const float mono = 0.5f * (inL + inR);
            wL = mono + f[1];
            wR = f[0];
        } else {
            wL = inL + f[0];
            wR = inR + f[1];
        }
        if (mode_ == Digital) { // keep runaway feedback bounded without colouring normal levels
            wL = std::fabs(wL) > 1.0f ? std::copysign(1.0f + nl::fastTanh(std::fabs(wL) - 1.0f), wL) : wL;
            wR = std::fabs(wR) > 1.0f ? std::copysign(1.0f + nl::fastTanh(std::fabs(wR) - 1.0f), wR) : wR;
        } else { // BBD / tape record saturation
            const float drive = 1.0f + 2.0f * age_;
            wL = nl::fastTanh(wL * drive) / drive;
            wR = nl::fastTanh(wR * drive) / drive;
        }
        line_.push(flushDenorm(wL), 0);
        if (stereoIn) line_.push(flushDenorm(wR), 1);
        line_.advance();
        wow_.advance(); flutter_.advance(); mod_.advance();
        const float wd = width_.next();
        if (stereoIn && (wd != 1.0f || width_.isSmoothing())) { // v0.6: wet M/S width
            const float m = 0.5f * (wet[0] + wet[1]), sd = 0.5f * (wet[0] - wet[1]) * wd;
            wet[0] = m + sd; wet[1] = m - sd;
        }
        b.ch[0][i] = inL + mix * (wet[0] - inL);
        if (stereoIn) b.ch[1][i] = inR + mix * (wet[1] - inR);
    }
}

} // namespace mfx
