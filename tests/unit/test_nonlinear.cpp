#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/nonlinear/Nonlinear.h"

using namespace mfx;
using namespace mfxtest;

TEST(unit, algebraic_sigmoid_slope_and_bounds) {
    for (float p : {1.5f, 2.0f, 4.0f, 10.0f}) {
        CHECK_NEAR(nl::algebraicSigmoid(1e-4f, p) / 1e-4f, 1.0, 1e-3);
        CHECK(std::fabs(nl::algebraicSigmoid(1000.0f, p)) <= 1.0f);
        CHECK(nl::algebraicSigmoid(-3.0f, p) == -nl::algebraicSigmoid(3.0f, p));
    }
    // harder knee -> closer to the linear asymptote right below the corner
    CHECK(nl::algebraicSigmoid(0.7f, 10.0f) > nl::algebraicSigmoid(0.7f, 2.0f));
}

TEST(unit, static_diode_newton_satisfies_kcl) {
    nl::StaticDiodeClipper c;
    nl::DiodeParams d;
    c.setParams(2200.0, d);
    for (double x : {-20.0, -1.0, -0.2, 0.0, 0.05, 0.6, 5.0, 40.0}) {
        const double v = c.process(x);
        const double residual = (x - v) / 2200.0 - 2.0 * d.Is * std::sinh(v / d.nVt());
        CHECK_MSG(std::fabs(residual) < 1e-9, "x=" << x << " residual " << residual);
        CHECK(std::fabs(v) < 1.0); // silicon pair clamps below ~0.8 V
    }
}

TEST(unit, wdf_matches_rc_ode_diode_clipper) {
    // Both discretise the capacitor with the bilinear transform; differences come only from the
    // explicit Wright-omega diode-pair approximation.
    const double fs = 192000.0;
    nl::DiodeParams d;
    nl::RCDiodeClipper ode; ode.prepare(fs); ode.setParams(2200.0, 10e-9, d);
    nl::WDFDiodeClipper wdf; wdf.prepare(fs); wdf.setParams(2200.0, 10e-9, d);
    std::vector<float> a, b;
    for (int i = 0; i < 40000; ++i) {
        const double x = 3.0 * std::sin(2.0 * kPi * 440.0 * i / fs);
        a.push_back((float)ode.process(x));
        b.push_back((float)wdf.process(x));
    }
    const double e = analysis::esr(a.data() + 1000, b.data() + 1000, a.size() - 1000);
    record("wdf_vs_ode_esr", e);
    CHECK_MSG(e < 1e-3, "esr " << e);
}

TEST(unit, wright_omega_accuracy) {
    for (double x : {-2.0, 0.0, 1.0, 5.0, 20.0}) {
        const double w = nl::wrightOmega4(x);
        // omega + log(omega) = x
        CHECK_NEAR(w + std::log(w), x, 1e-4);
    }
}
