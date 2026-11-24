#include <complex>
#include <cstdio>

#include "TestFramework.h"
#include "TestUtil.h"
#include "core/Parameter.h"
#include "core/Smoothed.h"
#include "dsp/fft/FFT.h"
#include "dsp/oversampling/Oversampler.h"
#include "dsp/utility/DelayLine.h"
#include "dsp/utility/Resample.h"
#include "dsp/utility/Tuner.h"
#include "platform/WavFile.h"

using namespace mfx;
using namespace mfxtest;

TEST(unit, smoothing_reaches_target_linearly) {
    LinearSmoothed s;
    s.reset(48000.0, 0.01); // 480 samples
    s.setImmediate(0.0f);
    s.setTarget(1.0f);
    float prev = 0.0f, maxStep = 0.0f;
    for (int i = 0; i < 480; ++i) { float v = s.next(); maxStep = std::max(maxStep, v - prev); prev = v; }
    CHECK_NEAR(prev, 1.0, 1e-6);
    CHECK(!s.isSmoothing());
    CHECK(maxStep < 1.0f / 480.0f + 1e-5f);
}

TEST(unit, parameter_clamp_discrete_and_log_mapping) {
    ParameterSet p;
    const char* ch[] = {"a", "b", "c"};
    ParamInfo c{"choice", "Choice", 0, 2, 0, ParamUnit::Choice}; c.choices = ch; c.numChoices = 3;
    const int ic = p.add(c);
    const int iF = p.add({"f", "F", 20, 20000, 1000, ParamUnit::Hertz, ParamScale::Log});
    p.set(ic, 1.6f);
    CHECK(p.getInt(ic) == 2);
    p.set(ic, 99.0f);
    CHECK(p.getInt(ic) == 2);
    p.set(iF, 50000.0f);
    CHECK_NEAR(p.get(iF), 20000.0, 1e-3);
    CHECK_NEAR(p.fromNormalized(iF, 0.5f), std::sqrt(20.0 * 20000.0), 0.5);
    CHECK_NEAR(p.toNormalized(iF, p.fromNormalized(iF, 0.3f)), 0.3, 1e-5);
    p.set(iF, std::nanf(""));
    CHECK_NEAR(p.get(iF), 20000.0, 1e-3); // NaN rejected
    CHECK(p.consumeDirty());
    CHECK(!p.consumeDirty());
}

TEST(unit, fft_roundtrip_and_parseval) {
    FFT fft(1024);
    std::vector<std::complex<float>> a(1024), b;
    auto x = makeNoise(1024, 1.0);
    for (int i = 0; i < 1024; ++i) a[(size_t)i] = {x[(size_t)i], 0.0f};
    b = a;
    fft.forward(a.data());
    double et = 0, ef = 0;
    for (int i = 0; i < 1024; ++i) { et += std::norm(b[(size_t)i]); ef += std::norm(a[(size_t)i]); }
    CHECK_NEAR(ef / 1024.0, et, et * 1e-4);
    fft.inverse(a.data());
    double err = 0;
    for (int i = 0; i < 1024; ++i) err = std::max(err, (double)std::abs(a[(size_t)i] - b[(size_t)i]));
    CHECK(err < 1e-5);
}

TEST(unit, oversampler_latency_is_integer_and_reported) {
    for (int idx = 1; idx <= 3; ++idx) {
        Oversampler os;
        os.prepare(64, 1);
        os.setFactorIndex(idx);
        std::vector<float> x(256, 0.0f), y(256, 0.0f);
        x[10] = 1.0f;
        for (int p = 0; p < 256; p += 64) {
            os.upsample(x.data() + p, 64, 0);
            os.downsample(y.data() + p, 64, 0);
        }
        int arg = 0;
        for (int i = 0; i < 256; ++i) if (std::fabs(y[(size_t)i]) > std::fabs(y[(size_t)arg])) arg = i;
        CHECK_MSG(arg - 10 == Oversampler::latencyFor(idx), "factor idx " << idx << " peak at " << arg - 10);
        CHECK(y[(size_t)arg] > 0.95f); // round trip of an impulse is nearly symmetric & unity
        record("oversampler_latency_" + std::to_string(1 << idx) + "x", Oversampler::latencyFor(idx), "samples");
    }
}

