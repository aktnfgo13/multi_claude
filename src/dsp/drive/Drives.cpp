#include "dsp/drive/Drives.h"

#include <iterator>

#include "engine/RtPublisher.h"
#include "presets/Json.h"

namespace mfx {

static const char* kOsChoices[] = {"1x", "2x", "4x", "8x"};

// ---------------------------------------------------------------- OversampledDrive
void OversampledDrive::addCommonParams(float defaultLevelDb, int defaultOs) {
    firstCommon_ = params_.size();
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
    params_.add({"level", "Level", -40, 40, defaultLevelDb, ParamUnit::Decibels});
    ParamInfo os{"oversampling", "Oversampling", 0, 3, (float)defaultOs, ParamUnit::Choice};
    os.choices = kOsChoices; os.numChoices = 4;
    params_.add(os);
}

void OversampledDrive::onPrepare(const ProcessSpec& spec) {
    os_.prepare(spec.maxBlockSize, spec.numChannels);
    tmp_.assign((size_t)spec.maxBlockSize, 0.0f);
    for (auto& k : knobs_) k.reset(spec.sampleRate, 0.03);
    mix_.reset(spec.sampleRate, 0.02);
    level_.reset(spec.sampleRate, 0.02);
    fadeLen_ = std::max(1, (int)(0.005 * spec.sampleRate));
    prepareDsp(spec.sampleRate, spec.sampleRate * (1 << osIndex_));
}

void OversampledDrive::onReset() {
    os_.reset();
    for (auto& b : dryBuf_) std::fill(std::begin(b), std::end(b), 0.0f);
    dryPos_[0] = dryPos_[1] = 0;
    fadeIn_ = 0;
    resetDsp();
}

void OversampledDrive::onParametersChanged(bool immediate) {
    for (int i = 0; i < firstCommon_ && i < kMaxKnobs; ++i) {
        if (immediate) knobs_[i].setImmediate(params_.get(i));
        else knobs_[i].setTarget(params_.get(i));
    }
    const float mix = params_.get(firstCommon_) / 100.0f;
    const float lvl = dbToGain(params_.get(firstCommon_ + 1));
    if (immediate) { mix_.setImmediate(mix); level_.setImmediate(lvl); }
    else { mix_.setTarget(mix); level_.setTarget(lvl); }
    const int os = clampv(params_.getInt(firstCommon_ + 2), 0, 3);
    if (immediate || os != osIndex_ || os_.factorIndex() != os) {
        const bool changed = os != osIndex_;
        osIndex_ = os;
        os_.setFactorIndex(os);
        if (changed || immediate) { os_.reset(); resetDsp(); }
        if (changed && !immediate) fadeIn_ = fadeLen_;
    }
    beginBlock(); // pending resources (e.g. knob LUT) become active before coefficients are computed
    updateDsp(spec_.sampleRate, spec_.sampleRate * (1 << osIndex_));
    float om, ol;
    if (immediate && overrideMixLevel(om, ol)) { mix_.setImmediate(om); level_.setImmediate(ol); }
}

void OversampledDrive::onProcess(AudioBlock& block) {
    const int lat = Oversampler::latencyFor(osIndex_);
    const int F = 1 << osIndex_;
    const double fs = spec_.sampleRate, fsOs = fs * F;
    beginBlock();
    for (int start = 0; start < block.numSamples; start += kControlBlock) {
        const int L = std::min(kControlBlock, block.numSamples - start);
        bool smoothing = false;
        for (int i = 0; i < firstCommon_; ++i)
            if (knobs_[i].isSmoothing()) { knobs_[i].skip(L); smoothing = true; }
        if (smoothing) updateDsp(fs, fsOs);
        float om, ol;
        if (overrideMixLevel(om, ol)) { mix_.setTarget(om); level_.setTarget(ol); }
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
            for (int i = 0; i < L; ++i) t[i] = pre(x[i], c);
            float* u = os_.upsample(t, L, c);
            for (int i = 0; i < L * F; ++i) u[i] = nonlinear(u[i], c);
            os_.downsample(t, L, c);
            for (int i = 0; i < L; ++i) {
                const float v = post(t[i], c) * fadeR[i];
                float* db = dryBuf_[c];
                db[dryPos_[c]] = x[i];
                const float d = db[(dryPos_[c] - lat) & (kDryLen - 1)];
                dryPos_[c] = (dryPos_[c] + 1) & (kDryLen - 1);
                float out = (d + mixR[i] * (v - d)) * lvlR[i];
                x[i] = std::isfinite(out) ? out : 0.0f;
            }
        }
    }
}

