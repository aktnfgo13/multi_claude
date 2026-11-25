#include "dsp/drive/CustomDrive.h"

#include "dsp/nonlinear/Nonlinear.h"

#include <iterator>

namespace mfx {

static const char* kOsChoices[] = {"1x", "2x", "4x", "8x"};

CustomDrivePhysical customDriveMappingV1(const CustomDriveKnobs& k) {
    auto n = [](float v) { return clampv((double)v / 100.0, 0.0, 1.0); };
    const double d = n(k.drive), h = n(k.headroom), t = n(k.tone), ti = n(k.tight);
    const double cr = n(k.crunch), g = n(k.grain), m = (n(k.mid) - 0.5) * 2.0;
    CustomDrivePhysical p{};
    p.gainDb = 48.0 * d;
    p.vth = std::pow(10.0, (-24.0 + 24.0 * h) / 20.0);
    p.p = 1.5 * std::pow(10.0 / 1.5, cr);
    const double a = 0.45 * g;
    p.vp = p.vth * (1.0 + a);
    p.vn = p.vth * (1.0 - a);
    p.emphasisDb = 15.0 * g;
    p.emphasisHz = 1500.0;
    p.tightHz = 30.0 * std::pow(900.0 / 30.0, ti);
    p.tightQ = 0.6;
    p.midPreHz = m >= 0 ? 700.0 * std::pow(2.0, 0.5 * m) : 700.0 * std::pow(2.0, 0.5 * m);
    p.midPreDb = 10.0 * m;
    p.midPreQ = m >= 0 ? 0.9 + 0.5 * m : 0.7;
    p.midPostDb = 3.0 * m;
    p.tiltDb = 12.0 * (t - 0.5);
    p.tiltHz = 800.0;
    p.fizzHz = 2500.0 * std::pow(14000.0 / 2500.0, t);
    p.secondStage = 1.0 + 2.0 * cr;
    p.biasDepth = 0.15 + 0.25 * g;
    p.makeupDb = 12.0;
    p.kappa = 0.0;
    return p;
}

// v2: gain staging for real guitar levels (peaks around -7 dBFS), a clipping curve that keeps a
// logarithmic slope above the knee (feedback-clipper-like: playing dynamics survive saturation,
// less brick-wall compression and fewer high-order products), narrower Headroom span with partial
// level compensation (Headroom is not a volume knob), gentler Grain emphasis (aliasing).
static constexpr double kPreClipLpHz = 7200.0;

CustomDrivePhysical customDriveMapping(const CustomDriveKnobs& k) {
    auto n = [](float v) { return clampv((double)v / 100.0, 0.0, 1.0); };
    const double d = n(k.drive), h = n(k.headroom), t = n(k.tone), ti = n(k.tight);
    const double cr = n(k.crunch), g = n(k.grain), m = (n(k.mid) - 0.5) * 2.0;
    CustomDrivePhysical p{};
    p.gainDb = -6.0 + 36.0 * d;
    const double vthDb = -18.0 + 18.0 * h;
    p.vth = std::pow(10.0, vthDb / 20.0);
    p.p = 2.0 + 4.0 * cr * cr;                  // analytic knee family, 2 (soft) .. 6 (hard); default 50 -> 3
    p.kappa = (0.12 + 0.5 * h) * (1.4 - 1.3 * cr);   // crunch 0: wide (dynamic) knee, 100: firm
    p.beta = 0.6 * g;                            // operating-point offset -> even harmonics
    p.vp = p.vth;
    p.vn = p.vth;
    p.emphasisDb = 5.0 * g;
    p.emphasisHz = 1500.0;
    p.tightHz = 30.0 * std::pow(900.0 / 30.0, ti);
    p.tightQ = 0.6;
    p.midPreHz = 700.0 * std::pow(2.0, 0.5 * m);
    p.midPreDb = 10.0 * m;
    p.midPreQ = m >= 0 ? 0.9 + 0.5 * m : 0.7;
    p.midPostDb = 3.0 * m;
    p.tiltDb = 12.0 * (t - 0.5);
    p.tiltHz = 800.0;
    p.fizzHz = 2500.0 * std::pow(14000.0 / 2500.0, t);
    p.secondStage = 1.0 + 0.3 * cr;
    p.biasDepth = 0.1 + 0.2 * g;
    p.makeupDb = -0.5 * vthDb - 0.15 * p.gainDb;
    return p;
}

CustomDrive::CustomDrive() {
    params_.add({"drive", "Drive", 0, 100, 25, ParamUnit::Percent});
    params_.add({"headroom", "Headroom", 0, 100, 50, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 50, ParamUnit::Percent});
    params_.add({"tight", "Tight", 0, 100, 50, ParamUnit::Percent});
    params_.add({"crunch", "Crunch", 0, 100, 50, ParamUnit::Percent});
    params_.add({"grain", "Grain", 0, 100, 50, ParamUnit::Percent});
    params_.add({"mid", "Mid Character", 0, 100, 50, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
    params_.add({"level", "Level", -24, 12, -6, ParamUnit::Decibels});
    ParamInfo os{"oversampling", "Oversampling", 0, 3, 1, ParamUnit::Choice};
    os.choices = kOsChoices; os.numChoices = 4;
    params_.add(os);
    static const char* const kModels[] = {"v1 (legacy)", "v2", "v2 + ADAA"};
    ParamInfo md{"model", "Model", 0, 2, (float)kModelV2, ParamUnit::Choice};
    md.choices = kModels; md.numChoices = 3;
    params_.add(md);
}

void CustomDrive::onPrepare(const ProcessSpec& spec) {
    os_.prepare(spec.maxBlockSize, spec.numChannels);
    tmp_.assign((size_t)spec.maxBlockSize, 0.0f);
    for (auto& k : knob_) k.reset(spec.sampleRate, 0.03);
    mix_.reset(spec.sampleRate, 0.02);
    level_.reset(spec.sampleRate, 0.02);
    dcOut_.prepare(spec.sampleRate, 5.0);
    fadeLen_ = std::max(1, (int)(0.005 * spec.sampleRate));
    nl::kneeAntiderivativeInit(); // ADAA tables (built once, non-real-time)
}

void CustomDrive::onReset() {
    os_.reset();
    for (auto& b : dryBuf_) std::fill(std::begin(b), std::end(b), 0.0f);
    dryPos_[0] = dryPos_[1] = 0;
    for (Biquad* b : {&dcIn_, &tightHp_, &midPre_, &preLp_, &emph_, &deemph_, &midPost_, &tiltLo_, &tiltHi_, &fizz_}) b->reset();
    dcOut_.reset();
    bias_[0] = bias_[1] = 0.0f;
    adInit_[0] = adInit_[1] = false;
    fadeIn_ = 0;
}

void CustomDrive::setOversampling(int idx) {
    idx = clampv(idx, 0, 3);
    if (idx == osIndex_ && os_.factorIndex() == idx) return;
    osIndex_ = idx;
    os_.setFactorIndex(idx);
    os_.reset();
    emph_.reset(); deemph_.reset(); preLp_.reset();
    fadeIn_ = fadeLen_;
}

void CustomDrive::onParametersChanged(bool immediate) {
    const int order[7] = {kDrive, kHeadroom, kTone, kTight, kCrunch, kGrain, kMid};
    for (int i = 0; i < 7; ++i) {
        if (immediate) knob_[i].setImmediate(params_.get(order[i]));
        else knob_[i].setTarget(params_.get(order[i]));
    }
    const float mix = params_.get(kMix) / 100.0f, lvl = dbToGain(params_.get(kLevel));
    if (immediate) { mix_.setImmediate(mix); level_.setImmediate(lvl); }
    else { mix_.setTarget(mix); level_.setTarget(lvl); }
    const int md = clampv(params_.getInt(kModel), 0, 2);
    if (md != model_) {
        model_ = md;
        bias_[0] = bias_[1] = 0.0f;
        adInit_[0] = adInit_[1] = false;
        preLp_.reset();
        if (!immediate) fadeIn_ = fadeLen_;
    }
    const int os = params_.getInt(kOversampling);
    if (immediate) { osIndex_ = -1; }
    setOversampling(os);
    if (immediate) fadeIn_ = 0;
    updateCoefficients();
}

void CustomDrive::updateCoefficients() {
    CustomDriveKnobs k;
    k.drive = knob_[0].current(); k.headroom = knob_[1].current(); k.tone = knob_[2].current();
    k.tight = knob_[3].current(); k.crunch = knob_[4].current(); k.grain = knob_[5].current();
    k.mid = knob_[6].current();
    phys_ = model_ == kModelV1 ? customDriveMappingV1(k) : customDriveMapping(k);
    const double fs = spec_.sampleRate, fsOs = fs * (1 << osIndex_);
    dcIn_.design(FilterType::HighPass1, fs, 20.0);
    tightHp_.design(FilterType::HighPass, fs, phys_.tightHz, phys_.tightQ);
    midPre_.design(FilterType::Peak, fs, phys_.midPreHz, phys_.midPreQ, phys_.midPreDb);
    emph_.design(FilterType::HighShelf1, fsOs, phys_.emphasisHz, 0.7071, phys_.emphasisDb);
    // pre-clip band limit (v2): feedback-cap style roll-off, keeps >7 kHz content from driving the clipper
    preLp_.design(FilterType::LowPass, fsOs, std::min(kPreClipLpHz, 0.45 * fsOs), 0.7071, 0.0);
    deemph_.design(FilterType::HighShelf1, fsOs, phys_.emphasisHz, 0.7071, -phys_.emphasisDb);
    midPost_.design(FilterType::Peak, fs, phys_.midPreHz, phys_.midPreQ, phys_.midPostDb);
    tiltLo_.design(FilterType::LowShelf1, fs, phys_.tiltHz, 0.7071, -0.5 * phys_.tiltDb);
    tiltHi_.design(FilterType::HighShelf1, fs, phys_.tiltHz, 0.7071, 0.5 * phys_.tiltDb);
    fizz_.design(FilterType::LowPass, fs, phys_.fizzHz, 0.7071);
    gLin_ = (float)dbToGainD(phys_.gainDb);
    vth_ = (float)phys_.vth;
    vp_ = (float)phys_.vp;
    vn_ = (float)phys_.vn;
    p_ = (float)phys_.p;
    c2_ = (float)phys_.secondStage;
    depth_ = (float)phys_.biasDepth;
    kappa_ = (float)phys_.kappa;
    invKappa_ = 1.0f / (1.0f + kappa_);
    beta_ = (float)phys_.beta;
    knee_ = nl::kneeWeights(p_);
    if (model_ != kModelV1) {   // core(z) = f(z+beta) - f(beta), f = knee + kappa*asinh; normalise slope at z = 0 to 1 (analytic, double)
        const double bz = beta_, k = kappa_;
        const double b2 = bz * bz, b4 = b2 * b2, b6 = b4 * b2;
        const double f = knee_.w2 * bz / std::sqrt(1.0 + b2) + knee_.w4 * bz / std::pow(1.0 + b4, 0.25)
                       + knee_.w6 * bz / std::pow(1.0 + b6, 1.0 / 6.0) + k * std::asinh(bz);
        const double df = knee_.w2 * std::pow(1.0 + b2, -1.5) + knee_.w4 * std::pow(1.0 + b4, -1.25)
                        + knee_.w6 * std::pow(1.0 + b6, -7.0 / 6.0) + k / std::sqrt(1.0 + b2);
        f0_ = (float)f;
        slope0Inv_ = (float)(1.0 / std::max(df, 1e-3));
    }
    makeup_ = (float)dbToGainD(phys_.makeupDb);
    biasCharge_ = 1.0f - onePoleCoeff(kBiasChargeSec, fsOs);
    biasRelease_ = 1.0f - onePoleCoeff(kBiasReleaseSec, fsOs);
}

void CustomDrive::onProcess(AudioBlock& block) {
    const int lat = Oversampler::latencyFor(osIndex_);
    const int F = 1 << osIndex_;
    for (int start = 0; start < block.numSamples; start += kControlBlock) {
        const int L = std::min(kControlBlock, block.numSamples - start);
        bool smoothing = false;
        for (auto& k : knob_) if (k.isSmoothing()) { k.skip(L); smoothing = true; }
        if (smoothing) updateCoefficients();

        // per-sample smoothed values are identical for all channels: precompute ramps
        float mixR[kControlBlock], lvlR[kControlBlock], fadeR[kControlBlock];
        for (int i = 0; i < L; ++i) {
            mixR[i] = mix_.next();
            lvlR[i] = level_.next();
            if (fadeIn_ > 0) { fadeR[i] = 1.0f - (float)fadeIn_ / (float)fadeLen_; --fadeIn_; }
            else fadeR[i] = 1.0f;
        }

        for (int c = 0; c < block.numChannels; ++c) {
            float* x = block.ch[c] + start;
            float* t = tmp_.data();
            // pre-clip linear section (base rate)
            for (int i = 0; i < L; ++i) {
                float v = dcIn_.process(x[i], c);
                v = tightHp_.process(v, c);
                t[i] = midPre_.process(v, c);
            }
            const bool diag = diagOn_.load(std::memory_order_relaxed);
            float dPk = 0, dSq = 0, dZ = 0, dY = 0, dLin = 0;
            if (diag) for (int i = 0; i < L; ++i) { const float a = std::fabs(x[i]); dPk = std::max(dPk, a); dSq += x[i] * x[i]; }
            // nonlinear section (oversampled)
            float* u = os_.upsample(t, L, c);
            if (model_ == kModelV1) processNonlinearV1(u, L * F, c);
            else if (model_ == kModelV2Adaa) processNonlinearAdaa(u, L * F, c);
            else processNonlinearV2(u, L * F, c, diag, dZ, dY, dLin);
            if (diag) {
                auto maxStore = [](std::atomic<float>& a, float v) { if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed); };
                maxStore(dInPeak_, dPk);
                dInSq_.store(dInSq_.load(std::memory_order_relaxed) + dSq, std::memory_order_relaxed);
                dInN_.store(dInN_.load(std::memory_order_relaxed) + (float)L, std::memory_order_relaxed);
                maxStore(dClipIn_, dZ);
                maxStore(dClipOut_, dY);
                maxStore(dLin_, dLin);
                const float ap[5] = {gLin_, vth_, makeup_, kappa_, p_};
                for (int k = 0; k < 5; ++k) dApplied_[k].store(ap[k], std::memory_order_relaxed);
            }
            os_.downsample(t, L, c);
            // post section (base rate) + mix with latency aligned dry
            for (int i = 0; i < L; ++i) {
                float v = dcOut_.process(t[i], c);
                v = midPost_.process(v, c);
                v = tiltLo_.process(v, c);
                v = tiltHi_.process(v, c);
                v = fizz_.process(v, c) * makeup_;
                float* db = dryBuf_[c];
                db[dryPos_[c]] = x[i];
                const float d = db[(dryPos_[c] - lat) & (kDryLen - 1)];
                dryPos_[c] = (dryPos_[c] + 1) & (kDryLen - 1);
                x[i] = (d + mixR[i] * (v * fadeR[i] - d)) * lvlR[i];
            }
        }
    }
}

CustomDrive::Diagnostics CustomDrive::readDiagnostics(bool reset) noexcept {
    auto db = [](float v) { return v > 1e-9f ? 20.0f * std::log10(v) : -180.0f; };
    Diagnostics d{};
    const float n = dInN_.load(std::memory_order_relaxed);
    d.inPeakDb = db(dInPeak_.load(std::memory_order_relaxed));
    d.inRmsDb = n > 0 ? db(std::sqrt(dInSq_.load(std::memory_order_relaxed) / n)) : -180.0f;
    float ap[5];
    for (int k = 0; k < 5; ++k) ap[k] = dApplied_[k].load(std::memory_order_relaxed);
    d.clipInPeakOverVthDb = db(dClipIn_.load(std::memory_order_relaxed)) - db(ap[1]);
    d.clipOutPeakDb = db(dClipOut_.load(std::memory_order_relaxed));
    d.gainReductionDb = db(dClipOut_.load(std::memory_order_relaxed)) - db(dLin_.load(std::memory_order_relaxed));
    d.appliedDriveDb = db(ap[0]);
    d.appliedVthDb = db(ap[1]);
    d.appliedMakeupDb = db(ap[2]);
    d.appliedKappa = ap[3];
    d.appliedKnee = ap[4];
    if (reset) for (auto* a : {&dInPeak_, &dInSq_, &dInN_, &dClipIn_, &dClipOut_, &dLin_}) a->store(0.0f, std::memory_order_relaxed);
    return d;
}

} // namespace mfx

namespace mfx {

// v1 (v0.1): asymmetric |z|^p sigmoid, no pre-clip LPF, 2nd stage at 1.25 Vp. Kept bit-exact.
void CustomDrive::processNonlinearV1(float* u, int n, int c) {
    float b = bias_[c];
    const float invC2 = 1.0f / c2_;
    for (int i = 0; i < n; ++i) {
        float v = emph_.process(u[i] * gLin_, c);
        v -= b * vth_;
        float y = nl::asymSigmoid(v, p_, vp_, vn_);
        const float over = std::min(std::max(0.0f, v / vp_ - 1.0f), 2.0f);
        const float target = depth_ * over;
        b += (target > b ? biasCharge_ : biasRelease_) * (target - b);
        const float s = 1.25f * vp_;
        y = s * nl::algebraicSigmoid(c2_ * y / s, 2.0f) * invC2;
        u[i] = deemph_.process(y, c);
    }
    bias_[c] = flushDenorm(b);
}

// v2 (v0.2, default): unchanged code path.
void CustomDrive::processNonlinearV2(float* u, int n, int c, bool diag, float& dZ, float& dY, float& dLin) {
    float b = bias_[c];
    const float invC2 = 1.0f / c2_;
    for (int i = 0; i < n; ++i) {
        float v = emph_.process(preLp_.process(u[i] * gLin_, c), c);
        v -= b * vth_;
        const float z = v / vth_ + beta_;
        // analytic knee + logarithmic residual slope (kappa), operating point beta, unity small-signal slope
        float y = vth_ * (nl::kneeCurve(z, knee_) + kappa_ * std::asinh(z) - f0_) * slope0Inv_;
        if (diag) { dZ = std::max(dZ, std::fabs(v)); }
        // grid-conduction style bias: positive overdrive charges the coupling state
        const float over = std::min(std::max(0.0f, v / vp_ - 1.0f), 2.0f);
        const float target = depth_ * over;
        b += (target > b ? biasCharge_ : biasRelease_) * (target - b);
        // second soft stage (crunch), unity small-signal slope
        const float s = 2.0f * vp_;
        y = s * nl::kneeCurve(c2_ * y / s, nl::KneeWeights{}) * invC2;
        if (diag) { dY = std::max(dY, std::fabs(y)); dLin = std::max(dLin, std::fabs(v)); }
        u[i] = deemph_.process(y, c);
    }
    bias_[c] = flushDenorm(b);
}

// v2 + ADAA (v0.4): same curves as v2, each clipping stage replaced by its 1st-order antiderivative
// anti-aliased form  y[n] = (G(x[n]) - G(x[n-1])) / (x[n] - x[n-1])  (midpoint fallback for tiny steps).
// Adds half a (oversampled) sample of group delay per stage. Double precision for the difference quotient.
void CustomDrive::processNonlinearAdaa(float* u, int n, int c) {
    float b = bias_[c];
    const nl::KneeWeights k2{};
    const double kap = kappa_, vth = vth_, s = 2.0 * vp_, c2 = c2_;
    auto G1 = [&](double z) { // antiderivative of knee(z) + kappa*asinh(z)
        return nl::kneeAntiderivative(z, knee_) + kap * (z * std::asinh(z) - std::sqrt(1.0 + z * z));
    };
    auto g1 = [&](double z) { return nl::kneeCurveD(z, knee_) + kap * std::asinh(z); };
    double zp = adZ_[c], Gp = adG_[c], wp = adW_[c], G2p = adG2_[c];
    if (!adInit_[c]) { // start from the current operating point (no transient)
        zp = beta_; Gp = G1(zp); wp = 0.0; G2p = std::sqrt(1.0);
        adInit_[c] = true;
    }
    constexpr double kEps = 1e-6;
    for (int i = 0; i < n; ++i) {
        float v = emph_.process(preLp_.process(u[i] * gLin_, c), c);
        v -= b * vth_;
        const double z = (double)v / vth + beta_;
        const double G = G1(z);
        const double dz = z - zp;
        const double f = std::fabs(dz) > kEps ? (G - Gp) / dz : g1(0.5 * (z + zp));
        zp = z; Gp = G;
        double y = vth * (f - f0_) * slope0Inv_;
        const float over = std::min(std::max(0.0f, v / vp_ - 1.0f), 2.0f);
        const float target = depth_ * over;
        b += (target > b ? biasCharge_ : biasRelease_) * (target - b);
        // second stage: s/c2 * K2(w), w = c2*y/s, antiderivative sqrt(1+w^2)
        const double w = c2 * y / s;
        const double G2 = std::sqrt(1.0 + w * w);
        const double dw = w - wp;
        const double k = std::fabs(dw) > kEps ? (G2 - G2p) / dw : nl::kneeCurveD(0.5 * (w + wp), k2);
        wp = w; G2p = G2;
        y = s * k / c2;
        u[i] = deemph_.process((float)y, c);
    }
    adZ_[c] = zp; adG_[c] = Gp; adW_[c] = wp; adG2_[c] = G2p;
    bias_[c] = flushDenorm(b);
}

} // namespace mfx
