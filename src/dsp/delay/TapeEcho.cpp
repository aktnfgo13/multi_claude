#include "dsp/delay/TapeEcho.h"

#include "dsp/modulation/Modulation.h"
#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

namespace {
const char* kHeadChoices[] = {"Head 1", "Heads 1+2", "Heads 1+2+3"};
constexpr double kRefTimeMs = 300.0;   // speed at which the playback filters have their nominal values
constexpr double kWowMax = 0.004;      // peak relative speed deviation at Wow 100 %
constexpr double kFlutterMax = 0.0012; // at Flutter 100 %
constexpr double kDriftMax = 0.0015;   // irregular drift at Modulation 100 %
constexpr float kPreEmphDb = 9.0f;
} // namespace

TapeEcho::TapeEcho() {
    params_.add({"time_ms", "Time", (float)kMinTimeMs, (float)kMaxTimeMs, 350, ParamUnit::Milliseconds, ParamScale::Log});
    ParamInfo s{"sync", "Sync", 0, (float)(kNumSyncChoices - 1), 0, ParamUnit::Choice};
    s.choices = kSyncChoices; s.numChoices = kNumSyncChoices; params_.add(s);
    params_.add({"feedback", "Feedback", 0, 110, 40, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 35, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 50, ParamUnit::Percent});
    params_.add({"drive", "Drive", 0, 100, 40, ParamUnit::Percent});
    params_.add({"age", "Age", 0, 100, 30, ParamUnit::Percent});
    params_.add({"wow", "Wow", 0, 100, 25, ParamUnit::Percent});
    params_.add({"flutter", "Flutter", 0, 100, 20, ParamUnit::Percent});
    params_.add({"modulation", "Modulation", 0, 100, 20, ParamUnit::Percent});
    ParamInfo h{"heads", "Heads", 0, 2, 0, ParamUnit::Choice}; h.choices = kHeadChoices; h.numChoices = 3; params_.add(h);
    params_.add({"spread", "Spread", 0, 100, 30, ParamUnit::Percent});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}

double TapeEcho::effectiveTimeMs() const {
    const int sync = params_.getInt(kSync);
    if (sync > 0) return clampv(60000.0 / bpm_ * syncDivisionBeats(sync), kMinTimeMs, kMaxTimeMs);
    return params_.get(kTime);
}

size_t TapeEcho::memoryBytes() const { return tape_.bytes() + hist_.size() * sizeof(double); }

float TapeEcho::noise() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (float)((double)rng_ / 2147483648.0 - 1.0);
}

void TapeEcho::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    const double maxSec = kMaxHeads * kMaxTimeMs * 0.001 * (1.0 + 2.0 * (kWowMax + kFlutterMax + kDriftMax)) + 0.05;
    tape_.prepare((int)(maxSec * fs) + 8);
    int hs = 1;
    while (hs < (int)(maxSec * fs / kHistDecim) + 8) hs <<= 1;
    hist_.assign((size_t)hs, 0.0);
    histMask_ = hs - 1;
    motorA_ = 1.0 - std::exp(-1.0 / (0.12 * fs)); // motor / capstan inertia
    wowInc_ = 0.55 / fs; flutInc_ = 9.3 / fs;
    aWow_ = (float)(1.0 - std::exp(-kTwoPi * 1.2 / fs));
    aFl_ = (float)(1.0 - std::exp(-kTwoPi * 55.0 / fs));
    aFlHp_ = (float)(1.0 - std::exp(-kTwoPi * 30.0 / fs));
    aDr_ = (float)(1.0 - std::exp(-kTwoPi * 0.25 / fs));
    // normalise the filtered noise sources to unit RMS (measured once here, deterministic)
    {
        uint32_t keep = rng_;
        rng_ = 99;
        double sw = 0, sf = 0, sd = 0; float w1 = 0, w2 = 0, f1 = 0, f2 = 0, d1 = 0, d2 = 0;
        const int n = (int)(fs * 8.0);
        for (int i = 0; i < n; ++i) {
            const float u = noise();
            w1 += aWow_ * (u - w1); w2 += aWow_ * (w1 - w2);
            f1 += aFl_ * (u - f1); f2 += aFlHp_ * (f1 - f2);
            d1 += aDr_ * (u - d1); d2 += aDr_ * (d1 - d2);
            if (i > n / 4) { sw += (double)w2 * w2; sf += (double)(f1 - f2) * (f1 - f2); sd += (double)d2 * d2; }
        }
        const double m = n - n / 4 - 1;
        gWow_ = (float)(1.0 / std::sqrt(sw / m + 1e-30));
        gFl_ = (float)(1.0 / std::sqrt(sf / m + 1e-30));
        gDr_ = (float)(1.0 / std::sqrt(sd / m + 1e-30));
        rng_ = keep;
    }
    preEmph_.design(FilterType::HighShelf, fs, 3000.0, 0.7071, kPreEmphDb);
    deEmph_.design(FilterType::HighShelf, fs, 3000.0, 0.7071, -kPreEmphDb);
    lowCut_.design(FilterType::HighPass1, fs, 70.0);
    fbHp_.design(FilterType::HighPass1, fs, 30.0);
    mix_.reset(fs, 0.03); fb_.reset(fs, 0.03); level_.reset(fs, 0.03);
}