// ---------------------------------------------------------------- CleanBoost
CleanBoost::CleanBoost() {
    params_.add({"gain", "Gain", 0, 24, 6, ParamUnit::Decibels});
    params_.add({"low_cut", "Low Cut", 20, 400, 20, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"bright", "Bright", -6, 6, 0, ParamUnit::Decibels});
}
void CleanBoost::onPrepare(const ProcessSpec& spec) { gain_.reset(spec.sampleRate, 0.02); }
void CleanBoost::onParametersChanged(bool immediate) {
    const float g = dbToGain(params_.get(0));
    if (immediate) gain_.setImmediate(g); else gain_.setTarget(g);
    lowCut_.design(FilterType::HighPass, spec_.sampleRate, params_.get(1), 0.7071);
    bright_.design(FilterType::HighShelf, spec_.sampleRate, 2500.0, 0.7071, params_.get(2));
}
void CleanBoost::onProcess(AudioBlock& b) {
    for (int i = 0; i < b.numSamples; ++i) {
        const float g = gain_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            float v = bright_.process(lowCut_.process(b.ch[c][i], c), c) * g;
            // Transparent below 0 dBFS; gentle ceiling above (protects downstream stages)
            const float a = std::fabs(v);
            if (a > 1.0f) v = std::copysign(1.0f + nl::fastTanh(a - 1.0f) * 0.5f, v);
            b.ch[c][i] = v;
        }
    }
}

// ---------------------------------------------------------------- Overdrive
Overdrive::Overdrive() {
    params_.add({"drive", "Drive", 0, 100, 50, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 50, ParamUnit::Percent});
    addCommonParams(-6.0f, 2);
}
void Overdrive::resetDsp() {
    for (Biquad* b : {&clipHp_, &clipLp_, &toneLp_, &toneShelf_}) b->reset();
}
void Overdrive::updateDsp(double fs, double fsOs) {
    const double d = knob(0) / 100.0, t = knob(1) / 100.0;
    // non-inverting gain 1 + (51k + 500k*d)/4.7k, clipping path high-passed at 720 Hz (4.7k/0.047uF)
    gain_ = (float)((51e3 + 500e3 * d * d) / 4.7e3);
    clipHp_.design(FilterType::HighPass1, fsOs, 720.0);
    clipLp_.design(FilterType::LowPass1, fsOs, 6000.0 - 3000.0 * d); // feedback cap (51pF-ish) rolls off with gain
    toneLp_.design(FilterType::LowPass1, fs, 700.0 * std::pow(6000.0 / 700.0, t));
    toneShelf_.design(FilterType::HighShelf, fs, 2000.0, 0.7071, -3.0 + 6.0 * t);
}
float Overdrive::pre(float x, int) { return x; }
float Overdrive::nonlinear(float u, int ch) {
    const float hp = clipHp_.process(u, ch);
    // diodes in feedback: the *excess* gain is limited logarithmically (asinh law)
    const float clipped = nl::diodeFeedback(gain_ * hp, 0.06f);
    return clipLp_.process(u + clipped, ch);
}
float Overdrive::post(float v, int ch) { return toneShelf_.process(toneLp_.process(v, ch), ch); }

// ---------------------------------------------------------------- Distortion
Distortion::Distortion() {
    params_.add({"distortion", "Distortion", 0, 100, 50, ParamUnit::Percent});
    params_.add({"filter", "Filter", 0, 100, 40, ParamUnit::Percent});
    addCommonParams(-10.0f, 2);
}
void Distortion::prepareDsp(double, double) {}
void Distortion::resetDsp() {
    for (auto& c : clip_) c.reset();
    for (Biquad* b : {&inHp_, &toneA_, &toneB_, &outLp_}) b->reset();
}
void Distortion::updateDsp(double fs, double fsOs) {
    const double d = knob(0) / 100.0, f = knob(1) / 100.0;
    gain_ = (float)dbToGainD(6.0 + 50.0 * std::pow(d, 1.2));
    nl::DiodeParams dp; // 1N4148-like
    if (fsOs != lastFsOs_) { for (auto& c : clip_) c.prepare(fsOs); lastFsOs_ = fsOs; }
    for (auto& c : clip_) c.setParams(1000.0, 10e-9, dp);
    inHp_.design(FilterType::HighPass1, fs, 60.0);
    // "filter" knob: higher = darker (like a variable RC low-pass after the clipper)
    const double fc = 12000.0 * std::pow(450.0 / 12000.0, f);
    toneA_.design(FilterType::LowPass1, fs, fc);
    toneB_.design(FilterType::LowPass1, fs, std::min(fs * 0.45, fc * 3.0));
    outLp_.design(FilterType::LowPass, fs, 9000.0, 0.7071);
    outScale_ = 1.0f / 0.7f;
}
float Distortion::pre(float x, int ch) { return inHp_.process(x, ch); }
float Distortion::nonlinear(float u, int ch) {
    // op-amp with +/-4.5 V rails, then RC + diode pair to ground (state-space ODE, Newton)
    double v = (double)(u * gain_);
    v = 4.5 * std::tanh(v / 4.5);
    return (float)clip_[ch].process(v);
}
float Distortion::post(float v, int ch) {
    return outLp_.process(toneB_.process(toneA_.process(v, ch), ch), ch) * outScale_;
}

