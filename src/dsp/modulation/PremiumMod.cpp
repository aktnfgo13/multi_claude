#include "dsp/modulation/PremiumMod.h"

#include "dsp/modulation/Modulation.h" // kSyncChoices / syncDivisionBeats
#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

namespace {
const char* kOnOffShapes[] = {"Sine", "Triangle"};
const char* kVibShapes[] = {"Sine", "Triangle", "Asymmetric"};
void addSync(ParameterSet& ps) {
    ParamInfo s{"sync", "Sync", 0, (float)(kNumSyncChoices - 1), 0, ParamUnit::Choice};
    s.choices = kSyncChoices; s.numChoices = kNumSyncChoices; ps.add(s);
}
double syncedRate(const ParameterSet& ps, int rateIdx, int syncIdx, double bpm) {
    const int s = ps.getInt(syncIdx);
    return s > 0 ? (bpm / 60.0) / syncDivisionBeats(s) : ps.get(rateIdx);
}
template <class S> void setS(S& s, float v, bool imm) { if (imm) s.setImmediate(v); else s.setTarget(v); }
inline float tptG(double f, double fs) { const double g = std::tan(kPi * clampv(f, 5.0, 0.45 * fs) / fs); return (float)(g / (1.0 + g)); }
inline float safe(float x) { return std::isfinite(x) ? x : 0.0f; }
} // namespace

