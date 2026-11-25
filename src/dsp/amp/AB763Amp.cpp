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
}

void AB763Amp::updateDsp(double fs, double fsOs) {
    stack_.design(BlackfaceToneStack::Values{}, std::clamp(knob(kTreble) / 10.0, 0.0, 1.0), bassTaper(knob(kBass)), fsOs);
    couple_.design(FilterType::HighPass1, fsOs, 1.0 / (2.0 * 3.14159265358979323846 * 0.022e-6 * 1e6));
    dcIn_.design(FilterType::HighPass1, fs, 10.0);
    volGain_ = (float)(volumeTaper(knob(kVolume)) * kMixingLoss);
}

float AB763Amp::pre(float x, int ch) { return dcIn_.process(x, ch); }

float AB763Amp::nonlinear(float u, int ch) {
    const float vin = u * (float)kVoltsPerFullScale;
    float v = v1_.process(vin);                // V1A (inverting): plate AC volts
    v = stack_.process(v, ch);                 // tone stack (DC blocked by its s-term)
    v = v2_.process(v * volGain_);             // volume + mixing network -> recovery stage
    v = couple_.process(v, ch);                // 0.022uF / 1M coupling
    const float z = v / (float)kPowerClipVolts;
    return nl::kneeCurve(z, powerKnee_) * 0.7f; // power section M1, full power ~ -3 dBFS peak
}

float AB763Amp::post(float v, int ch) { return dcOut_.process(v, ch); }

} // namespace mfx