// ---------------------------------------------------------------- Fuzz
Fuzz::Fuzz() {
    params_.add({"fuzz", "Fuzz", 0, 100, 70, ParamUnit::Percent});
    params_.add({"bias", "Bias", 0, 100, 60, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 50, ParamUnit::Percent});
    addCommonParams(-12.0f, 2);
}
void Fuzz::resetDsp() {
    for (Biquad* b : {&inHp_, &interHp_, &toneLp_, &toneHp_}) b->reset();
    sag_[0] = sag_[1] = 0.0f;
}
void Fuzz::updateDsp(double fs, double fsOs) {
    const double fz = knob(0) / 100.0, bi = knob(1) / 100.0, t = knob(2) / 100.0;
    gain_ = (float)dbToGainD(12.0 + 42.0 * fz);
    bias_ = (float)(0.15 + 0.85 * bi);       // positive swing available before clipping
    gate_ = (float)(0.06 * (1.0 - bi) * (1.0 - bi)); // starved bias -> dead zone (sputter)
    blend_ = (float)t;
    inHp_.design(FilterType::HighPass1, fs, 90.0);
    interHp_.design(FilterType::HighPass1, fsOs, 40.0);
    toneLp_.design(FilterType::LowPass1, fs, 900.0);
    toneHp_.design(FilterType::HighPass1, fs, 1100.0);
    sagCoef_ = 1.0f - onePoleCoeff(0.08, fsOs);
}
float Fuzz::pre(float x, int ch) { return inHp_.process(x, ch); }
float Fuzz::nonlinear(float u, int ch) {
    // stage 1: asymmetric, sharp knee, supply "sag" reduces gain on sustained loud input
    float& s = sag_[ch];
    const float g = gain_ * (1.0f - 0.35f * s);
    float v = g * u;
    // dead zone (starved bias gating)
    if (gate_ > 0.0f) v = v - gate_ * nl::fastTanh(v / gate_);
    float y1 = nl::asymSigmoid(v, 6.0f, bias_, 1.0f);
    s += sagCoef_ * (std::min(1.0f, std::fabs(y1)) - s);
    y1 = interHp_.process(y1, ch);
    // stage 2: softer, symmetric
    return nl::algebraicSigmoid(3.0f * y1, 3.0f);
}
float Fuzz::post(float v, int ch) {
    // passive LP/HP blend tone control (mid scoop at the centre position)
    const float lp = toneLp_.process(v, ch), hp = toneHp_.process(v, ch);
    return lp + blend_ * (hp - lp);
}

// ---------------------------------------------------------------- DdspDrive
float KnobLut::eval(int p, float k) const {
    if (n <= 0) return 0.0f;
    if (k <= knob[0] || n == 1) return value[p][0];
    if (k >= knob[n - 1]) return value[p][n - 1];
    int i = 0;
    while (i < n - 2 && k > knob[i + 1]) ++i;
    const float t = (k - knob[i]) / (knob[i + 1] - knob[i]);
    if (logDomain[p]) return std::exp(std::log(value[p][i]) + t * (std::log(value[p][i + 1]) - std::log(value[p][i])));
    return value[p][i] + t * (value[p][i + 1] - value[p][i]);
}

