#include "dsp/amp/AB763Amp.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mfx {

// ---------------------------------------------------------------- triode stage
double TriodeStageTable::plateCurrent(double vpk, double vgk) {
    // Koren 12AX7: mu 100, ex 1.4, kg1 1060, kp 600, kvb 300
    constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;
    if (vpk <= 0.0) return 0.0;
    const double a = kp * (1.0 / mu + vgk / std::sqrt(kvb + vpk * vpk));
    const double sp = a > 30.0 ? a : std::log1p(std::exp(a)); // softplus, overflow-safe
    const double e1 = vpk / kp * sp;
    return e1 > 0.0 ? 2.0 * std::pow(e1, ex) / kg1 : 0.0;
}

void TriodeStageTable::build(const Circuit& c, double vinMin, double vinMax, int points) {
    // quiescent point (no signal): cathode follows the plate current through Rk
    double lo = 0.0, hi = c.bPlus / (c.ra + c.rk);
    for (int it = 0; it < 100; ++it) {
        const double ip = 0.5 * (lo + hi);
        const double f = ip - plateCurrent(c.bPlus - ip * (c.ra + c.rk), -ip * c.rk);
        (f > 0.0 ? hi : lo) = ip;
    }
    const double ip0 = 0.5 * (lo + hi);
    vk0_ = ip0 * c.rk;            // cathode held at this voltage by the bypass capacitor (AC)
    vp0_ = c.bPlus - ip0 * c.ra;
    constexpr double kGrid = 2.0e-4; // grid current Ig = kGrid * max(0, Vgk)^1.5  [A]
    table_.resize((size_t)points);
    const double step = (vinMax - vinMin) / (points - 1);
    for (int i = 0; i < points; ++i) {
        const double vin = vinMin + i * step;
        // grid node: vin - vg = Rs * Ig(vg - Vk0)   (only conducts above the cathode)
        double vg = vin;
        if (vin > vk0_) {
            double a = vk0_, b = vin;
            for (int it = 0; it < 80; ++it) {
                const double m = 0.5 * (a + b);
                const double ig = kGrid * std::pow(std::max(0.0, m - vk0_), 1.5);
                (vin - m - c.rs * ig > 0.0 ? a : b) = m;
            }
            vg = 0.5 * (a + b);
        }
        const double vgk = vg - vk0_;
        // plate: (B+ - Vp)/Ra = Ip(Vp - Vk0, Vgk), decreasing in Vp
        double pa = vk0_, pb = c.bPlus;
        for (int it = 0; it < 80; ++it) {
            const double vp = 0.5 * (pa + pb);
            const double f = (c.bPlus - vp) / c.ra - plateCurrent(vp - vk0_, vgk);
            (f > 0.0 ? pa : pb) = vp;
        }
        table_[(size_t)i] = (float)(0.5 * (pa + pb) - vp0_);
    }
    vMin_ = (float)vinMin;
    invStep_ = (float)(1.0 / step);
}

float TriodeStageTable::process(float vin) const {
    float x = (vin - vMin_) * invStep_;
    if (!(x == x)) x = (0.0f - vMin_) * invStep_; // NaN input -> treat as 0 V (no UB in the index)
    const float last = (float)(table_.size() - 1);
    x = std::min(std::max(x, 0.0f), last - 1e-3f);
    const int i = (int)x;
    const float t = x - (float)i;
    return table_[(size_t)i] + t * (table_[(size_t)i + 1] - table_[(size_t)i]);
}

// ---------------------------------------------------------------- tone stack
void BlackfaceToneStack::analog(const Values& v, double t, double l, double b[4], double a[4]) {
    // Yeh & Smith, "Discretization of the '59 Fender Bassman tone stack" (DAFx 2006); mid pot -> fixed r3 (m = 1)
    const double C1 = v.c1, C2 = v.c2, C3 = v.c3, R1 = v.r1, R2 = v.r2, R3 = v.r3, R4 = v.r4, m = 1.0;
    b[0] = 0.0;
    b[1] = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
    b[2] = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
         + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
         + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
    b[3] = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
         - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
         + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
         + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
         + t * l * C1 * C2 * C3 * R1 * R2 * R4;
    a[0] = 1.0;
    a[1] = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
    a[2] = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
         + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
         + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
    a[3] = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
         - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
         + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
         + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
}