void TapeEcho::onReset() {
    tape_.reset();
    for (auto* f : {&preEmph_, &deEmph_, &gapLp_, &headBump_, &lowCut_, &toneLo_, &toneHi_, &fbHp_}) f->reset();
    rng_ = 0x2545F491u;
    nWow1_ = nWow2_ = nFl1_ = nFl2_ = nDr1_ = nDr2_ = 0.0f;
    wowPh_ = 0.0; flutPh_ = 0.25; eps_ = 0.0;
    // tape speed in gap units per sample: one gap = one head-1 delay
    vTarget_ = 1.0 / (effectiveTimeMs() * 0.001 * spec_.sampleRate);
    vBase_ = vTarget_;
    // history consistent with constant speed: P(t) = (t - n) * v
    const int hs = histMask_ + 1;
    n_ = (int64_t)hs * kHistDecim;
    for (int j = 0; j < hs; ++j) {
        const int64_t t = n_ - (int64_t)(hs - 1 - j) * kHistDecim;
        hist_[(size_t)((t / kHistDecim) & histMask_)] = (double)(t - n_) * vBase_;
    }
    pos_ = 0.0;
    for (int k = 0; k < kMaxHeads; ++k) tau_[k] = (k + 1) / vBase_;
    filterSpeed_ = 0.0;
    updateSpeedFilters(vBase_);
}

void TapeEcho::onParametersChanged(bool immediate) {
    const double fs = spec_.sampleRate;
    vTarget_ = 1.0 / (effectiveTimeMs() * 0.001 * fs);
    age_ = params_.get(kAge) / 100.0f;
    const float drive = params_.get(kDrive) / 100.0f;
    satGain_ = dbToGain(18.0f * drive) * (1.0f + 0.5f * age_);
    satBias_ = 0.06f + 0.18f * age_;
    const float th = std::tanh(satBias_);
    satOff_ = nl::fastTanh(satBias_);
    satNorm_ = 1.0f / (satGain_ * (1.0f - th * th)); // unity small-signal gain
    wowAmt_ = params_.get(kWow) / 100.0f;
    flutAmt_ = params_.get(kFlutter) / 100.0f;
    modAmt_ = params_.get(kModulation) / 100.0f;
    const float t = (params_.get(kTone) - 50.0f) / 50.0f;
    toneLo_.design(FilterType::LowShelf, fs, 300.0, 0.7071, -6.0 * t);
    toneHi_.design(FilterType::HighShelf, fs, 2500.0, 0.7071, 6.0 * t);
    heads_ = params_.getInt(kHeads) + 1;
    static const float norm[3] = {1.0f, 0.75f, 0.62f};
    const float sp = params_.get(kSpread) / 100.0f;
    static const float pan[3][3] = {{0, 0, 0}, {1, -1, 0}, {1, -1, 0}};
    for (int k = 0; k < kMaxHeads; ++k) {
        headGain_[k] = k < heads_ ? norm[heads_ - 1] : 0.0f;
        panL_[k] = 1.0f + sp * pan[heads_ - 1][k];
        panR_[k] = 1.0f - sp * pan[heads_ - 1][k];
    }
    filterSpeed_ = 0.0; // force playback filter update (Age changed)
    const float fb = params_.get(kFeedback) / 100.0f, mx = params_.get(kMix) / 100.0f;
    const float lv = dbToGain(params_.get(kLevel));
    if (immediate) { fb_.setImmediate(fb); mix_.setImmediate(mx); level_.setImmediate(lv); }
    else { fb_.setTarget(fb); mix_.setTarget(mx); level_.setTarget(lv); }
}

void TapeEcho::updateSpeedFilters(double v) {
    const double fs = spec_.sampleRate;
    const double r = v * kRefTimeMs * 0.001 * fs; // speed relative to the reference speed
    const double gap = clampv(7000.0 * std::pow(r, 0.6) * (1.0 - 0.55 * age_), 1200.0, std::min(18000.0, 0.45 * fs));
    gapLp_.design(FilterType::LowPass, fs, gap, 0.6);
    const double bump = clampv(110.0 * std::pow(r, 0.8), 35.0, 400.0);
    headBump_.design(FilterType::Peak, fs, bump, 1.2, 1.5 + 2.0 * age_);
    filterSpeed_ = v;
}

double TapeEcho::posAt(double t) const {
    const double last = (double)((n_ / kHistDecim) * kHistDecim);
    if (t >= last) {
        const double pl = hist_[(size_t)((n_ / kHistDecim) & histMask_)];
        return (double)n_ > last ? pl + (pos_ - pl) * (t - last) / ((double)n_ - last) : pos_;
    }
    const double q = t / kHistDecim;
    const int64_t j = (int64_t)std::floor(q);
    const double f = q - (double)j;
    const double a = hist_[(size_t)(j & histMask_)], b = hist_[(size_t)((j + 1) & histMask_)];
    return a + f * (b - a);
}

