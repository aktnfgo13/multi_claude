#include "dsp/nonlinear/Nonlinear.h"

#include <cmath>

namespace mfx {
namespace nl {

double StaticDiodeClipper::process(double x) {
    const double nVt = d_.nVt(), Is = d_.Is, R = R_;
    // initial guess: previous solution, limited to a plausible range
    double v = v_;
    iters_ = 0;
    for (int it = 0; it < 50; ++it) {
        const double s = std::sinh(v / nVt), c = std::cosh(v / nVt);
        const double g = (x - v) / R - 2.0 * Is * s;
        const double dg = -1.0 / R - 2.0 * Is * c / nVt;
        double step = g / dg;
        // damp very large steps (exp overflow guard)
        step = clampv(step, -0.1, 0.1);
        v -= step;
        ++iters_;
        if (std::fabs(step) < 1e-10) break;
    }
    v_ = v;
    return v;
}

double RCDiodeClipper::f(double v, double x) const {
    return ((x - v) / R_ - 2.0 * d_.Is * std::sinh(v / d_.nVt())) / C_;
}

double RCDiodeClipper::process(double x) {
    const double nVt = d_.nVt();
    const double vp = v_;
    const double rhsConst = vp + 0.5 * T_ * fPrev_;
    double v = vp;
    for (int it = 0; it < 50; ++it) {
        const double fv = f(v, x);
        const double g = v - rhsConst - 0.5 * T_ * fv;
        const double dfdv = (-1.0 / R_ - 2.0 * d_.Is * std::cosh(v / nVt) / nVt) / C_;
        const double dg = 1.0 - 0.5 * T_ * dfdv;
        double step = clampv(g / dg, -0.1, 0.1);
        v -= step;
        if (std::fabs(step) < 1e-10) break;
    }
    v_ = v;
    fPrev_ = f(v, x);
    return v;
}

double wrightOmega4(double x) {
    const double x1 = -3.341459552768620, x2 = 8.0;
    const double a = -1.314293149877800e-3, b = 4.775931364975583e-2;
    const double c = 3.631952663804445e-1, d = 6.313183464296682e-1;
    double y;
    if (x < x1) y = 0.0;
    else if (x < x2) y = d + x * (c + x * (b + x * a));
    else y = x - std::log(x);
    // omega4: one Newton-like refinement step
    y = y - (y - std::exp(x - y)) / (y + 1.0);
    // + one Newton step on f(y) = y + ln(y) - x (cheap, brings the residual to ~1e-6)
    if (y > 1e-12) y = y - (y + std::log(y) - x) / (1.0 + 1.0 / y);
    return y;
}

void WDFDiodeClipper::update() {
    Rc_ = 1.0 / (2.0 * fs_ * C_); // capacitor port resistance T/(2C)
    G1_ = 1.0 / R_;
    G2_ = 1.0 / Rc_;
    G3_ = G1_ + G2_;
    R3_ = 1.0 / G3_;
}

double WDFDiodeClipper::process(double x) {
    // leaves -> adaptor
    const double a1 = x;      // resistive voltage source reflects Vs
    const double a2 = bC_;    // capacitor reflects stored wave
    const double b3 = (G1_ * a1 + G2_ * a2) / G3_; // wave towards root (adapted port)
    // root: antiparallel diode pair, explicit Wright-omega solution (sign symmetric approx.)
    const double nVt = d_.nVt(), Is = d_.Is, R = R3_;
    const double absA = std::fabs(b3);
    const double lam = b3 >= 0.0 ? 1.0 : -1.0;
    const double arg = std::log(R * Is / nVt) + (absA + R * Is) / nVt;
    const double a3 = lam * (absA + 2.0 * R * Is - 2.0 * nVt * wrightOmega4(arg));
    // adaptor -> leaves
    const double toC = a3 + b3 - a2;
    bC_ = toC;               // capacitor: next reflected wave = current incident wave
    return 0.5 * (a3 + b3);  // node voltage
}

namespace {
struct KneeTable {
    static constexpr double kH = 1.0 / 32.0, kMax = 64.0;
    static constexpr int kN = (int)(kMax / kH) + 1;
    double g[kN] = {}, d[kN] = {};
    explicit KneeTable(int n) {
        auto f = [n](double z) { return z / std::pow(1.0 + std::pow(z, n), 1.0 / n); };
        g[0] = 0.0; d[0] = 0.0;
        for (int i = 1; i < kN; ++i) {
            const double a = (i - 1) * kH;
            const int sub = 16; const double hs = kH / sub; // composite Simpson
            double acc = f(a) + f(a + kH);
            for (int j = 1; j < sub; ++j) acc += (j & 1 ? 4.0 : 2.0) * f(a + j * hs);
            g[i] = g[i - 1] + acc * hs / 3.0;
            d[i] = f(i * kH);
        }
    }
    double eval(double z) const { // even function
        z = std::fabs(z);
        if (z >= kMax) return g[kN - 1] + (z - kMax);
        const double x = z / kH;
        const int i = (int)x;
        const double t = x - i, t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * g[i] + (t3 - 2 * t2 + t) * kH * d[i] + (-2 * t3 + 3 * t2) * g[i + 1] + (t3 - t2) * kH * d[i + 1];
    }
};
const KneeTable& table4() { static const KneeTable t(4); return t; }
const KneeTable& table6() { static const KneeTable t(6); return t; }
} // namespace

void kneeAntiderivativeInit() { (void)table4(); (void)table6(); }

double kneeAntiderivative(double z, const KneeWeights& k) {
    double g = 0.0;
    if (k.w2 != 0.0f) g += k.w2 * std::sqrt(1.0 + z * z);
    if (k.w4 != 0.0f) g += k.w4 * table4().eval(z);
    if (k.w6 != 0.0f) g += k.w6 * table6().eval(z);
    return g;
}

} // namespace nl
} // namespace mfx