double BlackfaceToneStack::analogMagnitude(const Values& v, double t, double l, double f) {
    double b[4], a[4];
    analog(v, t, l, b, a);
    const std::complex<double> s(0.0, 2.0 * 3.14159265358979323846 * f);
    const auto num = b[1] * s + b[2] * s * s + b[3] * s * s * s;
    const auto den = a[0] + a[1] * s + a[2] * s * s + a[3] * s * s * s;
    return std::abs(num / den);
}

void BlackfaceToneStack::design(const Values& v, double t, double l, double fs) {
    double b[4], a[4];
    analog(v, t, l, b, a);
    // denominator a3 s^3 + a2 s^2 + a1 s + 1: passive RC network -> real negative poles.
    // Newton from s = 0 converges to the pole closest to the origin.
    double r = 0.0;
    for (int it = 0; it < 200; ++it) {
        const double p = ((a[3] * r + a[2]) * r + a[1]) * r + a[0];
        const double dp = (3.0 * a[3] * r + 2.0 * a[2]) * r + a[1];
        if (dp == 0.0) break;
        const double nr = r - p / dp;
        if (std::fabs(nr - r) <= 1e-12 * std::max(1.0, std::fabs(nr))) { r = nr; break; }
        r = nr;
    }
    // deflate: (s - r)(a3 s^2 + e1 s + e0)
    const double e2 = a[3], e1 = a[2] + a[3] * r, e0 = a[1] + e1 * r;
    const double K = 2.0 * fs;
    // section 1: s / (s - r)
    {
        const double a0 = K - r;
        BiquadCoeffs c;
        c.b0 = (float)(K / a0); c.b1 = (float)(-K / a0); c.b2 = 0.0f;
        c.a1 = (float)((-K - r) / a0); c.a2 = 0.0f;
        s1_.setCoeffs(c);
    }
    // section 2: (b3 s^2 + b2 s + b1) / (e2 s^2 + e1 s + e0)
    {
        const double n2 = b[3], n1 = b[2], n0 = b[1];
        const double K2 = K * K;
        const double a0 = e2 * K2 + e1 * K + e0;
        BiquadCoeffs c;
        c.b0 = (float)((n2 * K2 + n1 * K + n0) / a0);
        c.b1 = (float)(2.0 * (n0 - n2 * K2) / a0);
        c.b2 = (float)((n2 * K2 - n1 * K + n0) / a0);
        c.a1 = (float)(2.0 * (e0 - e2 * K2) / a0);
        c.a2 = (float)((e2 * K2 - e1 * K + e0) / a0);
        s2_.setCoeffs(c);
    }
}