double TapeEcho::speedAt(double t) const {
    const double last = (double)((n_ / kHistDecim) * kHistDecim);
    if (t >= last) {
        if ((double)n_ <= last) return vBase_ * (1.0 + eps_);
        return (pos_ - hist_[(size_t)((n_ / kHistDecim) & histMask_)]) / ((double)n_ - last);
    }
    const int64_t j = (int64_t)std::floor(t / kHistDecim);
    return (hist_[(size_t)((j + 1) & histMask_)] - hist_[(size_t)(j & histMask_)]) / kHistDecim;
}

void TapeEcho::onProcess(AudioBlock& b) {
    const bool stereo = b.numChannels == 2;
    const double maxTau = (double)(tape_.bytes() / sizeof(float)) - 8.0;
    const float ageScale = 1.0f + 0.5f * age_;
    const double wA = kWowMax * wowAmt_ * ageScale, fA = kFlutterMax * flutAmt_ * ageScale;
    const double periodicW = 1.0 - 0.6 * modAmt_, randomW = 0.35 + 0.9 * modAmt_;
    for (int i = 0; i < b.numSamples; ++i) {
        // ---- transport
        vBase_ += motorA_ * (vTarget_ - vBase_);
        const float u = noise();
        nWow1_ += aWow_ * (u - nWow1_); nWow2_ += aWow_ * (nWow1_ - nWow2_);
        nFl1_ += aFl_ * (u - nFl1_); nFl2_ += aFlHp_ * (nFl1_ - nFl2_);
        nDr1_ += aDr_ * (u - nDr1_); nDr2_ += aDr_ * (nDr1_ - nDr2_);
        const double wow = periodicW * std::sin(kTwoPi * wowPh_) + randomW * gWow_ * nWow2_;
        const double flut = periodicW * (0.75 * std::sin(kTwoPi * flutPh_) + 0.3 * std::sin(kTwoPi * 2.0 * flutPh_ + 1.0))
                          + randomW * 0.4 * gFl_ * (nFl1_ - nFl2_);
        eps_ = wA * wow + fA * flut + kDriftMax * modAmt_ * gDr_ * nDr2_;
        wowPh_ += wowInc_ * (1.0 + 0.15 * modAmt_ * gDr_ * nDr2_); wowPh_ -= std::floor(wowPh_);
        flutPh_ += flutInc_; flutPh_ -= std::floor(flutPh_);
        pos_ += vBase_ * (1.0 + eps_);
        ++n_;
        if (n_ % kHistDecim == 0) hist_[(size_t)((n_ / kHistDecim) & histMask_)] = pos_;
        if ((n_ & 31) == 0 && std::fabs(vBase_ - filterSpeed_) > 0.003 * vBase_) updateSpeedFilters(vBase_);

        // ---- playback heads (one Newton step per head keeps P(n) - P(n - tau) = k)
        float hl = 0.0f, hr = 0.0f;
        for (int k = 0; k < heads_; ++k) {
            double& tau = tau_[k];
            const double t = (double)n_ - tau;
            const double f = pos_ - posAt(t) - (double)(k + 1);
            tau = clampv(tau - f / std::max(speedAt(t), 1e-9), 2.0, maxTau);
            const float y = tape_.read((float)tau) * headGain_[k];
            hl += panL_[k] * y; hr += panR_[k] * y;
        }
        // playback chain (channel 0 = L, 1 = R; LTI per channel so per-head filtering is not needed)
        float w[2] = {hl, hr};
        for (int c = 0; c < 2; ++c) {
            float v = deEmph_.process(w[c], c);
            v = headBump_.process(gapLp_.process(v, c), c);
            v = lowCut_.process(v, c);
            w[c] = toneHi_.process(toneLo_.process(v, c), c);
        }
        const float fb = fb_.next(), mix = mix_.next(), lvl = level_.next();
        // ---- record
        const float inL = b.ch[0][i], inR = stereo ? b.ch[1][i] : inL;
        const float fbSig = fbHp_.process(0.5f * (w[0] + w[1]), 0) * fb;
        const float rec = preEmph_.process(0.5f * (inL + inR) + fbSig, 0);
        const float sat = (nl::fastTanh(satGain_ * rec + satBias_) - satOff_) * satNorm_;
        tape_.push(flushDenorm(sat));
        // ---- output: dry stays at unity up to 50 % mix, wet reaches unity at 50 %
        const float dg = std::min(1.0f, 2.0f * (1.0f - mix)), wg = std::min(1.0f, 2.0f * mix);
        if (stereo) {
            b.ch[0][i] = lvl * (dg * inL + wg * w[0]);
            b.ch[1][i] = lvl * (dg * inR + wg * w[1]);
        } else {
            b.ch[0][i] = lvl * (dg * inL + wg * 0.5f * (w[0] + w[1]));
        }
    }
}

} // namespace mfx
