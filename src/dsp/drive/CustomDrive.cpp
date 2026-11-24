#include "dsp/drive/CustomDrive.h"

#include "dsp/nonlinear/Nonlinear.h"

#include <iterator>

namespace mfx {

static const char* kOsChoices[] = {"1x", "2x", "4x", "8x"};

CustomDrivePhysical customDriveMapping(const CustomDriveKnobs& k) {
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
}

void CustomDrive::onPrepare(const ProcessSpec& spec) {
    os_.prepare(spec.maxBlockSize, spec.numChannels);
    tmp_.assign((size_t)spec.maxBlockSize, 0.0f);
    for (auto& k : knob_) k.reset(spec.sampleRate, 0.03);
    mix_.reset(spec.sampleRate, 0.02);
    level_.reset(spec.sampleRate, 0.02);
    dcOut_.prepare(spec.sampleRate, 5.0);
    fadeLen_ = std::max(1, (int)(0.005 * spec.sampleRate));
}

void CustomDrive::onReset() {
    os_.reset();
    for (auto& b : dryBuf_) std::fill(std::begin(b), std::end(b), 0.0f);
    dryPos_[0] = dryPos_[1] = 0;
    for (Biquad* b : {&dcIn_, &tightHp_, &midPre_, &emph_, &deemph_, &midPost_, &tiltLo_, &tiltHi_, &fizz_}) b->reset();
    dcOut_.reset();
    bias_[0] = bias_[1] = 0.0f;
    fadeIn_ = 0;
}

void CustomDrive::setOversampling(int idx) {
    idx = clampv(idx, 0, 3);
    if (idx == osIndex_ && os_.factorIndex() == idx) return;
    osIndex_ = idx;
    os_.setFactorIndex(idx);
    os_.reset();
    emph_.reset(); deemph_.reset();
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
    phys_ = customDriveMapping(k);
    const double fs = spec_.sampleRate, fsOs = fs * (1 << osIndex_);
    dcIn_.design(FilterType::HighPass1, fs, 20.0);
    tightHp_.design(FilterType::HighPass, fs, phys_.tightHz, phys_.tightQ);
    midPre_.design(FilterType::Peak, fs, phys_.midPreHz, phys_.midPreQ, phys_.midPreDb);
    emph_.design(FilterType::HighShelf1, fsOs, phys_.emphasisHz, 0.7071, phys_.emphasisDb);
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
            // nonlinear section (oversampled)
            float* u = os_.upsample(t, L, c);
            float b = bias_[c];
            const float invC2 = 1.0f / c2_;
            for (int i = 0; i < L * F; ++i) {
                float v = emph_.process(u[i] * gLin_, c);
                v -= b * vth_;
                float y = nl::asymSigmoid(v, p_, vp_, vn_);
                // grid-conduction style bias: positive overdrive charges the coupling state
                const float over = std::min(std::max(0.0f, v / vp_ - 1.0f), 2.0f);
                const float target = depth_ * over;
                b += (target > b ? biasCharge_ : biasRelease_) * (target - b);
                // second soft stage (crunch), unity small-signal slope
                const float s = 1.25f * vp_;
                y = s * nl::algebraicSigmoid(c2_ * y / s, 2.0f) * invC2;
                u[i] = deemph_.process(y, c);
            }
            bias_[c] = flushDenorm(b);
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

} // namespace mfx