double BlackfaceToneStack::digitalMagnitude(double f, double fs) const {
    const std::complex<double> z1 = std::polar(1.0, -2.0 * 3.14159265358979323846 * f / fs);
    auto h = [&](const BiquadCoeffs& c) {
        return ((double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z1 * z1) / (1.0 + (double)c.a1 * z1 + (double)c.a2 * z1 * z1);
    };
    return std::abs(h(s1_.coeffs()) * h(s2_.coeffs()));
}

namespace {
// Koren beam tetrode / pentode (6V6GT, literature fit): plate current [A] for Vg1 (grid-cathode),
// Vg2 (screen) and Va (plate). Bounded and finite for any input.
inline float tetrodeCurrent(float vg1, float vg2, float va) {
    constexpr float mu = 10.7f, ex = 1.31f, kg1 = 1672.0f, kp = 41.16f, kvb = 12.7f;
    vg2 = std::max(vg2, 1.0f);
    const float a = kp * (1.0f / mu + vg1 / vg2);
    const float sp = a > 20.0f ? a : std::log1p(std::exp(a));
    const float e1 = vg2 / kp * sp;
    if (e1 <= 0.0f) return 0.0f;
    const float i = 2.0f * std::pow(e1, ex) / kg1 * std::atan(std::max(va, 0.0f) / kvb);
    return std::min(i, 0.2f); // 6V6 emission limit (bounded)
}
inline float finiteOr(float v, float alt) { return std::isfinite(v) ? v : alt; }
} // namespace

// ---------------------------------------------------------------- amp
double AB763Amp::volumeTaper(double k) {
    const double x = std::clamp(k / 10.0, 0.0, 1.0);
    return (std::pow(10.0, 2.0 * x) - 1.0) / 99.0; // 1M audio pot: ~10 % at mid rotation
}
double AB763Amp::bassTaper(double k) {
    const double x = std::clamp(k / 10.0, 0.0, 1.0);
    return x <= 0.0 ? 0.0 : std::exp((x - 1.0) * 3.4);
}

AB763Amp::AB763Amp() {
    params_.add({"volume", "Volume", 0, 10, 4, ParamUnit::None});
    params_.add({"treble", "Treble", 0, 10, 6, ParamUnit::None});
    params_.add({"bass", "Bass", 0, 10, 4, ParamUnit::None});
    addCommonParams(0.0f, 1); // mix, level (dB), oversampling (default 2x)
    static const char* const kModels[] = {"M1", "M2 (PI + 6V6 + sag + NFB + load)"};
    ParamInfo m{"model", "Model", 0, 1, (float)kM2, ParamUnit::Choice};
    m.choices = kModels; m.numChoices = 2;
    params_.add(m); // index kModelParam
}

void AB763Amp::onParametersChanged(bool immediate) {
    const int m = clampv(params_.getInt(kModelParam), 0, 1);
    if (m != model_) { model_ = m; resetDsp(); }
    OversampledDrive::onParametersChanged(immediate);
}

void AB763Amp::prepareDsp(double fs, double /*fsOs*/) {
    // tables depend only on circuit values (volts) -> sample-rate independent; built off the audio thread
    if (!tablesBuilt_) { // constant circuit -> built once per instance
        v1_.build({250.0, 100e3, 1.5e3, 68e3});
        v2_.build({250.0, 100e3, 1.5e3, 220e3});
        tablesBuilt_ = true;
    }
    dcOut_.prepare(fs, 5.0);
    powerKnee_ = nl::kneeWeights(3.0f);
}

void AB763Amp::resetDsp() {
    stack_.reset();
    couple_.reset();
    dcIn_.reset();
    dcOut_.reset();
    for (Biquad* b : {&gridA_, &gridB_, &otHp_, &otLp_, &spkRes_, &spkLe_, &fbLp_}) b->reset();
    for (int c = 0; c < kMaxChannels; ++c) {
        bias_[c][0] = bias_[c][1] = 0.0f;
        vPrim_[c] = vSpk_[c] = 0.0f;
        bPlus_[c] = (float)kSupplyIdle;
        preScale_[c] = 1.0f;
        gEst_[c] = g0_; vdPrev_[c] = iPrev_[c] = iSecLast_[c] = 0.0f;
    }
}

void AB763Amp::updateDsp(double fs, double fsOs) {
    stack_.design(BlackfaceToneStack::Values{}, std::clamp(knob(kTreble) / 10.0, 0.0, 1.0), bassTaper(knob(kBass)), fsOs);
    couple_.design(FilterType::HighPass1, fsOs, 1.0 / (2.0 * 3.14159265358979323846 * 0.022e-6 * 1e6));
    dcIn_.design(FilterType::HighPass1, fs, 10.0);
    volGain_ = (float)(volumeTaper(knob(kVolume)) * kMixingLoss);
    // ---- M2 linear parts (sample-rate independent: analog values, bilinear at the OS rate)
    constexpr double kPi = 3.14159265358979323846;
    gridA_.design(FilterType::HighPass1, fsOs, 1.0 / (2.0 * kPi * 0.1e-6 * 220e3));
    gridB_.design(FilterType::HighPass1, fsOs, 1.0 / (2.0 * kPi * 0.1e-6 * 220e3));
    otHp_.design(FilterType::HighPass1, fsOs, 25.0);           // primary inductance with source impedance
    otLp_.design(FilterType::LowPass1, fsOs, std::min(18000.0, 0.45 * fsOs)); // leakage inductance
    // speaker: Z = Re + Zle + Zres;  Zres = parallel RLC (fs 95 Hz, peak +36 ohm, Q 2.5) as a band-pass
    {
        const double f0 = 95.0, rRes = 36.0, q = 2.5;
        // RBJ band-pass (0 dB peak) scaled by rRes: Zres(j w0) = rRes
        BiquadCoeffs c = designBiquad(FilterType::BandPass, fsOs, f0, q);
        c.b0 *= (float)rRes; c.b1 *= (float)rRes; c.b2 *= (float)rRes;
        spkRes_.setCoeffs(c);
    }
    {
        // lossy voice-coil inductance: Zle(s) = s Le Rl / (Rl + s Le)  (Le 0.4 mH, Rl 18 ohm): high-pass shaped
        const double le = 0.4e-3, rl = 18.0;
        BiquadCoeffs c = designBiquad(FilterType::HighPass1, fsOs, rl / (2.0 * kPi * le));
        c.b0 *= (float)rl; c.b1 *= (float)rl; c.b2 *= (float)rl;
        spkLe_.setCoeffs(c);
    }
    fbLp_.design(FilterType::LowPass, fsOs, std::min(kFbBandwidth, 0.2 * fsOs), 0.7071);
    dtOs_ = (float)(1.0 / fsOs);
    {   // small-signal forward transconductance at idle (seeds the delay-free loop solver)
        const float vg2 = (float)kSupplyIdle - 15.0f, e = 0.05f;
        const float gm = (tetrodeCurrent(-34.0f + e, vg2, (float)kSupplyIdle) - tetrodeCurrent(-34.0f - e, vg2, (float)kSupplyIdle)) / (2.0f * e);
        g0_ = -(float)kTurns * 0.5f * gm * 1.82f * 27.0f;
    }
    gridDecay_ = (float)(1.0 - std::exp(-1.0 / (fsOs * 220e3 * 0.1e-6))); // blocking charge leaks with Rg*C
    preAlpha_ = (float)(1.0 - std::exp(-1.0 / (fsOs * 0.05)));           // preamp supply RC ~ 50 ms
}

float AB763Amp::pre(float x, int ch) { return dcIn_.process(x, ch); }

float AB763Amp::m1Power(float v) const {
    const float z = v / (float)kPowerClipVolts;
    return nl::kneeCurve(z, powerKnee_) * 0.7f; // power section M1, full power ~ -3 dBFS peak
}


float AB763Amp::m2Forward(float vd, int ch) {
    // ---- phase inverter (long-tailed pair): differential soft limit, 82k / 100k plate-load imbalance
    constexpr float kPiGain = 27.0f, kPiSwing = 60.0f; // differential gain, output swing limit [V]
    const float d = kPiSwing * nl::kneeCurve(kPiGain * vd / kPiSwing, nl::KneeWeights{});
    const float a = gridA_.process(-0.82f * d, ch);
    const float b = gridB_.process(d, ch);
    // ---- grid conduction on the 6V6 grids: coupling caps charge -> bias shift (blocking distortion)
    constexpr float kBias = -34.0f;
    float g[2] = {a - bias_[ch][0] + kBias, b - bias_[ch][1] + kBias};
    for (int k = 0; k < 2; ++k) {
        if (g[k] > 0.0f) { bias_[ch][k] += 0.02f * g[k]; g[k] *= 0.02f; } // grid current clamps near 0 V
        bias_[ch][k] -= gridDecay_ * bias_[ch][k];
        bias_[ch][k] = finiteOr(clampv(bias_[ch][k], -5.0f, 60.0f), 0.0f);
    }
    // ---- 6V6 pair, plates at B+ -/+ half the primary swing (previous sample)
    const float bp = bPlus_[ch];
    const float vg2 = bp - 15.0f;
    const float half = 0.5f * vPrim_[ch];
    const float ia = tetrodeCurrent(g[0], vg2, bp - half);
    const float ib = tetrodeCurrent(g[1], vg2, bp + half);
    // ---- sag: rectifier charges the reservoir, tube currents (plates + 10 % screen + 10 mA preamp) discharge it
    constexpr float kSource = 460.0f, kRect = 500.0f, kCap = 20e-6f;
    const float iLoad = 1.1f * (ia + ib) + 0.010f;
    const float iChg = std::max(0.0f, (kSource - bp) / kRect);
    bPlus_[ch] = finiteOr(clampv(bp + (iChg - iLoad) * dtOs_ / kCap, 150.0f, kSource), (float)kSupplyIdle);
    preScale_[ch] += preAlpha_ * (bPlus_[ch] / (float)kSupplyIdle - preScale_[ch]);
    return (ia - ib) * (float)kTurns * 0.5f; // push-pull primary current -> secondary current
}

float AB763Amp::m2Power(float v, int ch) {
    // Delay-free negative feedback: the load + feedback filters are linear, so their next output is
    // b0 * input + state1. With the forward path linearised around the previous operating point
    // (i ~ i_prev + G (vd - vd_prev), G < 0 = inverting), the loop equation is solved in closed form:
    //   vd = v_pi + beta * fb(z(i(vd)))   ->   vd = num / (1 - beta * kf * kz * G)   (denominator > 1)
    // then the nonlinear forward path is evaluated once at that vd (states committed).
    const float beta = (float)kNfb;
    const float vpi = v * (float)kPiInput;
    const float kz0 = 6.0f + spkRes_.coeffs().b0 + spkLe_.coeffs().b0;          // inner load b0
    const float kz = otLp_.coeffs().b0 * otHp_.coeffs().b0 * kz0;               // z = kz * i + zs
    const float zs = otLp_.coeffs().b0 * (otHp_.coeffs().b0 * (spkRes_.state1(ch) + spkLe_.state1(ch)) + otHp_.state1(ch))
                   + otLp_.state1(ch);
    const float kf = fbLp_.coeffs().b0, fs = fbLp_.state1(ch);                 // fb = kf * z + fs
    const float G = gEst_[ch];
    const float num = vpi + beta * kf * kz * (iPrev_[ch] - G * vdPrev_[ch]) + beta * (kf * zs + fs);
    const float den = 1.0f - beta * kf * kz * G;
    const float vd = finiteOr(num / den, 0.0f);
    const float iSec = m2Forward(vd, ch);
    // secant update of the forward transconductance (bounded, smoothed)
    const float dv = vd - vdPrev_[ch];
    if (std::fabs(dv) > 1e-4f) {
        const float gNew = clampv((iSec - iPrev_[ch]) / dv, 3.0f * g0_, 0.0f);
        gEst_[ch] = 0.5f * gEst_[ch] + 0.5f * gNew;
    }
    vdPrev_[ch] = vd; iPrev_[ch] = iSec;
    // ---- output transformer + reactive load (current drive), feedback network
    float z = 6.0f * iSec + spkRes_.process(iSec, ch) + spkLe_.process(iSec, ch); // speaker voltage (8 ohm tap)
    z = otLp_.process(otHp_.process(z, ch), ch);
    z = finiteOr(clampv(z, -200.0f, 200.0f), 0.0f);
    fbLp_.process(z, ch);
    vSpk_[ch] = z;
    vPrim_[ch] = z * (float)kTurns;
    return z * (1.0f / 25.0f);                                  // ~ 20 W into 8 ohm -> about -3 dBFS
}

float AB763Amp::nonlinear(float u, int ch) {
    const float vin = u * (float)kVoltsPerFullScale;
    float v = v1_.process(vin);                // V1A (inverting): plate AC volts
    v = stack_.process(v, ch);                 // tone stack (DC blocked by its s-term)
    if (model_ == kM2) v *= preScale_[ch];     // preamp B+ follows the (slow) supply sag
    v = v2_.process(v * volGain_);             // volume + mixing network -> recovery stage
    v = couple_.process(v, ch);                // 0.022uF / 1M coupling
    return model_ == kM2 ? m2Power(v, ch) : m1Power(v);
}

float AB763Amp::post(float v, int ch) { return dcOut_.process(v, ch); }

} // namespace mfx
