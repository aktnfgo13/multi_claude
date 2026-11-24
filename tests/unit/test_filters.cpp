#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/cabinet/Convolver.h"
#include "dsp/filters/Biquad.h"
#include "dsp/filters/EQ.h"

using namespace mfx;
using namespace mfxtest;

TEST(unit, biquad_lowpass_highpass_cutoff_minus3db) {
    const double fs = 48000.0;
    for (double fc : {100.0, 1000.0, 8000.0}) {
        auto lp = designBiquad(FilterType::LowPass, fs, fc, 0.7071);
        auto hp = designBiquad(FilterType::HighPass, fs, fc, 0.7071);
        CHECK_NEAR(db(biquadMagnitude(lp, fs, fc)), -3.01, 0.05);
        CHECK_NEAR(db(biquadMagnitude(hp, fs, fc)), -3.01, 0.05);
        CHECK_NEAR(db(biquadMagnitude(lp, fs, 1.0)), 0.0, 0.01);
        auto lp1 = designBiquad(FilterType::LowPass1, fs, fc);
        CHECK_NEAR(db(biquadMagnitude(lp1, fs, fc)), -3.01, 0.05);
    }
}

TEST(unit, biquad_peak_and_shelves_gain) {
    const double fs = 48000.0;
    auto pk = designBiquad(FilterType::Peak, fs, 1000.0, 1.0, 6.0);
    CHECK_NEAR(db(biquadMagnitude(pk, fs, 1000.0)), 6.0, 0.01);
    auto ls = designBiquad(FilterType::LowShelf, fs, 200.0, 0.7071, -9.0);
    CHECK_NEAR(db(biquadMagnitude(ls, fs, 10.0)), -9.0, 0.1);
    CHECK_NEAR(db(biquadMagnitude(ls, fs, 10000.0)), 0.0, 0.1);
    auto hs1 = designBiquad(FilterType::HighShelf1, fs, 1500.0, 0.7071, 12.0);
    CHECK_NEAR(db(biquadMagnitude(hs1, fs, 20.0)), 0.0, 0.05);
    CHECK_NEAR(db(biquadMagnitude(hs1, fs, 22000.0)), 12.0, 0.6);
    auto ls1 = designBiquad(FilterType::LowShelf1, fs, 800.0, 0.7071, -3.0);
    CHECK_NEAR(db(biquadMagnitude(ls1, fs, 5.0)), -3.0, 0.05);
}

TEST(unit, biquad_stable_over_parameter_sweep) {
    for (double fs : {44100.0, 48000.0, 96000.0})
        for (double f = 10; f < fs / 2; f *= 1.3)
            for (double q : {0.1, 0.7071, 4.0, 18.0})
                for (auto t : {FilterType::LowPass, FilterType::HighPass, FilterType::Peak, FilterType::LowShelf, FilterType::HighShelf, FilterType::BandPass, FilterType::Notch})
                    CHECK(biquadIsStable(designBiquad(t, fs, f, q, 12.0)));
}

TEST(unit, parametric_eq_response_matches_design) {
    ParametricEQ eq;
    eq.setParam("p2_db", 9.0f);
    eq.setParam("p2_hz", 1200.0f);
    eq.setParam("low_db", -6.0f);
    eq.prepare({48000.0, 256, 1});
    auto ir = processMono(eq, makeImpulse(16384), 256);
    const std::vector<double> fr = {50, 300, 1200, 5000};
    auto m = analysis::magnitudeResponseDb(ir, 48000.0, fr);
    for (size_t i = 0; i < fr.size(); ++i) CHECK_NEAR(m[i], eq.responseDb(fr[i]), 0.05);
    CHECK_NEAR(m[2], 9.0, 0.6);
}

TEST(unit, partitioned_convolution_equals_direct) {
    for (int len : {1, 50, 64, 65, 1000, 4097}) {
        auto ir = makeNoise((size_t)len, 0.3, 99);
        auto k = ConvolutionKernel::build(ir.data(), len);
        Convolver c; c.prepare(8192); c.setKernel(k.get());
        auto x = makeNoise(6000, 0.5, 5);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = c.process(x[i]);
        double err = 0.0;
        for (size_t n = 0; n < x.size(); n += 7) {
            double ref = 0.0;
            for (int m = 0; m < len && m <= (int)n; ++m) ref += (double)ir[(size_t)m] * x[n - (size_t)m];
            err = std::max(err, std::fabs(ref - y[n]));
        }
        CHECK_MSG(err < 2e-4, "len " << len << " err " << err);
    }
}
