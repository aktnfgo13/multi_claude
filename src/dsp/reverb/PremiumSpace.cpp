#include "dsp/reverb/PremiumSpace.h"

#include "dsp/nonlinear/Nonlinear.h"

namespace mfx {

namespace {
// line lengths (ms) at size factor 1.0 - spread 37..108 ms, no common small factors once converted to samples
constexpr double kLensMs[FdnCore::kN] = {37.3, 41.9, 44.7, 49.1, 53.9, 57.7, 61.3, 66.1, 70.9, 74.3, 79.1, 83.9, 89.3, 94.7, 101.3, 107.9};
constexpr double kDiffMs[4] = {3.31, 4.87, 7.13, 10.29};
constexpr double kMaxLenMs = 107.9;
inline int popc(unsigned v) { int n = 0; while (v) { n += v & 1u; v >>= 1; } return n; }
inline float hsign(int row, int i) { return (popc((unsigned)(row & i)) & 1) ? -1.0f : 1.0f; }
inline float softGuard(float w) { // identity below 2, bounded (<3) above: keeps the loop bounded at any setting
    const float a = std::fabs(w);
    return a <= 2.0f ? w : std::copysign(2.0f + std::tanh(a - 2.0f), w);
}
template <class S> void setS(S& s, float v, bool imm) { if (imm) s.setImmediate(v); else s.setTarget(v); }
inline float safe(float x) { return std::isfinite(x) ? x : 0.0f; }
inline float dryGain(float mix) { return std::min(1.0f, 2.0f * (1.0f - mix)); }
inline float wetGain(float mix) { return std::min(1.0f, 2.0f * mix); }
} // namespace

// =============================================================================================== FdnCore
void FdnCore::prepare(double fs, double maxSize) {
    fs_ = fs;
    const int maxLen = (int)((kMaxLenMs * maxSize + 1.0) * 0.001 * fs) + 64;
    memBytes_ = 0;
    for (auto& l : lines_) { l.prepare(maxLen); memBytes_ += l.bytes(); }
    for (int k = 0; k < 4; ++k) { diff_[k].prepare((int)(kDiffMs[k] * 0.001 * fs) + 8); memBytes_ += diff_[k].bytes(); }
    mod_.prepare(fs); mod_.setShape(ModLfo::kRandom);
    lenA_ = 1.0 - std::exp(-32.0 / (0.3 * fs));
    for (int k = 0; k < 4; ++k) diffLen_[k] = (float)std::max(2.0, std::round(kDiffMs[k] * 0.001 * fs));
}
void FdnCore::reset() {
    for (auto& l : lines_) l.reset();
    for (auto& d : diff_) d.reset();
    for (int i = 0; i < kN; ++i) { lowSh_[i].reset(); highSh_[i].reset(); len_[i] = lenTarget_[i]; lenStep_[i] = 0.0; }
    mod_.reset(); ctl_ = 0; guard_ = 1.0f;
    freeze_ = freezeTarget_;
    updateGains();
}
void FdnCore::setDecay(double rt60, double bass, double treble, double xLow, double xHigh) {
    rt60_ = std::max(0.05, rt60); bassMult_ = bass; trebMult_ = treble; xLow_ = xLow; xHigh_ = xHigh;
    updateGains();
}
void FdnCore::setSize(double size, bool imm) {
    for (int i = 0; i < kN; ++i) {
        lenTarget_[i] = kLensMs[i] * 0.001 * fs_ * size;
        if (imm) { len_[i] = lenTarget_[i]; lenStep_[i] = 0.0; }
    }
    if (imm) updateGains();
}
void FdnCore::setDiffusion(double a) { diffG_ = (float)(0.45 + 0.3 * clampv(a, 0.0, 1.0)); }
void FdnCore::setModulation(double depthMs, double rateHz) { modDepth_ = depthMs * 0.001 * fs_; mod_.setRate(rateHz, false); }
void FdnCore::setFreeze(bool on) { freezeTarget_ = on ? 1.0 : 0.0; freezeStep_ = 1.0 / (0.08 * fs_); }
void FdnCore::updateGains() {
    double s = 0;
    for (int i = 0; i < kN; ++i) {
        const double L = len_[i];
        const double gM = std::pow(10.0, -3.0 * L / (fs_ * rt60_));
        const double gL = std::pow(10.0, -3.0 * L / (fs_ * rt60_ * bassMult_));
        const double gH = std::pow(10.0, -3.0 * L / (fs_ * rt60_ * trebMult_));
        g_[i] = (float)gM;
        lowSh_[i].design(fs_, xLow_, gL / gM, false);
        highSh_[i].design(fs_, xHigh_, gH / gM, true);
        s += gM;
    }
    meanGain_ = s / kN;
}
void FdnCore::process(float inL, float inR, float extra, float& outL, float& outR) {
    // freeze ramp (80 ms): input fades out, loop losses fade out
    if (freeze_ != freezeTarget_) freeze_ = freezeTarget_ > freeze_ ? std::min(freezeTarget_, freeze_ + freezeStep_) : std::max(freezeTarget_, freeze_ - freezeStep_);
    if (++ctl_ >= 32) { // size smoothing at control rate (one-pole, slew-limited), gains follow the actual lengths
        ctl_ = 0;
        bool moved = false;
        for (int i = 0; i < kN; ++i) {
            const double d = lenTarget_[i] - len_[i];
            if (std::fabs(d) > 1e-3) {
                const double stepTotal = std::clamp(lenA_ * d, -kMaxSlew * 32.0, kMaxSlew * 32.0);
                lenStep_[i] = stepTotal / 32.0; moved = true;
            } else { len_[i] = lenTarget_[i]; lenStep_[i] = 0.0; }
        }
        if (moved) updateGains();
    }
    // read positions glide per sample (no 32-sample staircase -> no clicks while Size moves)
    for (int i = 0; i < kN; ++i) len_[i] += lenStep_[i];
    const float f = (float)freeze_;
    // input diffusion (Schroeder all-passes), L on channel 0, R on channel 1
    float v[2] = {inL, inR};
    for (int k = 0; k < 4; ++k) {
        for (int c = 0; c < 2; ++c) {
            const float z = diff_[k].readInt((int)diffLen_[k], c);
            const float y = -diffG_ * v[c] + z;
            diff_[k].push(flushDenorm(v[c] + diffG_ * y), c);
            v[c] = y;
        }
        diff_[k].advance();
    }
    const float inGain = 1.0f - f;
    const float dL = v[0] * inGain, dR = v[1] * inGain, ex = extra * inGain;
    // read the lines (smooth-random modulation, reduced while frozen)
    float o[kN];
    const double md = modDepth_ * (1.0 - 0.7 * f);
    for (int i = 0; i < kN; ++i) {
        const double m = md > 0.0 ? md * mod_.value(i * 0.618 + 0.11 * i * i) : 0.0;
        o[i] = lines_[i >> 1].read((float)std::max(4.0, len_[i] + m), i & 1);
    }
    // outputs: two orthogonal Hadamard rows -> decorrelated L/R without anti-phase tricks
    float l = 0, r = 0;
    for (int i = 0; i < kN; ++i) { l += hsign(3, i) * o[i]; r += hsign(12, i) * o[i]; }
    outL = 0.25f * l; outR = 0.25f * r;
    // fast Walsh-Hadamard mixing, orthonormal (x 1/4)
    float h[kN];
    for (int i = 0; i < kN; ++i) h[i] = o[i];
    for (int len = 1; len < kN; len <<= 1)
        for (int i = 0; i < kN; i += 2 * len)
            for (int j = i; j < i + len; ++j) { const float a = h[j], b = h[j + len]; h[j] = a + b; h[j + len] = a - b; }
    for (int i = 0; i < kN; ++i) {
        const float x = 0.25f * h[i];
        const float filt = highSh_[i].process(lowSh_[i].process(x, 0), 0) * g_[i];
        float w = filt + f * (x - filt);                       // frozen: lossless circulation
        const float sgn = ((i >> 1) & 1) ? -1.0f : 1.0f;
        w += sgn * (0.35f * ((i & 1) ? dR : dL) + 0.25f * ex);
        lines_[i >> 1].push(flushDenorm(softGuard(w)), i & 1);
    }
    for (auto& ln : lines_) ln.advance();
    mod_.advance();
}

// =============================================================================================== PitchShifter
void PitchShifter::prepare(double fs, double windowMs) {
    w_ = windowMs * 0.001 * fs;
    dMin_ = 4.0;
    buf_.prepare((int)(w_ + dMin_) + 16);
}
void PitchShifter::reset() { buf_.reset(); phase_ = 0.0; }
float PitchShifter::process(float x, int c) {
    buf_.push(x, c);
    double p1 = phase_ + offset_ * c; p1 -= std::floor(p1);
    double p2 = p1 + 0.5; p2 -= std::floor(p2);
    const double w1 = std::sin(kPi * p1), w2 = std::sin(kPi * p2);
    return (float)(w1 * w1) * buf_.read((float)(dMin_ + p1 * w_), c) + (float)(w2 * w2) * buf_.read((float)(dMin_ + p2 * w_), c);
}
void PitchShifter::advance() {
    buf_.advance();
    phase_ -= (ratio_ - 1.0) / w_;   // the taps move at (ratio - 1) samples per sample -> playback speed = ratio
    phase_ -= std::floor(phase_);
}

// =============================================================================================== Hall
PremiumHall::PremiumHall() {
    params_.add({"decay_s", "Decay", 0.3f, 20, 3.0f, ParamUnit::Seconds, ParamScale::Log});
    params_.add({"predelay_ms", "Pre-Delay", 0, 250, 20, ParamUnit::Milliseconds});
    params_.add({"size", "Size", 0, 100, 60, ParamUnit::Percent});
    params_.add({"bass_mult", "Bass Decay", 0.5f, 2.5f, 1.2f, ParamUnit::Ratio});
    params_.add({"treble_mult", "Treble Decay", 0.1f, 1.0f, 0.45f, ParamUnit::Ratio});
    params_.add({"early", "Early Reflections", 0, 100, 40, ParamUnit::Percent});
    params_.add({"diffusion", "Diffusion", 0, 100, 70, ParamUnit::Percent});
    params_.add({"mod_depth", "Mod Depth", 0, 100, 50, ParamUnit::Percent});
    params_.add({"mod_rate_hz", "Mod Rate", 0.05f, 2, 0.5f, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"low_cut_hz", "Low Cut", 20, 500, 80, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"high_cut_hz", "High Cut", 2000, 20000, 12000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"width", "Width", 0, 150, 100, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 25, ParamUnit::Percent});
    params_.add({"freeze", "Freeze", 0, 1, 0, ParamUnit::Toggle});
    params_.add({"duck_db", "Ducking", 0, 24, 0, ParamUnit::Decibels});
    params_.add({"duck_threshold_db", "Duck Threshold", -60, 0, -30, ParamUnit::Decibels});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
    // early-reflection pattern (ms at size factor 1): image-source-like, distinct L/R, decaying with distance
    static const double erL[kEr] = {7.1, 11.9, 17.3, 22.9, 29.3, 35.1, 41.7, 48.3, 55.9, 63.1, 71.3, 79.9};
    static const double erR[kEr] = {8.3, 13.1, 15.7, 24.7, 27.1, 37.9, 43.3, 46.1, 58.7, 61.9, 74.9, 77.3};
    for (int k = 0; k < kEr; ++k) {
        erMs_[0][k] = erL[k]; erMs_[1][k] = erR[k];
        erGain_[0][k] = (float)(0.55 / (1.0 + erL[k] / 12.0));
        erGain_[1][k] = (float)(0.55 / (1.0 + erR[k] / 12.0));
    }
}
void PremiumHall::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    fdn_.prepare(fs, 1.6);
    pre_.prepare((int)(0.4 * fs) + 16);
    duck_.prepare(fs);
    erLp_.setCutoff(fs, std::min(8000.0, 0.45 * fs));
    for (auto* s : {&mix_, &width_, &level_, &early_}) s->reset(fs, 0.03);
    preD_.reset(fs, 0.08); erScale_.reset(fs, 0.3);
}
void PremiumHall::onReset() { fdn_.reset(); pre_.reset(); inHp_.reset(); inLp_.reset(); erLp_.reset(); duck_.reset(); }
void PremiumHall::onParametersChanged(bool imm) {
    const double fs = spec_.sampleRate;
    const double size = 0.4 + 1.2 * params_.get(kSize) / 100.0;
    fdn_.setSize(size, imm);
    fdn_.setDecay(params_.get(kDecay), params_.get(kBass), params_.get(kTreble), 250.0, 4000.0);
    fdn_.setDiffusion(params_.get(kDiffusion) / 100.0);
    fdn_.setModulation(params_.get(kModDepth) / 100.0 * 0.6, params_.get(kModRate));
    fdn_.setFreeze(params_.getInt(kFreeze) != 0);
    inHp_.design(FilterType::HighPass, fs, params_.get(kLowCut), 0.7071);
    inLp_.design(FilterType::LowPass, fs, std::min((double)params_.get(kHighCut), 0.45 * fs), 0.7071);
    duck_.set(params_.get(kDuck), params_.get(kDuckThr));
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(width_, params_.get(kWidth) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
    setS(early_, params_.get(kEarly) / 100.0f, imm);
    setS(preD_, params_.get(kPreDelay) * 0.001f * (float)fs, imm);
    setS(erScale_, (float)(size * 0.001 * fs), imm);
}
void PremiumHall::onProcess(AudioBlock& b) {
    const bool st = b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float inL = safe(b.ch[0][i]), inR = st ? safe(b.ch[1][i]) : inL;
        const float l = inLp_.process(inHp_.process(inL, 0), 0), r = inLp_.process(inHp_.process(inR, 1), 1);
        pre_.push(l, 0); pre_.push(r, 1);
        const float pd = preD_.next(), es = erScale_.next();
        const float xl = pre_.read(std::max(pd, 0.0f), 0), xr = pre_.read(std::max(pd, 0.0f), 1);
        float er[2] = {0, 0};
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kEr; ++k) er[c] += erGain_[c][k] * pre_.read(pd + (float)erMs_[c][k] * es, k & 1 ? 1 - c : c);
        pre_.advance();
        float ll, lr;
        fdn_.process(xl, xr, 0.0f, ll, lr);
        const float e = early_.next();
        float wl = ll + e * erLp_.process(er[0], 0), wr = lr + e * erLp_.process(er[1], 1);
        const float w = width_.next(), m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w;
        wl = m + sd; wr = m - sd;
        const float dg = duck_.next(std::max(std::fabs(inL), std::fabs(inR)));
        const float mix = mix_.next(), lv = level_.next();
        b.ch[0][i] = lv * (dryGain(mix) * inL + wetGain(mix) * dg * wl);
        if (st) b.ch[1][i] = lv * (dryGain(mix) * inR + wetGain(mix) * dg * wr);
        else b.ch[0][i] = lv * (dryGain(mix) * inL + wetGain(mix) * dg * 0.5f * (wl + wr));
    }
}