bool KnobLut::fromJsonFile(const std::string& path, KnobLut& out, std::string* error) {
    Json j;
    if (!Json::load(path, j, error)) return false;
    if (j.at("format").asString() != "mfx-knob-lut" || j.at("target").asString() != "ddsp_drive") {
        if (error) *error = "not an mfx-knob-lut for ddsp_drive";
        return false;
    }
    static const char* ids[kNumParams] = {"hpf_hz", "gain_db", "thr_pos", "thr_neg", "knee", "lpf_hz", "out_db", "mix", "level"};
    const auto& kn = j.at("knob").elements();
    out.n = std::min((int)kn.size(), kMaxPoints);
    if (out.n < 1) { if (error) *error = "empty LUT"; return false; }
    for (int i = 0; i < out.n; ++i) {
        out.knob[i] = (float)kn[(size_t)i].asNumber();
        if (i > 0 && out.knob[i] <= out.knob[i - 1]) { if (error) *error = "knob values must increase"; return false; }
    }
    for (int p = 0; p < kNumParams; ++p) {
        const Json& e = j.at("params").at(ids[p]);
        const auto& v = e.at("values").elements();
        if ((int)v.size() < out.n) { if (error) *error = std::string("missing values for ") + ids[p]; return false; }
        out.logDomain[p] = e.at("domain").asString() == "log";
        for (int i = 0; i < out.n; ++i) {
            out.value[p][i] = (float)v[(size_t)i].asNumber();
            if (out.logDomain[p] && out.value[p][i] <= 0.0f) { if (error) *error = "non-positive value in log domain"; return false; }
        }
    }
    return true;
}

struct DdspDrive::LutHolder { RtPublisher<KnobLut> pub; };

DdspDrive::~DdspDrive() = default;

bool DdspDrive::loadLut(const std::string& path, std::string* error) {
    auto l = std::make_unique<KnobLut>();
    if (!KnobLut::fromJsonFile(path, *l, error)) return false;
    lutPath_ = path;
    lut_->pub.publish(std::move(l));
    return true;
}

void DdspDrive::collectGarbage() { lut_->pub.collect(); }

void DdspDrive::beginBlock() {
    if (lut_->pub.hasPending() && lut_->pub.swapIn()) updateDsp(spec_.sampleRate, spec_.sampleRate * (1 << osIndex_));
}

bool DdspDrive::overrideMixLevel(float& mix, float& level) {
    if (!lut_->pub.active()) return false;
    mix = lutMix_;
    level = lutLevel_;
    return true;
}

DdspDrive::DdspDrive() : lut_(std::make_unique<LutHolder>()) {
    params_.add({"hpf_hz", "Pre HPF", 5, 2000, 20, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"gain_db", "Gain", -12, 60, 12, ParamUnit::Decibels});
    params_.add({"thr_pos", "Thr +", 0.01f, 4, 0.5f, ParamUnit::None, ParamScale::Log});
    params_.add({"thr_neg", "Thr -", 0.01f, 4, 0.5f, ParamUnit::None, ParamScale::Log});
    params_.add({"knee", "Knee p", 0.5f, 16, 2, ParamUnit::None, ParamScale::Log});
    params_.add({"lpf_hz", "Post LPF", 300, 22000, 8000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"out_db", "Out Gain", -40, 40, 0, ParamUnit::Decibels});
    params_.add({"knob", "Knob (LUT)", 0, 100, 50, ParamUnit::Percent}); // used only when a knob LUT is loaded
    addCommonParams(0.0f, 2);
}
void DdspDrive::resetDsp() { hp_.reset(); lp_.reset(); }
void DdspDrive::updateDsp(double fs, double) {
    if (const KnobLut* l = lut_->pub.active()) {
        const float k = knob(7);
        hp_.design(FilterType::HighPass1, fs, l->eval(0, k));
        lp_.design(FilterType::LowPass1, fs, l->eval(5, k));
        g_ = (float)dbToGainD(l->eval(1, k));
        vp_ = l->eval(2, k); vn_ = l->eval(3, k); p_ = l->eval(4, k);
        out_ = (float)dbToGainD(l->eval(6, k));
        lutMix_ = clampv(l->eval(7, k) / 100.0f, 0.0f, 1.0f);
        lutLevel_ = dbToGain(l->eval(8, k));
        return;
    }
    hp_.design(FilterType::HighPass1, fs, knob(0));
    lp_.design(FilterType::LowPass1, fs, knob(5));
    g_ = (float)dbToGainD(knob(1));
    vp_ = knob(2); vn_ = knob(3); p_ = knob(4);
    out_ = (float)dbToGainD(knob(6));
}
float DdspDrive::pre(float x, int ch) { return hp_.process(x, ch); }
float DdspDrive::nonlinear(float u, int) { return nl::asymSigmoid(g_ * u, p_, vp_, vn_); }
float DdspDrive::post(float v, int ch) { return lp_.process(v, ch) * out_; }

} // namespace mfx
