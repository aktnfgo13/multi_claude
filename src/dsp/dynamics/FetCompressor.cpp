#include "dsp/dynamics/FetCompressor.h"

#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

namespace {
const char* kRatioChoices[] = {"4:1", "8:1", "12:1", "20:1", "All"};
constexpr double kRatios[5] = {4.0, 8.0, 12.0, 20.0, 30.0};
constexpr double kThresholds[5] = {-24.0, -22.0, -20.0, -18.0, -26.0}; // dBFS peak, input 0 dB
constexpr double kKnees[5] = {8.0, 6.0, 5.0, 4.0, 12.0};
constexpr float kInputLimit = 4.0f;  // input stage soft limit (+12 dBFS)
constexpr float kOutputLimit = 8.0f; // output stage soft limit (+18 dBFS)
} // namespace

FetCompressor::FetCompressor() {
    params_.add({"input_db", "Input", -20, 40, 0, ParamUnit::Decibels});
    params_.add({"output_db", "Output", -30, 20, 0, ParamUnit::Decibels});
    params_.add({"attack_us", "Attack", 20, 800, 200, ParamUnit::None, ParamScale::Log});
    params_.add({"release_ms", "Release", 50, 1100, 250, ParamUnit::Milliseconds, ParamScale::Log});
    ParamInfo r{"ratio", "Ratio", 0, 4, 0, ParamUnit::Choice}; r.choices = kRatioChoices; r.numChoices = 5; params_.add(r);
    params_.add({"sc_hpf_hz", "SC High-Pass", 20, 500, 20, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"character", "Character", 0, 100, 50, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
}

void FetCompressor::onPrepare(const ProcessSpec& spec) {
    in_.reset(spec.sampleRate, 0.02); out_.reset(spec.sampleRate, 0.02); mix_.reset(spec.sampleRate, 0.02);
    dcBlock_.design(FilterType::HighPass1, spec.sampleRate, 8.0);
    chargeUp_ = onePoleCoeff(0.8, spec.sampleRate * (1.0 / 16.0));   // control rate = fs / 16
    chargeDown_ = onePoleCoeff(2.0, spec.sampleRate * (1.0 / 16.0));
}

void FetCompressor::onReset() { gr_ = 0.0; charge_ = 0.0; ctl_ = 0; ctlNeed_ = 0.0; scHp_.reset(); dcBlock_.reset(); }

void FetCompressor::onParametersChanged(bool immediate) {
    const double fs = spec_.sampleRate;
    const int r = params_.getInt(kRatio);
    ratio_ = kRatios[r]; thr_ = kThresholds[r];
    knee_ = kKnees[r] / ratio_; // the knee lives on the output-level axis: W/R there ~ W on the input axis
    const double timeScale = r == kAll ? 0.6 : 1.0;
    // in the feedback loop the linearised attack time constant is tau / R (above threshold); scale it so the knob
    // reads as the effective attack time
    aA_ = onePoleCoeff(params_.get(kAttack) * 1e-6 * timeScale * ratio_, fs);
    relSec_ = params_.get(kRelease) * 0.001 * timeScale;
    aR_ = onePoleCoeff(relSec_ * (1.0 + 3.0 * charge_), fs);
    const float hp = params_.get(kScHpf);
    scOn_ = hp > 21.0f;
    if (scOn_) scHp_.design(FilterType::HighPass, fs, hp, 0.7071);
    const float ch = params_.get(kCharacter) / 100.0f;
    fet_ = 0.15f * ch * (r == kAll ? 2.5f : 1.0f);
    outSat_ = 0.5f * ch;
    satBias_ = 0.1f;
    satOff_ = nl::fastTanh(satBias_);
    const float t = std::tanh(satBias_);
    satNorm_ = 1.0f / (0.5f * (1.0f - t * t));
    const float ig = dbToGain(params_.get(kInput)), og = dbToGain(params_.get(kOutput)), mx = params_.get(kMix) / 100.0f;
    if (immediate) { in_.setImmediate(ig); out_.setImmediate(og); mix_.setImmediate(mx); }
    else { in_.setTarget(ig); out_.setTarget(og); mix_.setTarget(mx); }
}

double FetCompressor::reductionForOutput(double y) const {
    const double d = y - thr_;
    if (2.0 * d < -knee_) return 0.0;
    if (2.0 * std::fabs(d) <= knee_) { const double e = d + 0.5 * knee_; return (ratio_ - 1.0) * e * e / (2.0 * knee_); }
    return (ratio_ - 1.0) * d;
}

double FetCompressor::slopeForOutput(double y) const {
    const double d = y - thr_;
    if (2.0 * d < -knee_) return 0.0;
    if (2.0 * std::fabs(d) <= knee_) return (ratio_ - 1.0) * (d + 0.5 * knee_) / knee_;
    return ratio_ - 1.0;
}

// Solve g = a*gr_ + (1-a)*C(x - g) for g >= 0 (f is increasing and concave in g: Newton converges monotonically
// after at most one step past the root).
double FetCompressor::solve(double x, double a) const {
    double g = gr_;
    for (int it = 0; it < 6; ++it) {
        const double y = x - g;
        const double f = g - a * gr_ - (1.0 - a) * reductionForOutput(y);
        const double fp = 1.0 + (1.0 - a) * slopeForOutput(y);
        const double step = f / fp;
        g = std::max(0.0, g - step);
        if (std::fabs(step) < 1e-7) break;
    }
    return g;
}

void FetCompressor::onProcess(AudioBlock& b) {
    const int nch = b.numChannels;
    for (int i = 0; i < b.numSamples; ++i) {
        const float ig = in_.next(), og = out_.next(), mix = mix_.next();
        float xs[2] = {0, 0};
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c) {
            const float in = b.ch[c][i];
            const float x = (std::isfinite(in) ? in : 0.0f) * ig;
            xs[c] = x / std::sqrt(1.0f + (x / kInputLimit) * (x / kInputLimit));
            const float sc = scOn_ ? scHp_.process(xs[c], c) : xs[c];
            peak = std::max(peak, std::fabs(sc));
        }
        // feedback detector, delay-free: branch by whether the output-referred reduction must grow
        const double xDb = peak > 1e-6f ? 20.0 * std::log10((double)peak) : -120.0;
        const double need = reductionForOutput(xDb - gr_);
        const bool attack = need > gr_;
        ctlNeed_ = std::max(ctlNeed_, need);
        gr_ = solve(xDb, attack ? aA_ : aR_);
        // program-dependent release (control rate, block-size independent)
        if (++ctl_ >= 16) {
            ctl_ = 0;
            const double target = ctlNeed_ > 2.0 ? 1.0 : 0.0; // charges only while the signal is over threshold
            ctlNeed_ = 0.0;
            charge_ = target + (target > charge_ ? chargeUp_ : chargeDown_) * (charge_ - target);
            aR_ = std::exp(-1.0 / (relSec_ * (1.0 + 3.0 * charge_) * spec_.sampleRate));
        }
        const float g = (float)std::pow(10.0, -gr_ / 20.0);
        for (int c = 0; c < nch; ++c) {
            const float u = g * xs[c];
            const float fetTerm = fet_ * (1.0f - g) * u * u / (1.0f + u * u);
            float z = (u + dcBlock_.process(fetTerm, c)) * og;
            if (outSat_ > 0.0f) z += outSat_ * ((nl::fastTanh(0.5f * z + satBias_) - satOff_) * satNorm_ - z);
            z = z / std::sqrt(1.0f + (z / kOutputLimit) * (z / kOutputLimit));
            const float dry = std::isfinite(b.ch[c][i]) ? b.ch[c][i] : 0.0f;
            b.ch[c][i] = dry + mix * (z - dry);
        }
    }
}

} // namespace mfx
