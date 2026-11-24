// Static and stateful nonlinearities used by drive / amp blocks and the modelling comparison study.
//
//  (1) Mathematical clippers: tanh, hard clip, algebraic sigmoid (variable knee)
//  (2) Shockley-diode based static clippers (antiparallel pair), solved with Newton-Raphson
//  (3) RC + diode-pair clipper ODE (trapezoidal rule + Newton-Raphson)     -> "dynamic" circuit model
//  (4) Wave Digital Filter diode clipper (Wright-omega explicit diode pair) -> real-time circuit model
//
// Physical units for (2)-(4): volts. Digital full scale 1.0 is treated as 1 V at the circuit input
// unless the caller scales it.
#pragma once

#include "core/Common.h"

namespace mfx {
namespace nl {

/// Algebraic sigmoid: slope 1 at 0, |y| -> 1. p controls knee hardness (p=2 ~ soft, p>8 ~ hard).
inline float algebraicSigmoid(float u, float p) {
    const float a = std::fabs(u);
    if (a < 1e-9f) return u;
    return u / std::pow(1.0f + std::pow(a, p), 1.0f / p);
}

/// Asymmetric version: separate positive / negative saturation levels (vp, vn > 0).
inline float asymSigmoid(float u, float p, float vp, float vn) {
    return u >= 0.0f ? vp * algebraicSigmoid(u / vp, p) : vn * algebraicSigmoid(u / vn, p);
}

inline float hardClip(float u, float lim = 1.0f) { return clampv(u, -lim, lim); }

/// Fast rational tanh approximation (max error ~2e-4), clamped.
inline float fastTanh(float x) {
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/// Feedback-diode (op-amp feedback pair) style soft limiter: V = a * asinh(u / a).
/// Derived from i = 2 Is sinh(v / nVt) solved for v: logarithmic growth above ~a.
inline float diodeFeedback(float u, float a) { return a * std::asinh(u / a); }

struct DiodeParams {
    double Is = 2.52e-9;   // saturation current (A) ~1N4148
    double n = 1.752;      // emission coefficient
    double Vt = 0.02585;   // thermal voltage (V)
    double nVt() const { return n * Vt; }
};

/// Static diode-pair-to-ground clipper: (x - v)/R = 2 Is sinh(v / nVt). Newton-Raphson.
class StaticDiodeClipper {
public:
    void setParams(double R, const DiodeParams& d) { R_ = R; d_ = d; }
    void reset() { v_ = 0.0; }
    double process(double x);
    int lastIterations() const { return iters_; }
private:
    double R_ = 2200.0, v_ = 0.0;
    DiodeParams d_;
    int iters_ = 0;
};

/// RC diode clipper ODE:  C dv/dt = (x - v)/R - 2 Is sinh(v/nVt).
/// Trapezoidal (bilinear) integration, Newton-Raphson per sample.
class RCDiodeClipper {
public:
    void prepare(double fs) { T_ = 1.0 / fs; reset(); }
    void setParams(double R, double C, const DiodeParams& d) { R_ = R; C_ = C; d_ = d; }
    void reset() { v_ = 0.0; fPrev_ = 0.0; }
    double process(double x);
    double state() const { return v_; }
private:
    double f(double v, double x) const;
    double T_ = 1.0 / 48000.0, R_ = 2200.0, C_ = 10e-9, v_ = 0.0, fPrev_ = 0.0;
    DiodeParams d_;
};

/// Wright omega function: omega4 approximation (D'Angelo et al.) + one Newton refinement (explicit, fixed cost).
double wrightOmega4(double x);

/// Wave Digital Filter: resistive voltage source (R) -> parallel adaptor {C, root: diode pair}.
class WDFDiodeClipper {
public:
    void prepare(double fs) { fs_ = fs; update(); reset(); }
    void setParams(double R, double C, const DiodeParams& d) { R_ = R; C_ = C; d_ = d; update(); }
    void reset() { bC_ = 0.0; }
    double process(double x);
private:
    void update();
    double fs_ = 48000.0, R_ = 2200.0, C_ = 10e-9;
    double Rc_ = 0.0, G1_ = 0.0, G2_ = 0.0, G3_ = 0.0, R3_ = 0.0;
    double bC_ = 0.0;
    DiodeParams d_;
};

} // namespace nl
} // namespace mfx