// =============================================================================================== Chorus
PremiumChorus::PremiumChorus() {
    params_.add({"rate_hz", "Rate", 0.05f, 10, 0.6f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 50, ParamUnit::Percent});
    params_.add({"delay_ms", "Delay", 3, 30, 10, ParamUnit::Milliseconds});
    params_.add({"feedback", "Feedback", -70, 70, 0, ParamUnit::Percent});
    params_.add({"tone_hz", "Tone", 1000, 18000, 9000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    params_.add({"spread_deg", "Stereo Phase", 0, 180, 90, ParamUnit::None});
    static const char* kVoices[] = {"1", "2", "3"};
    ParamInfo v{"voices", "Voices", 0, 2, 0, ParamUnit::Choice}; v.choices = kVoices; v.numChoices = 3; params_.add(v);
    static const char* kChar[] = {"Clean", "BBD"};
    ParamInfo c{"character", "Character", 0, 1, 1, ParamUnit::Choice}; c.choices = kChar; c.numChoices = 2; params_.add(c);
    params_.add({"drift", "Drift", 0, 100, 20, ParamUnit::Percent});
    ParamInfo sh{"shape", "LFO Shape", 0, 1, 0, ParamUnit::Choice}; sh.choices = kOnOffShapes; sh.numChoices = 2; params_.add(sh);
    addSync(params_);
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumChorus::onPrepare(const ProcessSpec& spec) {
    line_.prepare((int)(0.045 * spec.sampleRate) + 8);
    lfo_.prepare(spec.sampleRate); drift_.prepare(spec.sampleRate);
    for (auto* s : {&base_, &swing_, &mix_, &fb_, &level_, &driftAmt_}) s->reset(spec.sampleRate, 0.05);
}
void PremiumChorus::onReset() {
    line_.reset(); lfo_.reset(); drift_.reset(0.37);
    for (auto* f : {&pre1_, &pre2_, &post1_, &post2_}) f->reset();
    fbState_[0] = fbState_[1] = 0.0f;
}
void PremiumChorus::onParametersChanged(bool imm) {
    const double fs = spec_.sampleRate;
    lfo_.setRate(syncedRate(params_, kRate, kSync, bpm_), imm);
    lfo_.setShape(params_.getInt(kShape) == 1 ? ModLfo::kTriangle : ModLfo::kSine);
    drift_.setRate(0.13, imm); drift_.setShape(ModLfo::kRandom);
    spread_ = params_.get(kSpread) / 360.0f;
    voices_ = params_.getInt(kVoices) + 1;
    bbd_ = params_.getInt(kCharacter) == 1;
    const double tone = std::min((double)params_.get(kTone), 0.45 * fs);
    // Clean: gentle 2-pole wet low-pass; BBD: anti-alias + reconstruction pair (4 + 4 poles) around the delay
    post1_.design(FilterType::LowPass, fs, tone, 0.5412); post2_.design(FilterType::LowPass, fs, tone, 1.3066);
    pre1_.design(FilterType::LowPass, fs, tone, 0.5412); pre2_.design(FilterType::LowPass, fs, tone, 1.3066);
    setS(base_, params_.get(kDelay) * 0.001f * (float)fs, imm);
    setS(swing_, params_.get(kDepth) / 100.0f * 0.006f * (float)fs, imm); // up to 6 ms peak-to-peak
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(fb_, params_.get(kFeedback) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
    setS(driftAmt_, params_.get(kDrift) / 100.0f * 0.0015f * (float)fs, imm); // up to 1.5 ms slow wander
}
void PremiumChorus::onProcess(AudioBlock& b) {
    const float maxD = (float)line_.maxDelay() - 2.0f;
    const float vNorm = 1.0f / std::sqrt((float)voices_);
    for (int i = 0; i < b.numSamples; ++i) {
        const float base = base_.next(), swing = swing_.next(), mix = mix_.next(), fb = fb_.next(), lvl = level_.next(), dr = driftAmt_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = safe(b.ch[c][i]);
            float wet = 0.0f;
            for (int v = 0; v < voices_; ++v) {
                const double off = c * spread_ + (double)v / voices_;
                const float m = (float)lfo_.value(off);
                const float d = clampv(base + swing * 0.5f * (1.0f + m) + dr * (float)drift_.value(off * 7.3 + v), 2.0f, maxD);
                wet += line_.read(d, c);
            }
            wet *= vNorm;
            if (bbd_) wet = post2_.process(post1_.process(wet, c), c);
            else wet = post1_.process(wet, c);
            float w = x + nl::fastTanh(fb * wet);
            if (bbd_) w = pre2_.process(pre1_.process(nl::fastTanh(w * 0.8f) * 1.25f, c), c); // chip input: soft limit + AA
            line_.push(flushDenorm(w), c);
            b.ch[c][i] = lvl * (x + mix * (wet - x));
        }
        line_.advance();
        lfo_.advance(); drift_.advance();
    }
}

// =============================================================================================== Vibrato
PremiumVibrato::PremiumVibrato() {
    params_.add({"rate_hz", "Rate", 0.1f, 14, 5, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth_cents", "Depth", 0, 100, 25, ParamUnit::None});
    ParamInfo sh{"shape", "Shape", 0, 2, 0, ParamUnit::Choice}; sh.choices = kVibShapes; sh.numChoices = 3; params_.add(sh);
    params_.add({"skew", "Skew", 10, 90, 35, ParamUnit::Percent});
    params_.add({"drift", "Analog Drift", 0, 100, 10, ParamUnit::Percent});
    params_.add({"stereo", "Stereo Variation", 0, 100, 0, ParamUnit::Percent});
    addSync(params_);
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
}
void PremiumVibrato::onPrepare(const ProcessSpec& spec) {
    baseInt_ = (int)std::lround(0.005 * spec.sampleRate); // centre delay (integer: zero depth = exact delayed copy)
    line_.prepare(2 * baseInt_ + 8);
    lfo_.prepare(spec.sampleRate); drift_.prepare(spec.sampleRate);
    for (auto* s : {&swing_, &mix_, &level_}) s->reset(spec.sampleRate, 0.05);
}
void PremiumVibrato::onReset() { line_.reset(); lfo_.reset(); drift_.reset(0.71); }
void PremiumVibrato::updateSwing() {
    // pitch ratio = 1 - d'(t); with d = A m(phase), max |d'| = A * f * maxSlope  ->  A for the requested peak cents
    const double fs = spec_.sampleRate;
    const double dev = std::pow(2.0, cents_ / 1200.0) - 1.0;
    const double A = dev / (rate_ * lfo_.maxSlope()) * fs;
    swingTarget_ = std::min(A, (double)baseInt_ - 3.0); // limit: swing must stay inside the centre delay
}
void PremiumVibrato::onParametersChanged(bool imm) {
    rate_ = syncedRate(params_, kRate, kSync, bpm_);
    lfo_.setRate(rate_, imm);
    const int sh = params_.getInt(kShape);
    lfo_.setShape(sh == 0 ? ModLfo::kSine : sh == 1 ? ModLfo::kTriangle : ModLfo::kAsym, params_.get(kSkew) / 100.0);
    drift_.setRate(0.4, imm); drift_.setShape(ModLfo::kRandom);
    cents_ = params_.get(kDepthCents);
    stereo_ = params_.get(kStereo) / 100.0f;
    drift_amt_ = params_.get(kDrift) / 100.0f;
    updateSwing();
    setS(swing_, (float)swingTarget_, imm);
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
}
void PremiumVibrato::onProcess(AudioBlock& b) {
    const float base = (float)baseInt_;
    for (int i = 0; i < b.numSamples; ++i) {
        const float A = swing_.next(), mix = mix_.next(), lvl = level_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = safe(b.ch[c][i]);
            line_.push(x, c);
        }
        for (int c = 0; c < b.numChannels; ++c) {
            // drift: slow random variation of the depth (+/- 30 %) and a small phase wander
            const double dv = drift_.value(c * 0.5 * stereo_);
            const float m = (float)lfo_.value(c * 0.25 * stereo_ + 0.05 * drift_amt_ * dv);
            const float d = A == 0.0f ? base : clampv(base + A * (1.0f + 0.3f * drift_amt_ * (float)dv) * m, 2.0f, 2.0f * base);
            // read after push: d = base means exactly base samples ago (integer -> bit-exact copy at zero depth)
            const float wet = line_.read(d, c);
            const float dry = line_.readInt(baseInt_, c);
            b.ch[c][i] = lvl * (dry + mix * (wet - dry));
        }
        line_.advance();
        lfo_.advance(); drift_.advance();
    }
}

// =============================================================================================== Phaser
PremiumPhaser::PremiumPhaser() {
    params_.add({"rate_hz", "Rate", 0.02f, 10, 0.4f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 80, ParamUnit::Percent});
    params_.add({"freq_lo_hz", "Low Freq", 40, 2000, 200, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"freq_hi_hz", "High Freq", 200, 10000, 2500, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"feedback", "Feedback", -95, 95, 40, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    static const char* kSt[] = {"2", "4", "6", "8", "12"};
    ParamInfo s{"stages", "Stages", 0, 4, 2, ParamUnit::Choice}; s.choices = kSt; s.numChoices = 5; params_.add(s);
    params_.add({"stereo_deg", "Stereo Phase", 0, 180, 90, ParamUnit::None});
    params_.add({"character", "Character", 0, 100, 30, ParamUnit::Percent});
    ParamInfo sh{"shape", "LFO Shape", 0, 1, 0, ParamUnit::Choice}; sh.choices = kOnOffShapes; sh.numChoices = 2; params_.add(sh);
    addSync(params_);
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumPhaser::onPrepare(const ProcessSpec& spec) {
    lfo_.prepare(spec.sampleRate);
    sA_ = 1.0 - std::exp(-(double)kCtl / (0.05 * spec.sampleRate));
    for (auto* s : {&mix_, &level_, &fb_}) s->reset(spec.sampleRate, 0.03);
}
void PremiumPhaser::onReset() {
    for (auto& a : ap_) a.reset();
    fbState_[0] = fbState_[1] = 0.0f;
    lfo_.reset(); ctl_ = 0;
    for (int c = 0; c < kMaxChannels; ++c)
        for (int k = 0; k < kMaxStages; ++k) G0_[c][k] = G1_[c][k] = tptG(stageHz(c, k), spec_.sampleRate);
}
double PremiumPhaser::stageHz(int c, int k) const {
    const double m = lfo_.value(c * stereo_);
    const double f = lo_ * std::pow(hi_ / lo_, 0.5 * (1.0 + depth_ * m));
    return f * spreadK_[k];
}
void PremiumPhaser::onParametersChanged(bool imm) {
    static const int st[] = {2, 4, 6, 8, 12};
    stages_ = st[clampv(params_.getInt(kStages), 0, 4)];
    lfo_.setRate(syncedRate(params_, kRate, kSync, bpm_), imm);
    lfo_.setShape(params_.getInt(kShape) == 1 ? ModLfo::kTriangle : ModLfo::kSine);
    tDepth_ = params_.get(kDepth) / 100.0;
    tLogLo_ = std::log((double)params_.get(kFreqLo));
    tLogHi_ = std::log(std::max((double)params_.get(kFreqHi), params_.get(kFreqLo) * 1.05));
    if (imm) { depth_ = tDepth_; lo_ = std::exp(tLogLo_); hi_ = std::exp(tLogHi_); }
    stereo_ = params_.get(kStereo) / 360.0;
    const double ch = params_.get(kCharacter) / 100.0;
    sat_ = ch;
    // analogue stage mismatch (fixed pattern, +/- 12 % at Character 100): spreads the notches slightly
    static const double pat[kMaxStages] = {0.9, 1.3, -0.6, 0.2, -1.1, 0.7, -0.3, 1.0, -0.8, 0.4, -0.2, 0.6};
    for (int k = 0; k < kMaxStages; ++k) spreadK_[k] = (float)(1.0 + 0.12 * ch * pat[k] / 1.3);
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
    setS(fb_, params_.get(kFeedback) / 100.0f, imm);
}
void PremiumPhaser::onProcess(AudioBlock& b) {
    const double fs = spec_.sampleRate;
    const float drive = 1.0f + 2.0f * (float)sat_;
    for (int i = 0; i < b.numSamples; ++i) {
        if (ctl_ == 0) { // control rate: smooth range / depth (log), next coefficient targets, per-sample ramp of G
            depth_ += sA_ * (tDepth_ - depth_);
            lo_ = std::exp(std::log(lo_) + sA_ * (tLogLo_ - std::log(lo_)));
            hi_ = std::exp(std::log(hi_) + sA_ * (tLogHi_ - std::log(hi_)));
        }
        if (ctl_ == 0)
            for (int c = 0; c < b.numChannels; ++c)
                for (int k = 0; k < stages_; ++k) { G0_[c][k] = G1_[c][k]; G1_[c][k] = tptG(stageHz(c, k), fs); }
        const float t = (float)(ctl_ + 1) / (float)kCtl;
        const float mix = mix_.next(), lvl = level_.next(), fb = fb_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = safe(b.ch[c][i]);
            float v = x + fb * fbState_[c];
            for (int k = 0; k < stages_; ++k) v = ap_[k].process(v, G0_[c][k] + t * (G1_[c][k] - G0_[c][k]), c);
            fbState_[c] = flushDenorm(nl::fastTanh(drive * v) / drive); // saturating feedback amplifier
            b.ch[c][i] = lvl * (x + mix * (v - x));
        }
        lfo_.advance();
        ctl_ = (ctl_ + 1) % kCtl;
    }
}

// =============================================================================================== Uni-Vibe
// Four first-order phase-shift stages with very different capacitors (15 nF, 220 nF, 470 pF, 4.7 nF - the
// classic topology, values from public schematics), all driven by ONE light-dependent resistance:
//   LFO (asymmetric phase-shift oscillator) -> lamp drive -> lamp temperature (fast heat / slow cool)
//   -> photocell conductance (fast attack / slow release "memory") -> R = R_bright * (1/c)^0.7
// The resulting notches are non-uniformly spread and sweep asymmetrically - unlike the phaser's equal stages.
// Approximation: time constants and the R(light) law are typical datasheet-like values, not measured.
PremiumUnivibe::PremiumUnivibe() {
    params_.add({"speed_hz", "Speed", 0.3f, 12, 2.5f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"intensity", "Intensity", 0, 100, 70, ParamUnit::Percent});
    static const char* kModes[] = {"Chorus", "Vibrato"};
    ParamInfo m{"mode", "Mode", 0, 1, 0, ParamUnit::Choice}; m.choices = kModes; m.numChoices = 2; params_.add(m);
    params_.add({"volume_db", "Volume", -24, 12, 0, ParamUnit::Decibels});
    addSync(params_);
}
void PremiumUnivibe::onPrepare(const ProcessSpec& spec) {
    const double ctlFs = spec.sampleRate / kCtl;
    lfo_.prepare(spec.sampleRate);
    lampUp_ = 1.0 - std::exp(-1.0 / (0.008 * ctlFs)); lampDn_ = 1.0 - std::exp(-1.0 / (0.030 * ctlFs));
    cellUp_ = 1.0 - std::exp(-1.0 / (0.005 * ctlFs)); cellDn_ = 1.0 - std::exp(-1.0 / (0.040 * ctlFs));
    level_.reset(spec.sampleRate, 0.03); mixWet_.reset(spec.sampleRate, 0.03);
}
void PremiumUnivibe::onReset() {
    for (auto& a : ap_) a.reset();
    lfo_.reset(); ctl_ = 0; lamp_ = cell_ = 0.3;
    ldr_ = 1e5;
    for (int k = 0; k < 4; ++k) G0_[k] = G1_[k] = 0.0f;
}
void PremiumUnivibe::onParametersChanged(bool imm) {
    lfo_.setRate(syncedRate(params_, kSpeed, kSync, bpm_), imm);
    lfo_.setShape(ModLfo::kSine);
    intensity_ = params_.get(kIntensity) / 100.0;
    setS(level_, dbToGain(params_.get(kVolume)), imm);
    setS(mixWet_, params_.getInt(kMode) == kVibrato ? 1.0f : 0.5f, imm);
}
void PremiumUnivibe::onProcess(AudioBlock& b) {
    static const double C[4] = {15e-9, 220e-9, 470e-12, 4.7e-9};
    const double fs = spec_.sampleRate;
    for (int i = 0; i < b.numSamples; ++i) {
        if (ctl_ == 0) {
            // skewed oscillator (fast rise, slow fall), lamp responds to drive^1.5
            const double p = lfo_.value(0.0) + 0.35 * lfo_.value(0.25) * lfo_.value(0.0);
            const double drive = std::pow(clampv(0.5 * (1.0 + p / 1.1), 0.0, 1.0), 1.5);
            const double target = 0.08 + 0.92 * (1.0 - intensity_) * 0.5 + intensity_ * drive * 0.92;
            lamp_ += (target > lamp_ ? lampUp_ : lampDn_) * (target - lamp_);
            cell_ += (lamp_ > cell_ ? cellUp_ : cellDn_) * (lamp_ - cell_);
            ldr_ = 4.0e3 * std::pow(1.0 / std::max(cell_, 0.02), 0.7);
            for (int k = 0; k < 4; ++k) {
                stageHz_[k] = 1.0 / (kTwoPi * ldr_ * C[k]);
                G0_[k] = G1_[k];
                G1_[k] = tptG(stageHz_[k], fs);
            }
        }
        const float t = (float)(ctl_ + 1) / (float)kCtl;
        const float lvl = level_.next(), wm = mixWet_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float x = safe(b.ch[c][i]);
            float v = x;
            for (int k = 0; k < 4; ++k) v = ap_[k].process(v, G0_[k] + t * (G1_[k] - G0_[k]), c);
            b.ch[c][i] = lvl * (x + wm * (v - x));
        }
        lfo_.advance();
        ctl_ = (ctl_ + 1) % kCtl;
    }
}

// =============================================================================================== Flanger
// Wet = modulated tap, Dry = fixed tap at kDryDelayMs from the same line. The relative delay
// rel = wet - dry crosses zero in Through-Zero mode (true TZF: the comb disappears and re-forms with
// inverted slope); Classic keeps rel > 0. The dry tap delay is constant -> reported latency.
PremiumFlanger::PremiumFlanger() {
    params_.add({"rate_hz", "Rate", 0.02f, 5, 0.2f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 70, ParamUnit::Percent});
    params_.add({"manual_ms", "Manual", -4, 4, 0.5f, ParamUnit::Milliseconds});
    params_.add({"feedback", "Feedback", -95, 95, 50, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 50, ParamUnit::Percent});
    static const char* kModes[] = {"Classic", "Through-Zero"};
    ParamInfo m{"mode", "Mode", 0, 1, 1, ParamUnit::Choice}; m.choices = kModes; m.numChoices = 2; params_.add(m);
    params_.add({"stereo_deg", "Stereo Phase", 0, 180, 90, ParamUnit::None});
    ParamInfo sh{"shape", "LFO Shape", 0, 1, 1, ParamUnit::Choice}; sh.choices = kOnOffShapes; sh.numChoices = 2; params_.add(sh);
    addSync(params_);
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumFlanger::onPrepare(const ProcessSpec& spec) {
    dryInt_ = (int)std::lround(kDryDelayMs * 0.001 * spec.sampleRate);
    line_.prepare(3 * dryInt_ + 16);
    fbl_.prepare(3 * dryInt_ + 16);
    lfo_.prepare(spec.sampleRate);
    for (auto* s : {&manual_, &depth_, &mix_, &fb_, &level_}) s->reset(spec.sampleRate, 0.05);
}
void PremiumFlanger::onReset() { line_.reset(); fbl_.reset(); lfo_.reset(); fbState_[0] = fbState_[1] = 0.0f; }
void PremiumFlanger::onParametersChanged(bool imm) {
    const float fs = (float)spec_.sampleRate;
    lfo_.setRate(syncedRate(params_, kRate, kSync, bpm_), imm);
    lfo_.setShape(params_.getInt(kShape) == 1 ? ModLfo::kTriangle : ModLfo::kSine);
    mode_ = params_.getInt(kMode);
    stereo_ = params_.get(kStereo) / 360.0;
    setS(manual_, params_.get(kManual) * 0.001f * fs, imm);
    setS(depth_, params_.get(kDepth) / 100.0f, imm);
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(fb_, params_.get(kFeedback) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
}
void PremiumFlanger::onProcess(AudioBlock& b) {
    const float D = (float)dryInt_;
    for (int i = 0; i < b.numSamples; ++i) {
        const float man = manual_.next(), dep = depth_.next(), mix = mix_.next(), fb = fb_.next(), lvl = level_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            const float m = (float)lfo_.value(c * stereo_);
            float rel;
            if (mode_ == kThroughZero) rel = clampv(man + dep * (D - 3.0f) * m, -(D - 3.0f), D - 3.0f);
            else rel = clampv(std::fabs(man) + dep * (D - 3.0f) * 0.5f * (1.0f + m), 0.02f * D, 2.0f * D - 3.0f);
            rel_[c] = rel;
            const float x = safe(b.ch[c][i]);
            const float dry = line_.readInt(dryInt_, c);
            float wet;
            if (mode_ == kClassic) {
                // classic: the feedback loop runs around the relative delay: w(t) = x(t - D - rel) + fb * w(t - rel)
                wet = line_.read(D + rel, c) + nl::fastTanh(fb * fbl_.read(std::max(rel, 2.0f), c));
                fbl_.push(flushDenorm(wet), c);
                line_.push(x, c);
            } else {
                // through-zero: rel may be <= 0, so the loop runs around the whole modulated tap (D + rel > 0)
                wet = line_.read(D + rel, c);
                line_.push(flushDenorm(x + nl::fastTanh(fb * wet)), c);
                fbl_.push(0.0f, c);
            }
            fbState_[c] = wet;
            b.ch[c][i] = lvl * (dry + mix * (wet - dry));
        }
        line_.advance(); fbl_.advance();
        lfo_.advance();
    }
}

// =============================================================================================== Tremolo
PremiumTremolo::PremiumTremolo() {
    params_.add({"rate_hz", "Rate", 0.2f, 16, 4.5f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"depth", "Depth", 0, 100, 50, ParamUnit::Percent});
    static const char* kModels[] = {"Optical", "Bias", "Harmonic"};
    ParamInfo m{"model", "Model", 0, 2, 0, ParamUnit::Choice}; m.choices = kModels; m.numChoices = 3; params_.add(m);
    static const char* kShapes[] = {"Sine", "Triangle", "Square"};
    ParamInfo sh{"shape", "Shape", 0, 2, 0, ParamUnit::Choice}; sh.choices = kShapes; sh.numChoices = 3; params_.add(sh);
    addSync(params_);
    params_.add({"stereo", "Stereo (Pan)", 0, 1, 0, ParamUnit::Toggle});
    params_.add({"crossover_hz", "Crossover", 200, 2000, 700, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"character", "Character", 0, 100, 50, ParamUnit::Percent});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumTremolo::onPrepare(const ProcessSpec& spec) {
    lfo_.prepare(spec.sampleRate);
    depth_.reset(spec.sampleRate, 0.03); level_.reset(spec.sampleRate, 0.03);
    dc_.design(FilterType::HighPass1, spec.sampleRate, 10.0);
}
void PremiumTremolo::onReset() { lfo_.reset(); xo_.reset(); dc_.reset(); lamp_[0] = lamp_[1] = 1.0; }
void PremiumTremolo::onParametersChanged(bool imm) {
    const double fs = spec_.sampleRate;
    lfo_.setRate(syncedRate(params_, kRate, kSync, bpm_), imm);
    shape_ = params_.getInt(kShape);
    lfo_.setShape(shape_ == 1 ? ModLfo::kTriangle : ModLfo::kSine);
    model_ = params_.getInt(kModel);
    character_ = params_.get(kCharacter) / 100.0f;
    // optical: lamp heats fast, cools slowly; the photocell adds memory (Character lengthens the decay)
    lampUp_ = 1.0 - std::exp(-1.0 / (0.004 * fs));
    lampDn_ = 1.0 - std::exp(-1.0 / ((0.012 + 0.03 * character_) * fs));
    xo_.design(fs, params_.get(kCrossover));
    setS(depth_, params_.get(kDepth) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
}
void PremiumTremolo::onProcess(AudioBlock& b) {
    const bool pan = params_.getInt(kStereo) != 0 && b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float d = depth_.next(), lvl = level_.next();
        for (int c = 0; c < b.numChannels; ++c) {
            double m = lfo_.value(pan && c == 1 ? 0.5 : 0.0);
            if (shape_ == 2) m = std::tanh(5.0 * m) / std::tanh(5.0);
            const float x = safe(b.ch[c][i]);
            float y;
            if (model_ == kOptical) {
                const double target = 0.5 * (1.0 + m);                     // lamp drive 0..1
                lamp_[c] += (target > lamp_[c] ? lampUp_ : lampDn_) * (target - lamp_[c]);
                const double light = std::pow(lamp_[c], 0.6);              // LDR: conductance ~ light^0.6 (approx.)
                y = x * (float)(1.0 - d * (1.0 - light));
            } else if (model_ == kBias) {
                // bias swing of a push-pull stage: gain follows a compressed curve (more time near full gain)
                const double g = 1.0 - d * std::pow(0.5 * (1.0 - m), 1.4);
                const float u = (float)g * x;
                const float even = 0.25f * character_ * (1.0f - (float)g) * u * u / (1.0f + u * u); // bias shift -> 2nd harmonic
                y = u + dc_.process(even, c);
            } else {
                float lo, hi;
                xo_.process(x, c, lo, hi);
                const float gl = 1.0f - d * 0.5f * (1.0f + (float)m), gh = 1.0f - d * 0.5f * (1.0f - (float)m);
                y = gl * lo + gh * hi;
            }
            b.ch[c][i] = lvl * y;
        }
        lfo_.advance();
    }
}

// =============================================================================================== Rotary
// Horn (treble) and drum (bass) rotors behind an LR4 crossover. Each rotor: angular speed with inertia
// (separate acceleration / brake time constants, drum much heavier), Doppler delay (r / c * cos(theta)),
// directivity amplitude modulation, two virtual microphones at +/- width * 90 deg. Base delay = reported latency.
PremiumRotary::PremiumRotary() {
    static const char* kSpeeds[] = {"Slow", "Fast", "Brake"};
    ParamInfo s{"speed", "Speed", 0, 2, 0, ParamUnit::Choice}; s.choices = kSpeeds; s.numChoices = 3; params_.add(s);
    params_.add({"accel", "Acceleration", 25, 400, 100, ParamUnit::Percent, ParamScale::Log});
    params_.add({"horn_db", "Horn Level", -24, 6, 0, ParamUnit::Decibels});
    params_.add({"drum_db", "Drum Level", -24, 6, 0, ParamUnit::Decibels});
    params_.add({"depth", "Depth", 0, 100, 80, ParamUnit::Percent});
    params_.add({"width", "Stereo Width", 0, 100, 70, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 100, ParamUnit::Percent});
    params_.add({"drive", "Drive", 0, 100, 0, ParamUnit::Percent});
    params_.add({"crossover_hz", "Crossover", 400, 1600, 800, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumRotary::onPrepare(const ProcessSpec& spec) {
    baseInt_ = (int)std::ceil((kHornRadius / kSoundSpeed + 0.001) * spec.sampleRate);
    horn_.prepare(2 * baseInt_ + 8); drum_.prepare(2 * baseInt_ + 8);
    for (auto* s : {&hornLv_, &drumLv_, &mix_, &level_, &depth_, &width_}) s->reset(spec.sampleRate, 0.05);
}
void PremiumRotary::onReset() {
    horn_.reset(); drum_.reset(); xo_.reset();
    hornW_ = hornT_; drumW_ = drumT_; hornTh_ = 0.0; drumTh_ = 0.37;
}
void PremiumRotary::onParametersChanged(bool imm) {
    const int sp = params_.getInt(kSpeed);
    hornT_ = sp == kFast ? kHornFast : sp == kSlow ? kHornSlow : 0.0;
    drumT_ = sp == kFast ? kDrumFast : sp == kSlow ? kDrumSlow : 0.0;
    if (imm) { hornW_ = hornT_; drumW_ = drumT_; }
    accel_ = 100.0 / params_.get(kAccel); // time-constant scale
    xo_.design(spec_.sampleRate, params_.get(kCrossover));
    const float dr = params_.get(kDrive) / 100.0f;
    drive_ = dbToGain(18.0f * dr);
    driveNorm_ = 1.0f / drive_;
    setS(hornLv_, dbToGain(params_.get(kHorn)), imm);
    setS(drumLv_, dbToGain(params_.get(kDrum)), imm);
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
    setS(depth_, params_.get(kDepth) / 100.0f, imm);
    setS(width_, params_.get(kWidth) / 100.0f, imm);
}
void PremiumRotary::onProcess(AudioBlock& b) {
    const double fs = spec_.sampleRate;
    // inertia: horn ~0.7 s up / 0.9 s down, drum ~3.5 s up / 4.5 s down (time constants, scaled by Acceleration)
    const double hUp = 1.0 - std::exp(-1.0 / (0.70 * accel_ * fs)), hDn = 1.0 - std::exp(-1.0 / (0.90 * accel_ * fs));
    const double dUp = 1.0 - std::exp(-1.0 / (3.50 * accel_ * fs)), dDn = 1.0 - std::exp(-1.0 / (4.50 * accel_ * fs));
    const double hornDop = kHornRadius / kSoundSpeed * fs, drumDop = kDrumRadius / kSoundSpeed * fs;
    const float base = (float)baseInt_;
    for (int i = 0; i < b.numSamples; ++i) {
        hornW_ += (hornT_ > hornW_ ? hUp : hDn) * (hornT_ - hornW_);
        drumW_ += (drumT_ > drumW_ ? dUp : dDn) * (drumT_ - drumW_);
        hornTh_ += hornW_ / fs; hornTh_ -= std::floor(hornTh_);
        drumTh_ += drumW_ / fs; drumTh_ -= std::floor(drumTh_);
        const float hl = hornLv_.next(), dl = drumLv_.next(), mix = mix_.next(), lvl = level_.next(), dep = depth_.next(), wid = width_.next();
        const float xin = b.numChannels == 2 ? 0.5f * (safe(b.ch[0][i]) + safe(b.ch[1][i])) : safe(b.ch[0][i]);
        const float xd = drive_ > 1.0f ? nl::fastTanh(drive_ * xin) * driveNorm_ * 1.2f : xin;
        float lo, hi;
        xo_.process(xd, 0, lo, hi);
        horn_.push(hi, 0); drum_.push(lo, 0);
        // dry copies on channel 1 of each line, delayed by the same base delay (parallel blend stays aligned)
        horn_.push(safe(b.ch[0][i]), 1); drum_.push(b.numChannels == 2 ? safe(b.ch[1][i]) : safe(b.ch[0][i]), 1);
        for (int c = 0; c < b.numChannels; ++c) {
            const double mic = (b.numChannels == 2 ? (c == 0 ? -0.25 : 0.25) : 0.0) * wid; // +/- 90 deg at width 100 %
            const double ah = kTwoPi * (hornTh_ - mic), ad = kTwoPi * (drumTh_ - mic);
            const float dh = base - (float)(dep * hornDop * std::cos(ah));
            const float dd = base - (float)(dep * drumDop * std::cos(ad));
            const float gh = (float)((1.0 + 0.6 * dep * std::cos(ah)) / (1.0 + 0.6 * dep));
            const float gd = (float)((1.0 + 0.35 * dep * std::cos(ad)) / (1.0 + 0.35 * dep));
            const float wet = hl * gh * horn_.read(dh, 0) + dl * gd * drum_.read(dd, 0);
            const float dryDelayed = c == 0 ? horn_.readInt(baseInt_, 1) : drum_.readInt(baseInt_, 1);
            b.ch[c][i] = lvl * (dryDelayed + mix * (wet - dryDelayed));
        }
        horn_.advance(); drum_.advance();
    }
}

} // namespace mfx
