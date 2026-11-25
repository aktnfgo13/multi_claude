#include "dsp/dynamics/PremiumGate.h"

namespace mfx {

namespace {
const char* kModes[] = {"Gate", "Expander"};
const char* kDetectors[] = {"Peak", "RMS"};
constexpr double kKneeDb = 6.0;
constexpr double kFloorDb = -120.0;
} // namespace

PremiumGate::PremiumGate() {
    params_.add({"threshold_db", "Threshold", -90, 0, -55, ParamUnit::Decibels});
    params_.add({"range_db", "Range", -90, 0, -60, ParamUnit::Decibels});
    params_.add({"attack_ms", "Attack", 0.05f, 20, 0.5f, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"hold_ms", "Hold", 0, 500, 40, ParamUnit::Milliseconds});
    params_.add({"release_ms", "Release", 5, 3000, 250, ParamUnit::Milliseconds, ParamScale::Log});
    params_.add({"ratio", "Ratio", 1.5f, 20, 4, ParamUnit::Ratio, ParamScale::Log});
    params_.add({"hysteresis_db", "Hysteresis", 0, 20, 6, ParamUnit::Decibels});
    ParamInfo m{"mode", "Mode", 0, 1, (float)kExpander, ParamUnit::Choice}; m.choices = kModes; m.numChoices = 2; params_.add(m);
    ParamInfo d{"detector", "Detector", 0, 1, (float)kPeak, ParamUnit::Choice}; d.choices = kDetectors; d.numChoices = 2; params_.add(d);
    params_.add({"sc_hpf_hz", "SC High-Pass", 20, 500, 80, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"stereo_link", "Stereo Link", 0, 1, 1, ParamUnit::Toggle});
    params_.add({"adaptive_release", "Adaptive Release", 0, 1, 1, ParamUnit::Toggle});
}

void PremiumGate::onPrepare(const ProcessSpec& spec) {
    peakRel_ = onePoleCoeff(0.020, spec.sampleRate);
    rmsA_ = 1.0f - onePoleCoeff(0.003, spec.sampleRate);
    corner_ = 1.0f - onePoleCoeff(0.0002, spec.sampleRate);
    slowA_ = 1.0f - onePoleCoeff(0.030, spec.sampleRate);
}

void PremiumGate::onReset() {
    scHp_.reset();
    for (int c = 0; c < kMaxChannels; ++c) {
        env_[c] = 0.0f; open_[c] = true; hold_[c] = holdSamples_; g_[c] = 1.0f; // start open (no fade-in / cut on first note)
        slowLevel_[c] = kFloorDb; decayRate_[c] = 0.0;
        red_[c] = 0.0; // start open: no fade-in on the first note after a reset
    }
    transitions_ = 0;
}

void PremiumGate::onParametersChanged(bool) {
    const double fs = spec_.sampleRate;
    thr_ = params_.get(kThreshold);
    range_ = -params_.get(kRange);
    ratio_ = params_.get(kRatio);
    hyst_ = params_.get(kHysteresis);
    attStep_ = 60.0 / std::max(1.0, params_.get(kAttack) * 0.001 * fs);   // dB per sample
    relStep_ = 60.0 / std::max(1.0, params_.get(kRelease) * 0.001 * fs);
    holdSamples_ = (int)(params_.get(kHold) * 0.001 * fs);
    mode_ = params_.getInt(kMode);
    det_ = params_.getInt(kDetector);
    link_ = params_.getInt(kLink) != 0;
    adaptive_ = params_.getInt(kAdaptive) != 0;
    scHp_.design(FilterType::HighPass, fs, params_.get(kScHpf), 0.7071);
}

double PremiumGate::staticReduction(double L, bool open) const {
    const double t = open ? thr_ - hyst_ : thr_;
    if (mode_ == kGate) return L >= t ? 0.0 : range_;
    // downward expander with quadratic soft knee: slope (ratio - 1) below t
    const double d = L - t;
    double r;
    if (d >= kKneeDb * 0.5) r = 0.0;
    else if (d > -kKneeDb * 0.5) { const double e = kKneeDb * 0.5 - d; r = (ratio_ - 1.0) * e * e / (2.0 * kKneeDb); }
    else r = (ratio_ - 1.0) * -d;
    return std::min(r, range_);
}

void PremiumGate::onProcess(AudioBlock& b) {
    const int nch = b.numChannels;
    const int nState = link_ ? 1 : nch;
    for (int i = 0; i < b.numSamples; ++i) {
        double level[2] = {kFloorDb, kFloorDb};
        for (int c = 0; c < nch; ++c) {
            float x = b.ch[c][i];
            if (!std::isfinite(x)) x = 0.0f;
            const float s = scHp_.process(x, c);
            if (det_ == kPeak) {
                const float a = std::fabs(s);
                env_[c] = flushDenorm(a > env_[c] ? a : env_[c] * peakRel_);
                level[c] = env_[c] > 1e-6f ? 20.0 * std::log10((double)env_[c]) : kFloorDb;
            } else {
                env_[c] = flushDenorm(env_[c] + rmsA_ * (s * s - env_[c]));
                level[c] = env_[c] > 1e-12f ? 10.0 * std::log10((double)env_[c]) + 3.01 : kFloorDb; // sine-peak equivalent
            }
        }
        if (link_ && nch == 2) level[0] = std::max(level[0], level[1]);
        for (int k = 0; k < nState; ++k) {
            const double L = level[k];
            // hysteresis + hold state
            if (!open_[k]) {
                if (L > thr_) { open_[k] = true; hold_[k] = holdSamples_; ++transitions_; }
            } else if (L > thr_ - hyst_) {
                hold_[k] = holdSamples_;
            } else if (hold_[k] > 0) {
                --hold_[k];
            } else {
                open_[k] = false; ++transitions_;
            }
            // decay-rate estimate for the adaptive release (dB/sample, positive when falling)
            const double prev = slowLevel_[k];
            slowLevel_[k] += slowA_ * (std::max(L, -100.0) - slowLevel_[k]);
            decayRate_[k] += slowA_ * ((prev - slowLevel_[k]) - decayRate_[k]);
            // target reduction: none while open or holding
            const double target = (open_[k] && (L > thr_ - hyst_ || hold_[k] > 0)) ? 0.0 : staticReduction(L, open_[k]);
            double& r = red_[k];
            if (target < r) r = std::max(target, r - attStep_);
            else if (target > r) {
                double step = relStep_;
                if (adaptive_) step = clampv(decayRate_[k], relStep_, 4.0 * relStep_);
                r = std::min(target, r + step);
            }
        }
        for (int c = 0; c < nch; ++c) {
            const int k = link_ ? 0 : c;
            const float gt = red_[k] <= 0.0 ? 1.0f : (float)std::pow(10.0, -red_[k] / 20.0);
            float& g = g_[c];
            g += corner_ * (gt - g);
            if (gt == 1.0f && std::fabs(1.0f - g) < 1e-6f) g = 1.0f; // exact unity when open
            g = flushDenorm(g);
            const float x = b.ch[c][i];
            b.ch[c][i] = std::isfinite(x) ? x * g : 0.0f;
        }
    }
}

} // namespace mfx