// =============================================================================================== Spring
// Lightweight parametric spring model after the published approach of dispersive all-pass "spectral delay
// filters" inside a feedback loop (Abel, Berners, Valimaki, Parker et al., e.g. "Parametric spring reverberation
// effect", JAES 2010): low chirps = cascade of 60 stretched all-passes (a + z^-K)/(1 + a z^-K) band-limited to the
// transition frequency fC = fs / 2K, high chirps = 20 first-order all-passes in a shorter loop above fC.
// Group delay rises with frequency up to fC -> the "drip" chirp; every round trip disperses further.
// Two tanks with slightly different springs; width 0 = their mono sum. Not fitted to a measured tank.
PremiumSpring::PremiumSpring() {
    params_.add({"decay_s", "Decay", 0.5f, 8, 2.5f, ParamUnit::Seconds, ParamScale::Log});
    params_.add({"mix", "Mix", 0, 100, 25, ParamUnit::Percent});
    params_.add({"tone", "Tone", 0, 100, 50, ParamUnit::Percent});
    params_.add({"predelay_ms", "Pre-Delay", 0, 100, 0, ParamUnit::Milliseconds});
    params_.add({"drive", "Drive", 0, 100, 30, ParamUnit::Percent});
    params_.add({"character", "Spring Character", 0, 100, 50, ParamUnit::Percent});
    params_.add({"width", "Stereo Width", 0, 100, 60, ParamUnit::Percent});
    params_.add({"duck_db", "Ducking", 0, 24, 0, ParamUnit::Decibels});
    params_.add({"duck_threshold_db", "Duck Threshold", -60, 0, -30, ParamUnit::Decibels});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumSpring::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    loop_.prepare((int)(0.09 * fs) + 16);
    loopH_.prepare((int)(0.04 * fs) + 16);
    pre_.prepare((int)(0.1 * fs) + 16);
    jitter_.prepare(fs); jitter_.setShape(ModLfo::kRandom); jitter_.setRate(0.7, true);
    duck_.prepare(fs);
    outHp_.design(FilterType::HighPass, fs, 90.0, 0.9);
    for (auto* s : {&mix_, &width_, &level_}) s->reset(fs, 0.03);
}
void PremiumSpring::onReset() {
    for (auto& t : lf_) for (auto& s : t) s.reset();
    for (auto& t : hfZ_) for (auto& z : t) z = 0;
    loop_.reset(); loopH_.reset(); pre_.reset();
    for (int t = 0; t < 2; ++t) { lpC_[t].reset(); lpC2_[t].reset(); }
    hpH_.reset(); outHp_.reset(); tone_.reset(); jitter_.reset(); duck_.reset();
}
void PremiumSpring::onParametersChanged(bool imm) {
    const double fs = spec_.sampleRate;
    const double ch = params_.get(kCharacter) / 100.0, T = params_.get(kDecay);
    const double fC0 = 3500.0 + 2500.0 * ch;               // stiffer spring -> higher transition frequency
    const double tdMs0 = 55.0 - 20.0 * ch;                  // and shorter round trip
    for (int t = 0; t < 2; ++t) {
        const double fC = fC0 * (t ? 0.93 : 1.0);
        K_[t] = clampv((int)std::lround(fs / (2.0 * fC)), 1, StretchedAp::kMaxK);
        td_[t] = tdMs0 * (t ? 1.12 : 1.0) * 0.001 * fs;
        tdH_[t] = td_[t] * 0.42;
        a_[t] = (float)(0.55 + 0.1 * (1.0 - ch) + (t ? 0.03 : 0.0));
        const double fCq = fs / (2.0 * K_[t]);
        lpC_[t].design(FilterType::LowPass, fs, std::min(fCq, 0.45 * fs), 0.5412);
        lpC2_[t].design(FilterType::LowPass, fs, std::min(fCq, 0.45 * fs), 1.3066);
        // loop period at low frequencies: delay + DC group delay of the chain
        const double gdDc = kLfStages * K_[t] * (1.0 - a_[t]) / (1.0 + a_[t]);
        g_[t] = (float)std::pow(10.0, -3.0 * (td_[t] + gdDc) / (fs * T));
        gH_[t] = (float)std::pow(10.0, -3.0 * tdH_[t] / (fs * T * 0.5));
    }
    hpH_.design(FilterType::HighPass, fs, std::min(fC0, 0.4 * fs), 0.7071);
    tone_.setCutoff(fs, std::min(mapLog(params_.get(kTone) / 100.0, 1500.0, 12000.0), 0.45 * fs));
    const float dr = params_.get(kDrive) / 100.0f;
    drive_ = dbToGain(18.0f * dr); driveN_ = 1.0f / drive_;
    preSamples_ = (int)(params_.get(kPreDelay) * 0.001 * fs);
    duck_.set(params_.get(kDuck), params_.get(kDuckThr));
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(width_, params_.get(kWidth) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
}
void PremiumSpring::onProcess(AudioBlock& b) {
    const bool st = b.numChannels == 2;
    const double jit = 0.00003 * spec_.sampleRate;
    for (int i = 0; i < b.numSamples; ++i) {
        const float inL = safe(b.ch[0][i]), inR = st ? safe(b.ch[1][i]) : inL;
        pre_.push(0.5f * (inL + inR), 0);
        const float xin = preSamples_ > 0 ? pre_.readInt(preSamples_, 0) : 0.5f * (inL + inR);
        pre_.advance();
        const float x = nl::fastTanh(drive_ * xin) * driveN_;   // transducer drive (gentle; unity small-signal)
        const float xh = hpH_.process(x, 0);
        float tank[2];
        for (int t = 0; t < 2; ++t) {
            float s = loop_.read((float)(td_[t] + jit * jitter_.value(t * 0.5)), t);
            for (int k = 0; k < kLfStages; ++k) s = lf_[t][k].process(s, a_[t], K_[t]);
            s = lpC2_[t].process(lpC_[t].process(s, 0), 0);
            s = tone_.process(s, t);
            loop_.push(flushDenorm(x + g_[t] * s), t);
            float h = loopH_.read((float)tdH_[t], t);
            for (int k = 0; k < kHfStages; ++k) { const float y = -0.6f * h + hfZ_[t][k]; hfZ_[t][k] = flushDenorm(h + 0.6f * y); h = y; }
            loopH_.push(flushDenorm(xh + gH_[t] * h), t);
            tank[t] = s + 0.2f * h;
        }
        loop_.advance(); loopH_.advance(); jitter_.advance();
        float wl = outHp_.process(tank[0], 0), wr = outHp_.process(tank[1], 1);
        const float w = width_.next(), m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w;
        wl = m + sd; wr = m - sd;
        const float dg = duck_.next(std::max(std::fabs(inL), std::fabs(inR)));
        const float mix = mix_.next(), lv = level_.next();
        b.ch[0][i] = lv * (dryGain(mix) * inL + wetGain(mix) * dg * (st ? wl : 0.5f * (wl + wr)));
        if (st) b.ch[1][i] = lv * (dryGain(mix) * inR + wetGain(mix) * dg * wr);
    }
}

// =============================================================================================== Shimmer
PremiumShimmer::PremiumShimmer() {
    params_.add({"decay_s", "Decay", 1, 30, 8, ParamUnit::Seconds, ParamScale::Log});
    params_.add({"shimmer", "Shimmer", 0, 100, 50, ParamUnit::Percent});
    static const char* kInt[] = {"+12", "+7", "+19", "+5", "-12"};
    ParamInfo iv{"interval", "Interval", 0, 4, 0, ParamUnit::Choice}; iv.choices = kInt; iv.numChoices = 5; params_.add(iv);
    static const char* kStr[] = {"Feedback", "Pre"};
    ParamInfo s{"structure", "Structure", 0, 1, 0, ParamUnit::Choice}; s.choices = kStr; s.numChoices = 2; params_.add(s);
    params_.add({"tone_hz", "Tone", 1000, 12000, 6000, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"size", "Size", 0, 100, 70, ParamUnit::Percent});
    params_.add({"mod_depth", "Mod Depth", 0, 100, 30, ParamUnit::Percent});
    params_.add({"predelay_ms", "Pre-Delay", 0, 250, 30, ParamUnit::Milliseconds});
    params_.add({"low_cut_hz", "Low Cut", 20, 1000, 200, ParamUnit::Hertz, ParamScale::Log});
    params_.add({"width", "Width", 0, 150, 100, ParamUnit::Percent});
    params_.add({"mix", "Mix", 0, 100, 35, ParamUnit::Percent});
    params_.add({"freeze", "Freeze", 0, 1, 0, ParamUnit::Toggle});
    params_.add({"duck_db", "Ducking", 0, 24, 0, ParamUnit::Decibels});
    params_.add({"duck_threshold_db", "Duck Threshold", -60, 0, -30, ParamUnit::Decibels});
    params_.add({"level_db", "Output Level", -24, 12, 0, ParamUnit::Decibels});
}
void PremiumShimmer::onPrepare(const ProcessSpec& spec) {
    const double fs = spec.sampleRate;
    fdn_.prepare(fs, 1.6);
    shift_.prepare(fs, 50.0);
    shift_.setPhaseOffset(0.31); // L/R windows offset -> decorrelated grain boundaries
    pre_.prepare((int)(0.26 * fs) + 16);
    duck_.prepare(fs);
    regA_ = 1.0f - (float)std::exp(-1.0 / (0.01 * fs)); regR_ = 1.0f - (float)std::exp(-1.0 / (0.3 * fs));
    for (auto* s : {&mix_, &width_, &level_, &amount_}) s->reset(fs, 0.05);
}
void PremiumShimmer::onReset() {
    fdn_.reset(); shift_.reset(); pre_.reset(); inHp_.reset(); shHp_.reset(); shLp_.reset(); duck_.reset();
    fbL_ = fbR_ = regEnv_ = 0.0f;
}
void PremiumShimmer::onParametersChanged(bool imm) {
    const double fs = spec_.sampleRate;
    static const double semis[] = {12, 7, 19, 5, -12};
    shift_.setRatio(std::pow(2.0, semis[clampv(params_.getInt(kInterval), 0, 4)] / 12.0));
    structure_ = params_.getInt(kStructure);
    fdn_.setSize(0.4 + 1.2 * params_.get(kSize) / 100.0, imm);
    fdn_.setDecay(params_.get(kDecay), 1.0, 0.6, 250.0, 4000.0);
    fdn_.setDiffusion(0.85);
    fdn_.setModulation(params_.get(kModDepth) / 100.0 * 0.6, 0.25);
    fdn_.setFreeze(params_.getInt(kFreeze) != 0);
    inHp_.design(FilterType::HighPass, fs, params_.get(kLowCut), 0.7071);
    shHp_.design(FilterType::HighPass, fs, 250.0, 0.7071);
    shLp_.design(FilterType::LowPass, fs, std::min((double)params_.get(kTone), 0.45 * fs), 0.7071);
    preSamples_ = (int)(params_.get(kPreDelay) * 0.001 * fs);
    duck_.set(params_.get(kDuck), params_.get(kDuckThr));
    setS(mix_, params_.get(kMix) / 100.0f, imm);
    setS(width_, params_.get(kWidth) / 100.0f, imm);
    setS(level_, dbToGain(params_.get(kLevel)), imm);
    setS(amount_, params_.get(kShimmer) / 100.0f, imm);
}
void PremiumShimmer::onProcess(AudioBlock& b) {
    const bool st = b.numChannels == 2;
    for (int i = 0; i < b.numSamples; ++i) {
        const float inL = safe(b.ch[0][i]), inR = st ? safe(b.ch[1][i]) : inL;
        float l = inHp_.process(inL, 0), r = inHp_.process(inR, 1);
        pre_.push(l, 0); pre_.push(r, 1);
        if (preSamples_ > 0) { l = pre_.readInt(preSamples_, 0); r = pre_.readInt(preSamples_, 1); }
        pre_.advance();
        const float amt = amount_.next();
        // pitch layer: in the loop (previous wet output, accumulates +interval per pass) or before the reverb
        const float srcL = structure_ == kFeedback ? fbL_ : l, srcR = structure_ == kFeedback ? fbR_ : r;
        float sl = shLp_.process(shHp_.process(shift_.process(srcL, 0), 0), 0);
        float sr = shLp_.process(shHp_.process(shift_.process(srcR, 1), 1), 1);
        shift_.advance();
        // loop gain normalised by the network's mean loop gain, plus a level regulator (no hard clipping)
        const float norm = structure_ == kFeedback ? 2.4f * (float)std::sqrt(std::max(0.0, 1.0 - fdn_.meanLoopGain() * fdn_.meanLoopGain())) : 1.0f;
        sl *= amt * norm; sr *= amt * norm;
        const float a = std::max(std::fabs(sl), std::fabs(sr));
        regEnv_ += (a > regEnv_ ? regA_ : regR_) * (a - regEnv_);
        if (regEnv_ > 0.3f) { const float k = 0.3f / regEnv_; sl *= k; sr *= k; }
        float wl, wr;
        fdn_.process(l + sl, r + sr, 0.0f, wl, wr);
        fbL_ = wl; fbR_ = wr;
        const float w = width_.next(), m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * w;
        wl = m + sd; wr = m - sd;
        const float dg = duck_.next(std::max(std::fabs(inL), std::fabs(inR)));
        const float mix = mix_.next(), lv = level_.next();
        b.ch[0][i] = lv * (dryGain(mix) * inL + wetGain(mix) * dg * (st ? wl : 0.5f * (wl + wr)));
        if (st) b.ch[1][i] = lv * (dryGain(mix) * inR + wetGain(mix) * dg * wr);
    }
}

} // namespace mfx
