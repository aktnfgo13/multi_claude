#include "dsp/delay/BbdDelay.h"

#include <cstring>

#include "dsp/modulation/Modulation.h"
#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

namespace {
constexpr float kRef = 0.25f;        // compander reference level
constexpr float kEnvFloor = 1e-3f;   // compressor gain limited to +24 dB
constexpr float kBucketBias = 0.08f; // asymmetric bucket overload
} // namespace

BbdDelay::BbdDelay() {
    params_.add({"time_ms", "Time", (float)kMinTimeMs, (float)kMaxTimeMs, 300, ParamUnit::Milliseconds, ParamScale::Log});
    ParamInfo s{"sync", "Sync", 0, (float)(kNumSyncChoices - 1), 0, ParamUnit::Choice};
    s.choices = kSyncChoices; s.numChoices = kNumSyncChoices; params_.add(s);
    params_.add({"feedback", "Feedback", 0, 110, 40, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 35, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 45, ParamUnit::Percent});
    params_.add({"mod_depth", "Mod Depth", 0, 100, 15, ParamUnit::Percent});
    params_.add({"mod_rate_hz", "Mod Rate", 0.1f, 8, 0.5f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"compander", "Compander", 0, 100, 100, ParamUnit::Percent});
    params_.add({"drive", "Drive", 0, 100, 30, ParamUnit::Percent});
    params_.add({"spread", "Spread", 0, 100, 40, ParamUnit::Percent});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}

double BbdDelay::effectiveTimeMs() const {
    const int sync = params_.getInt(kSync);
    if (sync > 0) return clampv(60000.0 / bpm_ * syncDivisionBeats(sync), kMinTimeMs, kMaxTimeMs);
    return params_.get(kTime);
}

void BbdDelay::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    envA_ = onePoleCoeff(0.004, fs);
    envR_ = onePoleCoeff(0.040, fs);
    timeA_ = 1.0 - std::exp(-1.0 / (0.06 * fs)); // clock VCO / time-knob lag
    fbHp_.design(FilterType::HighPass1, fs, 40.0);
    mix_.reset(fs, 0.03); fb_.reset(fs, 0.03); level_.reset(fs, 0.03);
}

void BbdDelay::onReset() {
    std::memset(ring_, 0, sizeof(ring_));
    for (auto* f : {&aa1_, &aa2_, &rc1_, &rc2_, &fbHp_}) f->reset();
    for (int c = 0; c < 2; ++c) {
        count_[c] = 0; phase_[c] = 0.0; xPrev_[c] = 0.0f; envC_[c] = envE_[c] = 0.0f; fbState_[c] = 0.0f;
        spcBase_[c] = spcTarget_[c];
        spc_[c] = spcBase_[c];
    }
    lfoPh_ = 0.0;
}

void BbdDelay::onParametersChanged(bool immediate) {
    const double fs = spec_.sampleRate;
    spread_ = params_.get(kSpread) / 100.0f;
    const double base = effectiveTimeMs() * 0.001 * fs / kBuckets;
    spcTarget_[0] = base;
    spcTarget_[1] = base * (1.0 + 0.03 * spread_); // second chip runs slightly slower
    const double fa = std::min(mapLog(params_.get(kTone) / 100.0, 2000.0, 9000.0), 0.45 * fs);
    aa1_.design(FilterType::LowPass, fs, fa, 0.5412); aa2_.design(FilterType::LowPass, fs, fa, 1.3066);
    rc1_.design(FilterType::LowPass, fs, fa, 0.5412); rc2_.design(FilterType::LowPass, fs, fa, 1.3066);
    modDepth_ = params_.get(kModDepth) / 100.0f;
    lfoInc_ = params_.get(kModRate) / fs;
    const float amt = params_.get(kCompander) / 100.0f;
    compA_ = 0.5f * amt;                    // compressor exponent (2:1 at 100 %)
    expPow_ = compA_ / (1.0f - compA_);     // matching expander exponent
    driveIn_ = dbToGain(-6.0f + 18.0f * params_.get(kDrive) / 100.0f);
    driveOut_ = 1.0f / driveIn_;
    const float fb = params_.get(kFeedback) / 100.0f, mx = params_.get(kMix) / 100.0f;
    const float lv = dbToGain(params_.get(kLevel));
    if (immediate) {
        fb_.setImmediate(fb); mix_.setImmediate(mx); level_.setImmediate(lv);
        for (int c = 0; c < 2; ++c) spcBase_[c] = spcTarget_[c];
    } else { fb_.setTarget(fb); mix_.setTarget(mx); level_.setTarget(lv); }
}