TEST(unit, oversampler_passband_and_image_rejection) {
    const double fs = 48000.0;
    for (int idx = 1; idx <= 3; ++idx) {
        // passband: 1 kHz .. 18 kHz flat within 0.1 dB (round trip)
        for (double f : {1000.0, 10000.0, 18000.0}) {
            Oversampler os; os.prepare(256, 1); os.setFactorIndex(idx);
            auto x = makeSine(fs, f, 0.5, 8192);
            std::vector<float> y(8192);
            for (int p = 0; p < 8192; p += 256) { os.upsample(x.data() + p, 256, 0); os.downsample(y.data() + p, 256, 0); }
            const double g = analysis::rms(y.data() + 4096, 4096) / analysis::rms(x.data() + 4096, 4096);
            CHECK_MSG(std::fabs(db(g)) < 0.1, "f=" << f << " idx=" << idx << " gain dB=" << db(g));
        }
        // images: content of an upsampled 5 kHz tone above the original Nyquist (stage 1 rejection)
        Oversampler os; os.prepare(256, 1); os.setFactorIndex(idx);
        auto x = makeSine(fs, 5000.0, 0.5, 8192);
        std::vector<float> up;
        for (int p = 0; p < 8192; p += 256) { float* u = os.upsample(x.data() + p, 256, 0); up.insert(up.end(), u, u + 256 * (1 << idx)); }
        size_t N = 0;
        const double fsu = fs * (1 << idx);
        auto mag = analysis::spectrum(up.data() + up.size() / 2, up.size() / 2, &N);
        double sig = 0, img = 0;
        for (size_t k = 0; k < mag.size(); ++k) {
            const double f = k * fsu / (double)N;
            if (f < fs / 2) sig = std::max(sig, mag[k]);
            else img = std::max(img, mag[k]);
        }
        const double rej = db(sig / img);
        record("oversampler_image_rejection_" + std::to_string(1 << idx) + "x", rej, "dB");
        CHECK_MSG(rej > 70.0, "image rejection " << rej << " dB");
    }
}

TEST(unit, delayline_hermite_reads_integer_exact) {
    DelayLine d; d.prepare(100);
    for (int i = 0; i < 50; ++i) { d.push((float)i, 0); d.advance(); }
    CHECK_NEAR(d.read(5.0f, 0), 45.0, 1e-5);   // after 50 pushes, delay 5 -> value 45
    CHECK_NEAR(d.read(5.5f, 0), 44.5, 1e-4);   // linear ramp interpolates exactly
}

TEST(unit, wav_roundtrip_float_and_24bit) {
    WavData w; w.sampleRate = 44100;
    w.channels = {makeSine(44100, 440, 0.7, 1000), makeNoise(1000, 0.5)};
    const std::string p32 = "/tmp/mfx_test32.wav", p24 = "/tmp/mfx_test24.wav";
    CHECK(writeWav(p32, w, 32));
    CHECK(writeWav(p24, w, 24));
    WavData r32, r24;
    CHECK(readWav(p32, r32));
    CHECK(readWav(p24, r24));
    CHECK(r32.numChannels() == 2 && r32.numFrames() == 1000 && r32.sampleRate == 44100);
    double e32 = 0, e24 = 0;
    for (size_t i = 0; i < 1000; ++i) {
        e32 = std::max(e32, (double)std::fabs(r32.channels[1][i] - w.channels[1][i]));
        e24 = std::max(e24, (double)std::fabs(r24.channels[0][i] - w.channels[0][i]));
    }
    CHECK(e32 == 0.0);
    CHECK(e24 < 2.0 / 8388607.0);
}

TEST(unit, resampler_preserves_sine) {
    auto x = makeSine(44100, 1000, 0.5, 44100);
    std::vector<float> xv(x.begin(), x.end());
    auto y = resampleOffline(xv, 44100, 48000);
    CHECK(std::abs((long)y.size() - 48000) <= 1);
    auto ref = makeSine(48000, 1000, 0.5, y.size());
    const double e = analysis::esr(ref.data() + 2000, y.data() + 2000, y.size() - 4000);
    record("resample_44k1_to_48k_esr", e);
    CHECK_MSG(e < 1e-4, "esr " << e);
}

TEST(unit, tuner_yin_accuracy) {
    for (double f : {82.41, 110.0, 196.0, 329.63}) {
        auto x = makeSine(24000.0, f, 0.4, 4096);
        auto r = detectPitchYin(x.data(), 4096, 24000.0);
        CHECK(r.valid);
        const double cents = 1200.0 * std::log2(r.frequency / f);
        CHECK_MSG(std::fabs(cents) < 1.0, "f=" << f << " cents err " << cents);
    }
    auto p = makePluck(24000.0, 110.0, 0.4);
    auto r = detectPitchYin(p.data() + 2000, 4096, 24000.0);
    CHECK(r.valid && std::string(r.noteName()) == "A");
}

TEST(unit, tap_tempo_median) {
    TapTempo t;
    double bpm = 0;
    for (int i = 0; i < 5; ++i) bpm = t.tap(1.0 + 0.5 * i + (i == 2 ? 0.02 : 0.0));
    CHECK_NEAR(bpm, 120.0, 3.0);
    t.tap(10.0); // long gap restarts
    CHECK_NEAR(t.tap(10.25), 240.0, 0.5);
}
