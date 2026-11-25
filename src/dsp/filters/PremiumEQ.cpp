#include "dsp/filters/PremiumEQ.h"

#include <complex>

namespace mfx {

namespace {
const char* kTypeNames[] = {"Off", "Bell", "Low Shelf", "High Shelf", "HPF 12", "HPF 24", "LPF 12", "LPF 24", "Notch"};
const char* kFreqIds[] = {"b1_hz", "b2_hz", "b3_hz", "b4_hz", "b5_hz"};
const char* kGainIds[] = {"b1_db", "b2_db", "b3_db", "b4_db", "b5_db"};
const char* kQIds[] = {"b1_q", "b2_q", "b3_q", "b4_q", "b5_q"};
const char* kTypeIds[] = {"b1_type", "b2_type", "b3_type", "b4_type", "b5_type"};
const char* kFreqNames[] = {"Band 1 Freq", "Band 2 Freq", "Band 3 Freq", "Band 4 Freq", "Band 5 Freq"};
const char* kGainNames[] = {"Band 1 Gain", "Band 2 Gain", "Band 3 Gain", "Band 4 Gain", "Band 5 Gain"};
const char* kQNames[] = {"Band 1 Q", "Band 2 Q", "Band 3 Q", "Band 4 Q", "Band 5 Q"};
const char* kTypeLabels[] = {"Band 1 Type", "Band 2 Type", "Band 3 Type", "Band 4 Type", "Band 5 Type"};
constexpr float kDefFreq[5] = {100, 400, 1500, 4000, 8000};
constexpr int kDefType[5] = {PremiumEQ::kLowShelf, PremiumEQ::kBell, PremiumEQ::kBell, PremiumEQ::kBell, PremiumEQ::kHighShelf};
constexpr double kMinF = 10.0, kMaxFRel = 0.49, kMinQ = 0.1, kMaxQ = 18.0, kMaxGain = 24.0;
} // namespace

PremiumEQ::PremiumEQ() {
    for (int b = 0; b < kBands; ++b) {
        params_.add({kFreqIds[b], kFreqNames[b], 20, 20000, kDefFreq[b], ParamUnit::Hertz, ParamScale::Log});
        params_.add({kGainIds[b], kGainNames[b], -24, 24, 0, ParamUnit::Decibels});
        params_.add({kQIds[b], kQNames[b], 0.1f, 18, 0.7071f, ParamUnit::None, ParamScale::Log});
    }
    params_.add({"output_db", "Output", -24, 24, 0, ParamUnit::Decibels});
    for (int b = 0; b < kBands; ++b) {
        ParamInfo t{kTypeIds[b], kTypeLabels[b], 0, (float)(kNumTypes - 1), (float)kDefType[b], ParamUnit::Choice};
        t.choices = kTypeNames; t.numChoices = kNumTypes;
        params_.add(t);
    }
}

PremiumEQ::Coeffs PremiumEQ::design(int type, double fs, double f, double gainDb, double q) {
    f = clampv(f, kMinF, kMaxFRel * fs);
    q = clampv(q, kMinQ, kMaxQ);
    gainDb = clampv(gainDb, -kMaxGain, kMaxGain);
    const double t = std::tan(kPi * f / fs);
    const double A = std::pow(10.0, gainDb / 40.0);
    Coeffs c;
    double g = t, k = 1.0 / q, m0 = 1, m1 = 0, m2 = 0;
    switch (type) {
        case kBell: k = 1.0 / (q * A); m1 = k * (A * A - 1.0); break;
        case kLowShelf: g = t / std::sqrt(A); m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
        case kHighShelf: g = t * std::sqrt(A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
        case kHp12: m1 = -k; m2 = -1.0; break;
        case kHp24: k = 1.0 / 0.5412; c.k2 = (float)(1.0 / 1.3066); c.cascade = true; m1 = -k; m2 = -1.0; break;
        case kLp12: m0 = 0; m2 = 1.0; break;
        case kLp24: k = 1.0 / 0.5412; c.k2 = (float)(1.0 / 1.3066); c.cascade = true; m0 = 0; m2 = 1.0; break;
        case kNotch: m1 = -k; break;
        default: break; // Off
    }
    c.g = (float)g; c.k = (float)k; c.m0 = (float)m0; c.m1 = (float)m1; c.m2 = (float)m2;
    if (!c.cascade) c.k2 = c.k;
    c.prep();
    return c;
}

void PremiumEQ::Coeffs::prep() { a1 = 1.0f / (1.0f + g * (g + k)); b1 = 1.0f / (1.0f + g * (g + k2)); }

double PremiumEQ::magnitude(const Coeffs& c, double fs, double f) {
    if (c.m0 == 1.0f && c.m1 == 0.0f && c.m2 == 0.0f) return 1.0;
    const std::complex<double> s(0.0, std::tan(kPi * std::min(f, 0.4999 * fs) / fs) / c.g);
    auto stage = [&](double k, double m1) {
        const std::complex<double> d = s * s + k * s + 1.0;
        return (double)c.m0 + m1 * s / d + (double)c.m2 / d;
    };
    std::complex<double> h = stage(c.k, c.m1);
    if (c.cascade) h *= stage(c.k2, (double)c.m1 * c.k2 / c.k);
    return std::abs(h);
}

double PremiumEQ::responseDb(double f) const {
    double m = out_;
    for (const auto& b : bands_) {
        const Filter& fl = b.f[b.active];
        if (fl.type != kOff) m *= magnitude(fl.cur, spec_.sampleRate, f);
    }
    return 20.0 * std::log10(std::max(m, 1e-12));
}

double PremiumEQ::targetResponseDb(const ParameterSet& ps, double fs, double f) {
    double m = dbToGain(ps.get(kOutput));
    for (int b = 0; b < kBands; ++b) {
        const int t = ps.getInt(kTypeBase + b);
        if (t == kOff) continue;
        m *= magnitude(design(t, fs, ps.get(b * 3), ps.get(b * 3 + 1), ps.get(b * 3 + 2)), fs, f);
    }
    return 20.0 * std::log10(std::max(m, 1e-12));
}

void PremiumEQ::onPrepare(const ProcessSpec& spec) {
    smoothA_ = 1.0f - (float)std::exp(-(double)kCtl / (0.020 * spec.sampleRate));
    xfadeLen_ = std::max(kCtl, (int)(0.020 * spec.sampleRate));
}

void PremiumEQ::onReset() {
    for (auto& b : bands_) { b.f[0].clearState(); b.f[1].clearState(); b.xfade = 0; }
    ctl_ = 0;
    controlUpdate(true);
}

void PremiumEQ::onParametersChanged(bool immediate) {
    for (int b = 0; b < kBands; ++b) {
        Band& bd = bands_[b];
        bd.tLogF = std::log(clampv((double)params_.get(b * 3), kMinF, kMaxFRel * spec_.sampleRate));
        bd.tGain = params_.get(b * 3 + 1);
        bd.tLogQ = std::log((double)params_.get(b * 3 + 2));
        bd.pendingType = params_.getInt(kTypeBase + b);
    }
    outTarget_ = dbToGain(params_.get(kOutput));
    if (immediate) {
        for (auto& bd : bands_) { bd.f[bd.active].type = bd.pendingType; bd.xfade = 0; }
        out_ = outTarget_;
        controlUpdate(true);
    }
}

// Every kCtl samples: smooth the targets, start type cross-fades, compute the next coefficient ramp end point.
void PremiumEQ::controlUpdate(bool snap) {
    const double fs = spec_.sampleRate;
    for (auto& bd : bands_) {
        if (snap) { bd.logF = bd.tLogF; bd.gain = bd.tGain; bd.logQ = bd.tLogQ; }
        else {
            auto follow = [&](double& v, double t, double eps) { v += smoothA_ * (t - v); if (std::fabs(t - v) < eps) v = t; };
            follow(bd.logF, bd.tLogF, 1e-5); follow(bd.gain, bd.tGain, 1e-4); follow(bd.logQ, bd.tLogQ, 1e-5);
        }
        if (!snap && bd.xfade == 0 && bd.pendingType != bd.f[bd.active].type) { // dual-filter cross-fade
            bd.active = 1 - bd.active;
            Filter& nf = bd.f[bd.active];
            nf.type = bd.pendingType;
            nf.clearState();
            nf.cur = nf.from = nf.to = design(nf.type, fs, std::exp(bd.logF), bd.gain, std::exp(bd.logQ));
            bd.xfade = xfadeLen_;
        }
        for (int k = 0; k < 2; ++k) {
            Filter& fl = bd.f[k];
            if (k != bd.active && bd.xfade == 0) continue;
            fl.from = fl.to;
            if (k == bd.active) fl.to = design(fl.type, fs, std::exp(bd.logF), bd.gain, std::exp(bd.logQ));
            if (snap) fl.from = fl.cur = fl.to;
            fl.moving = !(fl.from.g == fl.to.g && fl.from.k == fl.to.k && fl.from.m0 == fl.to.m0 && fl.from.m1 == fl.to.m1 &&
                          fl.from.m2 == fl.to.m2 && fl.from.k2 == fl.to.k2);
            if (!fl.moving) fl.cur = fl.to;
            for (int c = 0; c < kMaxChannels; ++c)
                for (int s = 0; s < 2; ++s) { fl.s1[c][s] = flushDenorm(fl.s1[c][s]); fl.s2[c][s] = flushDenorm(fl.s2[c][s]); }
        }
    }
    if (snap || std::fabs(outTarget_ - out_) < 1e-7f) { out_ = outTarget_; outStep_ = 0.0f; }
    else outStep_ = (outTarget_ - out_) / (float)kCtl;
}

inline float PremiumEQ::tick(Filter& fl, const Coeffs& c, float x, int ch) {
    if (fl.type == kOff) return x;
    auto stage = [&](int st, float in, float k, float m1, float a1) {
        (void)k;
        const float g = c.g;
        const float a2 = g * a1, a3 = g * a2;
        float& ic1 = fl.s1[ch][st];
        float& ic2 = fl.s2[ch][st];
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return c.m0 * in + m1 * v1 + c.m2 * v2;
    };
    const float y = stage(0, x, c.k, c.m1, c.a1);
    return c.cascade ? stage(1, y, c.k2, c.m1 * c.k2 / c.k, c.b1) : y;
}

void PremiumEQ::onProcess(AudioBlock& b) {
    const int nch = b.numChannels;
    for (int i = 0; i < b.numSamples; ++i) {
        if (ctl_ == 0) controlUpdate(false);
        const float t = (float)(ctl_ + 1) / (float)kCtl;
        for (auto& bd : bands_)
            for (int k = 0; k < 2; ++k) {
                Filter& fl = bd.f[k];
                if ((k != bd.active && bd.xfade == 0) || !fl.moving) continue;
                const Coeffs& a = fl.from; const Coeffs& z = fl.to;
                Coeffs& c = fl.cur;
                c.g = a.g + t * (z.g - a.g); c.k = a.k + t * (z.k - a.k); c.k2 = a.k2 + t * (z.k2 - a.k2);
                c.m0 = a.m0 + t * (z.m0 - a.m0); c.m1 = a.m1 + t * (z.m1 - a.m1); c.m2 = a.m2 + t * (z.m2 - a.m2);
                c.cascade = z.cascade;
                c.prep();
            }
        out_ += outStep_;
        for (int ch = 0; ch < nch; ++ch) {
            float v = b.ch[ch][i];
            if (!std::isfinite(v)) v = 0.0f;
            for (auto& bd : bands_) {
                const float yNew = tick(bd.f[bd.active], bd.f[bd.active].cur, v, ch);
                if (bd.xfade > 0) {
                    Filter& old = bd.f[1 - bd.active];
                    const float yOld = tick(old, old.cur, v, ch);
                    const float w = 1.0f - (float)bd.xfade / (float)xfadeLen_;
                    v = yOld + w * (yNew - yOld);
                } else {
                    v = yNew;
                }
            }
            b.ch[ch][i] = v * out_;
        }
        for (auto& bd : bands_) if (bd.xfade > 0) --bd.xfade;
        ctl_ = (ctl_ + 1) % kCtl;
    }
}

} // namespace mfx