float BbdDelay::processChannel(int c, float x, double s) {
    // compressor (rectified-average detector on the chip input)
    const float v = x * driveIn_;
    const float av = std::fabs(v);
    envC_[c] = av + (av > envC_[c] ? envA_ : envR_) * (envC_[c] - av);
    const float e = std::max(envC_[c], kEnvFloor);
    const float gc = compA_ == 0.5f ? std::sqrt(kRef / e) : (compA_ > 0.0f ? std::pow(kRef / e, compA_) : 1.0f);
    float a = aa2_.process(aa1_.process(v * gc, c), c);
    // bucket overload (charge limits), unity small-signal slope
    static const float tb = nl::fastTanh(kBucketBias), nb = 1.0f / (1.0f - std::tanh(kBucketBias) * std::tanh(kBucketBias));
    a = (nl::fastTanh(a + kBucketBias) - tb) * nb;
    // sample at the clock instants inside (n-1, n]
    float* ring = ring_[c];
    double ph = phase_[c] + 1.0;
    while (ph >= s) {
        ph -= s;
        const float f = (float)clampv(ph, 0.0, 1.0); // tick happened f samples before n
        ring[count_[c] & kMask] = flushDenorm(a - f * (a - xPrev_[c]));
        ++count_[c];
    }
    phase_[c] = ph;
    xPrev_[c] = a;
    // reconstruct the output stream: entry (K-1-N) emerged at the last tick, ph/s ticks ago
    const int64_t base = count_[c] - 1 - kBuckets;
    const float u = (float)clampv(ph / s, 0.0, 1.0);
    const float xm1 = ring[(base - 1) & kMask], x0 = ring[base & kMask];
    const float x1 = ring[(base + 1) & kMask], x2 = ring[(base + 2) & kMask];
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    float r = ((c3 * u + c2) * u + c1) * u + x0;
    r = rc2_.process(rc1_.process(r, c), c);
    // expander (detector on the chip output)
    const float ar = std::fabs(r);
    envE_[c] = ar + (ar > envE_[c] ? envA_ : envR_) * (envE_[c] - ar);
    const float ge = expPow_ == 1.0f ? envE_[c] / kRef : (expPow_ > 0.0f ? std::pow(envE_[c] / kRef, expPow_) : 1.0f);
    return r * ge * driveOut_;
}

void BbdDelay::onProcess(AudioBlock& b) {
    const bool stereo = b.numChannels == 2;
    const int nch = stereo ? 2 : 1;
    for (int i = 0; i < b.numSamples; ++i) {
        const float fb = fb_.next(), mix = mix_.next(), lvl = level_.next();
        const float lfo[2] = {(float)std::sin(kTwoPi * lfoPh_), (float)std::sin(kTwoPi * (lfoPh_ + 0.5 * spread_))};
        lfoPh_ += lfoInc_; lfoPh_ -= std::floor(lfoPh_);
        float y[2] = {0, 0};
        for (int c = 0; c < nch; ++c) {
            spcBase_[c] += timeA_ * (spcTarget_[c] - spcBase_[c]);
            spc_[c] = spcBase_[c] * (1.0 + 0.01 * modDepth_ * lfo[c]);
            const float in = b.ch[c][i];
            y[c] = processChannel(c, in + fbState_[c], spc_[c]);
            fbState_[c] = nl::diodeFeedback(fbHp_.process(y[c], c) * fb, 1.2f);
        }
        const float dg = std::min(1.0f, 2.0f * (1.0f - mix)), wg = std::min(1.0f, 2.0f * mix);
        for (int c = 0; c < nch; ++c) b.ch[c][i] = lvl * (dg * b.ch[c][i] + wg * y[c]);
    }
}

} // namespace mfx
