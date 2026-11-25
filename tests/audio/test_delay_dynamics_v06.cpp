// v0.6: Premium Tape Echo, BBD Analog Delay, FET Compressor, stereo additions to "delay".
// Measurements are recorded with record(); listening evaluation is NOT part of these tests.
#include <cmath>
#include <complex>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/delay/BbdDelay.h"
#include "dsp/delay/Delay.h"
#include "dsp/delay/TapeEcho.h"
#include "dsp/dynamics/FetCompressor.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"

using namespace mfx;
using namespace mfxtest;

namespace {
struct St { std::vector<float> l, r; };
St runSt(Effect& fx, const std::vector<float>& x, int block = 256, const std::vector<float>* xr = nullptr) {
    St s{x, xr ? *xr : x};
    runEffect(fx, s.l, &s.r, block);
    return s;
}
size_t argmaxAbs(const std::vector<float>& y, size_t a, size_t b) {
    size_t k = a; float m = 0;
    for (size_t i = a; i < b && i < y.size(); ++i) if (std::fabs(y[i]) > m) { m = std::fabs(y[i]); k = i; }
    return k;
}
double rms(const std::vector<float>& y, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)y[i] * y[i];
    return std::sqrt(s / (double)(b - a));
}
// single-bin DFT magnitude (Hann), amplitude units
double tone(const std::vector<float>& y, size_t a, size_t n, double f, double fs) {
    std::complex<double> acc = 0; double w = 0;
    for (size_t i = 0; i < n; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        acc += h * (double)y[a + i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / fs));
        w += h;
    }
    return 2.0 * std::abs(acc) / w;
}
// instantaneous frequency from zero crossings -> deviation in cents, and its spectrum peak
std::vector<double> crossingCents(const std::vector<float>& y, size_t a, size_t b, double fs, double f0, std::vector<double>* times) {
    std::vector<double> t;
    for (size_t i = a + 1; i < b; ++i)
        if (y[i - 1] < 0 && y[i] >= 0) t.push_back((double)(i - 1) + y[i - 1] / (y[i - 1] - y[i]));
    std::vector<double> c;
    for (size_t k = 1; k < t.size(); ++k) {
        c.push_back(1200.0 * std::log2(fs / (t[k] - t[k - 1]) / f0));
        if (times) times->push_back(0.5 * (t[k] + t[k - 1]) / fs);
    }
    return c;
}
double modulationPeakHz(const std::vector<double>& c, double fsc, double fmax) {
    double best = 0, bf = 0, m = 0;
    for (double v : c) m += v;
    m /= (double)c.size();
    for (double f = 0.2; f < fmax; f += 0.1) {
        std::complex<double> acc = 0;
        for (size_t i = 0; i < c.size(); ++i) acc += (c[i] - m) * std::exp(std::complex<double>(0, -2 * kPi * f * i / fsc));
        if (std::abs(acc) > best) { best = std::abs(acc); bf = f; }
    }
    return bf;
}
double peakDiff(const std::vector<float>& y, size_t a, size_t b) {
    double m = 0; for (size_t i = a + 1; i < b; ++i) m = std::max(m, (double)std::fabs(y[i] - y[i - 1]));
    return m;
}
} // namespace

// ------------------------------------------------------------------------------------------ Tape Echo
TEST(audio, tape_echo_delay_time_accuracy_and_heads) {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (float ms : {80.f, 350.f, 1000.f}) {
            TapeEcho t;
            t.setParam("time_ms", ms); t.setParam("mix", 50.f); t.setParam("feedback", 0.f);
            t.setParam("wow", 0.f); t.setParam("flutter", 0.f); t.setParam("modulation", 0.f); t.setParam("drive", 0.f);
            t.setParam("heads", 2.f); t.setParam("spread", 0.f);
            t.prepare({fs, 256, 2});
            const size_t d = (size_t)(ms * 0.001 * fs);
            std::vector<float> x(d * 3 + (size_t)(0.2 * fs), 0.0f);
            x[0] = 0.5f;
            auto s = runSt(t, x);
            for (int k = 1; k <= 3; ++k) {
                const size_t p = argmaxAbs(s.l, k * d - d / 4, k * d + d / 4);
                const double err = ((double)p - (double)(k * d)) / fs * 1000.0;
                if (fs == 48000.0 && ms == 350.f) record("tape_head" + std::to_string(k) + "_error_ms_350", err, "ms");
                CHECK_MSG(std::fabs(err) < 0.5, "fs " << fs << " ms " << ms << " head " << k << " err " << err);
            }
        }
    }
}

TEST(audio, tape_echo_repeats_darken_and_feedback_stays_bounded) {
    TapeEcho t;
    t.setParam("time_ms", 200.f); t.setParam("mix", 100.f); t.setParam("feedback", 70.f);
    t.setParam("wow", 0.f); t.setParam("flutter", 0.f); t.setParam("modulation", 0.f); t.setParam("spread", 0.f);
    t.prepare({48000, 256, 2});
    std::vector<float> x(48000 * 2, 0.0f);
    auto nz = makeNoise(480, 0.3, 11);
    std::copy(nz.begin(), nz.end(), x.begin());
    auto s = runSt(t, x);
    // high/low energy ratio of echo k (window around k * 200 ms)
    auto ratio = [&](int k) {
        std::vector<float> w(s.l.begin() + k * 9600 - 1200, s.l.begin() + k * 9600 + 3600);
        double lo = 0, hi = 0;
        for (double f : {300.0, 500.0, 700.0}) lo += tone(w, 0, w.size(), f, 48000);
        for (double f : {4000.0, 5000.0, 6000.0}) hi += tone(w, 0, w.size(), f, 48000);
        return db(hi / lo);
    };
    const double r1 = ratio(1), r3 = ratio(3), r5 = ratio(5);
    record("tape_repeat_hf_lf_db_1", r1, "dB"); record("tape_repeat_hf_lf_db_3", r3, "dB"); record("tape_repeat_hf_lf_db_5", r5, "dB");
    CHECK_MSG(r3 < r1 - 3.0 && r5 < r3, "repeat darkening " << r1 << " " << r3 << " " << r5);
    // self-oscillation at 110 % with drive: bounded, finite, no runaway over 20 s
    TapeEcho q;
    q.setParam("feedback", 110.f); q.setParam("drive", 100.f); q.setParam("mix", 100.f); q.setParam("time_ms", 40.f);
    q.setParam("wow", 100.f); q.setParam("flutter", 100.f); q.setParam("modulation", 100.f); q.setParam("heads", 2.f);
    q.prepare({48000, 256, 2});
    std::vector<float> y(48000 * 20, 0.0f);
    auto b = makeNoise(4800, 0.9, 3);
    std::copy(b.begin(), b.end(), y.begin());
    auto o = runSt(q, y);
    const double pk = std::max(analysis::peak(o.l.data(), o.l.size()), analysis::peak(o.r.data(), o.r.size()));
    record("tape_selfosc_peak", pk, "");
    CHECK(allFinite(o.l) && allFinite(o.r) && pk < 4.0);
    CHECK(rms(o.l, 48000 * 19, 48000 * 20) > 1e-3); // it does self-oscillate (sustains)
    for (double fs : {48000.0, 192000.0}) {
        TapeEcho m;
        m.prepare({fs, 256, 2});
        record("tape_memory_kib_" + std::to_string((int)(fs / 1000)) + "k", (double)m.memoryBytes() / 1024.0, "KiB");
        CHECK(m.memoryBytes() < 6u * 1024u * 1024u);
    }
}

TEST(audio, tape_echo_wow_and_flutter_are_separate_bands) {
    auto measure = [](float wow, float flut, double* devCents) {
        TapeEcho t;
        t.setParam("time_ms", 300.f); t.setParam("mix", 100.f); t.setParam("feedback", 0.f); t.setParam("drive", 0.f);
        t.setParam("wow", wow); t.setParam("flutter", flut); t.setParam("modulation", 0.f); t.setParam("age", 0.f); t.setParam("spread", 0.f);
        t.prepare({48000, 256, 2});
        auto x = makeSine(48000, 1000.0, 0.2, 48000 * 12);
        auto s = runSt(t, x);
        auto c = crossingCents(s.l, 48000 * 2, 48000 * 12, 48000, 1000.0, nullptr);
        double m = 0, v = 0;
        for (double e : c) m += e;
        m /= (double)c.size();
        for (double e : c) v += (e - m) * (e - m);
        *devCents = std::sqrt(v / (double)c.size());
        return modulationPeakHz(c, 1000.0, 20.0);
    };
    double dw = 0, df = 0, d0 = 0;
    const double fw = measure(100.f, 0.f, &dw), ff = measure(0.f, 100.f, &df);
    measure(0.f, 0.f, &d0);
    record("tape_wow_peak_hz", fw, "Hz"); record("tape_wow_rms_cents", dw, "cents");
    record("tape_flutter_peak_hz", ff, "Hz"); record("tape_flutter_rms_cents", df, "cents");
    record("tape_nomod_rms_cents", d0, "cents");
    CHECK_MSG(fw < 2.0, "wow peak " << fw);
    CHECK_MSG(ff > 5.0 && ff < 15.0, "flutter peak " << ff);
    CHECK(dw > 1.0 && df > 0.3 && d0 < 0.05);
}

TEST(audio, tape_echo_time_change_glides_without_clicks) {
    TapeEcho t;
    t.setParam("time_ms", 300.f); t.setParam("mix", 100.f); t.setParam("feedback", 30.f);
    t.setParam("wow", 0.f); t.setParam("flutter", 0.f); t.setParam("modulation", 0.f);
    t.prepare({48000, 256, 2});
    auto x = makeSine(48000, 440.0, 0.3, 48000 * 4);
    std::vector<float> l = x, r = x;
    for (size_t pos = 0; pos < l.size(); pos += 256) {
        if (pos == 48000 * 2) t.setParam("time_ms", 500.f);
        AudioBlock b; b.numChannels = 2; b.numSamples = 256; b.ch[0] = l.data() + pos; b.ch[1] = r.data() + pos;
        t.process(b);
    }
    const double before = peakDiff(l, 48000, 48000 * 2), during = peakDiff(l, 48000 * 2, 48000 * 3);
    record("tape_time_change_maxstep_ratio", during / before, "");
    CHECK_MSG(during < 1.6 * before, "step during " << during << " before " << before);
    CHECK(std::fabs(t.headDelaySamples(0) - 0.5 * 48000) < 2.0); // settled to the new time
}

TEST(audio, tape_echo_frequency_dependent_saturation) {
    // pre-emphasis before the saturator: high frequencies compress more than lows at the same level
    auto gainAt = [](double f, double amp) {
        TapeEcho t;
        t.setParam("time_ms", 100.f); t.setParam("mix", 100.f); t.setParam("feedback", 0.f); t.setParam("drive", 70.f);
        t.setParam("wow", 0.f); t.setParam("flutter", 0.f); t.setParam("modulation", 0.f); t.setParam("tone", 50.f); t.setParam("spread", 0.f);
        t.prepare({48000, 256, 2});
        auto x = makeSine(48000, f, amp, 48000);
        auto s = runSt(t, x);
        return tone(s.l, 24000, 8192, f, 48000) / amp;
    };
    const double compLow = db(gainAt(200.0, 0.3) / gainAt(200.0, 0.01));
    const double compHigh = db(gainAt(4000.0, 0.3) / gainAt(4000.0, 0.01));
    record("tape_sat_compression_200hz", compLow, "dB"); record("tape_sat_compression_4khz", compHigh, "dB");
    CHECK_MSG(compHigh < compLow - 1.0, "low " << compLow << " high " << compHigh);
}

// ------------------------------------------------------------------------------------------ BBD
TEST(audio, bbd_delay_time_accuracy_bandwidth_and_aliasing) {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (float ms : {20.f, 300.f, 800.f}) {
            BbdDelay d;
            d.setParam("time_ms", ms); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f);
            d.setParam("compander", 0.f); d.setParam("spread", 0.f);
            d.prepare({fs, 256, 2});
            const size_t n = (size_t)(ms * 0.001 * fs);
            std::vector<float> x(n + (size_t)(0.1 * fs), 0.0f);
            x[0] = 0.5f;
            auto s = runSt(d, x);
            const size_t p = argmaxAbs(s.l, n / 2, s.l.size());
            const double err = ((double)p - (double)n) / fs * 1000.0;
            CHECK_MSG(err > -0.1 && err < 1.0, "fs " << fs << " ms " << ms << " err " << err); // + filter group delay
            if (fs == 48000.0) record("bbd_time_error_ms_" + std::to_string((int)ms), err, "ms");
        }
    }
    // aliasing grows with delay time (clock falls): 3 kHz tone, energy outside the tone
    auto alias = [](float ms) {
        BbdDelay d;
        d.setParam("time_ms", ms); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f);
        d.setParam("compander", 0.f); d.setParam("tone", 100.f); d.setParam("drive", 0.f);
        d.prepare({48000, 256, 2});
        auto x = makeSine(48000, 3000.0, 0.1, 48000 * 2);
        auto s = runSt(d, x);
        const double sig = tone(s.l, 48000, 16384, 3000.0, 48000);
        const double clk = 48000.0 / (ms * 0.001 * 48000 / BbdDelay::kBuckets);
        const double img = tone(s.l, 48000, 16384, std::fabs(clk - 3000.0), 48000);
        return db(img / sig);
    };
    const double a100 = alias(100.f), a800 = alias(800.f);
    record("bbd_alias_image_db_100ms", a100, "dB"); record("bbd_alias_image_db_800ms", a800, "dB");
    CHECK_MSG(a800 > a100 + 20.0, "alias 100 " << a100 << " 800 " << a800);
}

TEST(audio, bbd_compander_is_transparent_static_and_breathes_on_transients) {
    auto level = [](double amp, float comp) {
        BbdDelay d;
        d.setParam("time_ms", 100.f); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f);
        d.setParam("compander", comp); d.setParam("drive", 0.f);
        d.prepare({48000, 256, 2});
        auto x = makeSine(48000, 500.0, amp, 48000);
        auto s = runSt(d, x);
        return tone(s.l, 24000, 8192, 500.0, 48000) / amp;
    };
    for (double a : {0.01, 0.1, 0.4}) {
        const double e = db(level(a, 100.f) / level(a, 0.f));
        record("bbd_compander_static_err_db_" + std::to_string((int)(a * 100)), e, "dB");
        CHECK_MSG(std::fabs(e) < 1.0, "amp " << a << " err " << e);
    }
    // transient: a step from -40 to -6 dBFS shows a compander overshoot that settles
    BbdDelay d;
    d.setParam("time_ms", 50.f); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f);
    d.prepare({48000, 256, 2});
    auto x = makeSine(48000, 500.0, 1.0, 48000);
    for (size_t i = 0; i < x.size(); ++i) x[i] *= i < 24000 ? 0.01f : 0.5f;
    auto s = runSt(d, x);
    const size_t st = 24000 + 2400;
    const double early = rms(s.l, st, st + 240), late = rms(s.l, st + 9600, st + 14400);
    record("bbd_compander_transient_overshoot_db", db(early / late), "dB");
    CHECK(early / late > 1.05);
}

TEST(audio, bbd_feedback_bounded_time_change_and_modulation) {
    BbdDelay d;
    d.setParam("feedback", 110.f); d.setParam("mix", 100.f); d.setParam("time_ms", 20.f); d.setParam("drive", 100.f);
    d.setParam("mod_depth", 100.f); d.setParam("mod_rate_hz", 8.f);
    d.prepare({48000, 256, 2});
    std::vector<float> y(48000 * 15, 0.0f);
    auto b = makeNoise(4800, 0.9, 5);
    std::copy(b.begin(), b.end(), y.begin());
    auto o = runSt(d, y);
    const double pk = std::max(analysis::peak(o.l.data(), o.l.size()), analysis::peak(o.r.data(), o.r.size()));
    record("bbd_selfosc_peak", pk, "");
    CHECK(allFinite(o.l) && allFinite(o.r) && pk < 4.0);
    // time change: no step larger than the steady-state sine slope allows
    BbdDelay e;
    e.setParam("time_ms", 300.f); e.setParam("mix", 100.f); e.setParam("feedback", 30.f); e.setParam("mod_depth", 0.f);
    e.prepare({48000, 256, 2});
    auto x = makeSine(48000, 440.0, 0.3, 48000 * 4);
    std::vector<float> l = x, r = x;
    for (size_t pos = 0; pos < l.size(); pos += 256) {
        if (pos == 48000 * 2) e.setParam("time_ms", 150.f);
        AudioBlock bk; bk.numChannels = 2; bk.numSamples = 256; bk.ch[0] = l.data() + pos; bk.ch[1] = r.data() + pos;
        e.process(bk);
    }
    const double before = peakDiff(l, 48000, 48000 * 2), during = peakDiff(l, 48000 * 2, 48000 * 3);
    record("bbd_time_change_maxstep_ratio", during / before, "");
    CHECK_MSG(during < 2.5 * before, "during " << during << " before " << before); // pitch rises while the clock speeds up
    // clock modulation shows up as frequency modulation at the LFO rate
    BbdDelay m;
    m.setParam("time_ms", 300.f); m.setParam("mix", 100.f); m.setParam("feedback", 0.f); m.setParam("mod_depth", 100.f);
    m.setParam("mod_rate_hz", 2.f); m.setParam("compander", 0.f);
    m.prepare({48000, 256, 2});
    auto xs = makeSine(48000, 500.0, 0.2, 48000 * 8);
    auto s = runSt(m, xs);
    auto c = crossingCents(s.l, 48000, 48000 * 8, 48000, 500.0, nullptr);
    const double fpk = modulationPeakHz(c, 500.0, 10.0);
    record("bbd_mod_peak_hz", fpk, "Hz");
    CHECK(std::fabs(fpk - 2.0) < 0.3);
}

TEST(audio, bbd_stereo_spread_decorrelates_and_memory_is_rate_independent) {
    BbdDelay d;
    d.setParam("time_ms", 300.f); d.setParam("mix", 100.f); d.setParam("feedback", 40.f); d.setParam("spread", 100.f);
    d.setParam("mod_depth", 60.f);
    d.prepare({48000, 256, 2});
    auto x = makeNoise(48000 * 3, 0.3, 9);
    auto s = runSt(d, x);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = 48000; i < s.l.size(); ++i) { sl += s.l[i] * s.l[i]; sr += s.r[i] * s.r[i]; slr += s.l[i] * s.r[i]; }
    const double corr = slr / std::sqrt(sl * sr);
    record("bbd_lr_corr_spread100", corr, "");
    CHECK(corr < 0.9);
    BbdDelay a, b;
    a.prepare({44100, 64, 2}); b.prepare({192000, 64, 2});
    CHECK(a.memoryBytes() == b.memoryBytes());
    record("bbd_memory_kib", (double)a.memoryBytes() / 1024.0, "KiB");
}

// ------------------------------------------------------------------------------------------ FET compressor
TEST(audio, fet_comp_static_curve_matches_ratio_and_threshold) {
    for (int r = 0; r < 4; ++r) {
        FetCompressor c;
        c.setParam("ratio", (float)r); c.setParam("attack_us", 800.f); c.setParam("release_ms", 1100.f);
        c.setParam("character", 0.f);
        c.prepare({48000, 256, 2});
        auto outDb = [&](double inDb) {
            c.reset();
            auto x = makeSine(48000, 1000.0, std::pow(10.0, inDb / 20.0), 48000);
            auto y = processMono(c, x);
            return db(tone(y, 36000, 8192, 1000.0, 48000));
        };
        const double T = c.thresholdDb(), R = c.ratio();
        const double y1 = outDb(T + 6.0), y2 = outDb(T + 18.0); // stays below the input-stage soft limit
        const double slope = 12.0 / (y2 - y1);
        const double below = outDb(T - 20.0) - (T - 20.0);
        record("fet_static_ratio_" + std::to_string((int)R), slope, "");
        CHECK_MSG(std::fabs(slope - R) < 0.2 * R, "ratio " << R << " measured " << slope);
        CHECK_MSG(std::fabs(below) < 0.3, "below threshold gain " << below);
        // threshold accuracy: output 6 dB over threshold must sit at T + 6/R
        record("fet_static_out_at_T6_err_db_" + std::to_string((int)R), y1 - (T + 6.0 / R), "dB");
        CHECK_MSG(std::fabs(y1 - (T + 6.0 / R)) < 1.0, "output at T+6 " << y1 << " expected " << T + 6.0 / R);
    }
}

TEST(audio, fet_comp_attack_release_and_program_dependence) {
    auto attack63 = [](float us) {
        FetCompressor c;
        c.setParam("attack_us", us); c.setParam("release_ms", 500.f); c.setParam("ratio", 3.f); c.setParam("character", 0.f);
        c.prepare({96000, 256, 2});
        std::vector<float> x(9600, 0.0f);
        for (size_t i = 960; i < x.size(); ++i) x[i] = 0.5f; // DC step well over threshold (rectifier sees a constant)
        processMono(c, x);
        // re-run sample by sample to read the gain reduction trajectory
        FetCompressor d;
        d.setParam("attack_us", us); d.setParam("release_ms", 500.f); d.setParam("ratio", 3.f); d.setParam("character", 0.f);
        d.prepare({96000, 1, 2});
        std::vector<float> one(1);
        double grEnd = 0; std::vector<double> gr;
        for (size_t i = 0; i < x.size(); ++i) { one[0] = x[i]; processMono(d, one, 1); gr.push_back(d.gainReductionDb()); }
        grEnd = gr.back();
        for (size_t i = 960; i < gr.size(); ++i) if (gr[i] > 0.63 * grEnd) return (double)(i - 960) / 96.0; // ms
        return 1e9;
    };
    const double a20 = attack63(20.f), a800 = attack63(800.f);
    record("fet_attack63_ms_20us", a20, "ms"); record("fet_attack63_ms_800us", a800, "ms");
    CHECK(a20 < 0.1 && a800 > a20 * 4.0);
    // program-dependent release: recovery to 1 dB after a short burst vs a long burst
    auto recovery = [](double burstSec) {
        FetCompressor c;
        c.setParam("release_ms", 300.f); c.setParam("ratio", 1.f); c.setParam("character", 0.f);
        c.prepare({48000, 64, 2});
        const size_t nb = (size_t)(burstSec * 48000);
        auto x = makeSine(48000, 200.0, 0.5, nb + 48000 * 4);
        for (size_t i = nb; i < x.size(); ++i) x[i] *= 0.001f;
        std::vector<float> blk(64);
        for (size_t pos = 0; pos < x.size(); pos += 64) {
            std::copy(x.begin() + pos, x.begin() + pos + 64, blk.begin());
            processMono(c, blk, 64);
            if (pos > nb && c.gainReductionDb() < 1.0f) return (double)(pos - nb) / 48000.0;
        }
        return 1e9;
    };
    const double rShort = recovery(0.05), rLong = recovery(2.0);
    record("fet_release_to_1db_after_50ms_s", rShort, "s"); record("fet_release_to_1db_after_2s_s", rLong, "s");
    CHECK_MSG(rLong > 1.5 * rShort, "short " << rShort << " long " << rLong);
}

TEST(audio, fet_comp_distortion_character_and_mix) {
    auto thd = [](double f, float att, float rel, float ch) {
        FetCompressor c;
        c.setParam("attack_us", att); c.setParam("release_ms", rel); c.setParam("ratio", 3.f); c.setParam("character", ch);
        c.setParam("input_db", 12.f);
        c.prepare({48000, 256, 2});
        auto x = makeSine(48000, f, 0.25, 48000 * 2);
        auto y = processMono(c, x);
        const double h1 = tone(y, 48000, 32768, f, 48000);
        double hs = 0;
        for (int k = 2; k <= 6; ++k) { const double h = tone(y, 48000, 32768, f * k, 48000); hs += h * h; }
        return 100.0 * std::sqrt(hs) / h1;
    };
    const double fastLow = thd(100.0, 20.f, 50.f, 0.f), slowLow = thd(100.0, 800.f, 1100.f, 0.f);
    const double chLow = thd(1000.0, 800.f, 1100.f, 0.f), chHigh = thd(1000.0, 800.f, 1100.f, 100.f);
    record("fet_thd_100hz_fast_pct", fastLow, "%"); record("fet_thd_100hz_slow_pct", slowLow, "%");
    record("fet_thd_1k_char0_pct", chLow, "%"); record("fet_thd_1k_char100_pct", chHigh, "%");
    CHECK_MSG(fastLow > 2.0 * slowLow, "fast " << fastLow << " slow " << slowLow);
    CHECK_MSG(chHigh > chLow * 1.25, "char0 " << chLow << " char100 " << chHigh);
    // SMPTE IMD (60 Hz + 7 kHz, 4:1)
    {
        FetCompressor c;
        c.setParam("attack_us", 20.f); c.setParam("release_ms", 50.f); c.setParam("ratio", 3.f); c.setParam("input_db", 12.f);
        c.prepare({48000, 256, 2});
        auto a = makeSine(48000, 60.0, 0.2, 96000), b = makeSine(48000, 7000.0, 0.05, 96000);
        for (size_t i = 0; i < a.size(); ++i) a[i] += b[i];
        auto y = processMono(c, a);
        const double c0 = tone(y, 48000, 32768, 7000.0, 48000);
        const double sb = std::hypot(tone(y, 48000, 32768, 6940.0, 48000), tone(y, 48000, 32768, 7060.0, 48000));
        record("fet_imd_smpte_fast_pct", 100.0 * sb / c0, "%");
        CHECK(std::isfinite(sb));
    }
    // Mix 0 = dry (exact)
    FetCompressor c;
    c.setParam("mix", 0.f); c.setParam("input_db", 30.f);
    c.prepare({48000, 256, 2});
    auto x = makeNoise(4800, 0.5, 2);
    auto y = processMono(c, x);
    double e = 0; for (size_t i = 0; i < x.size(); ++i) e = std::max(e, (double)std::fabs(y[i] - x[i]));
    CHECK(e < 1e-6);
}

TEST(audio, fet_comp_extreme_settings_bounded_at_all_rates) {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        FetCompressor c;
        c.setParam("input_db", 40.f); c.setParam("output_db", 20.f); c.setParam("attack_us", 20.f); c.setParam("release_ms", 50.f);
        c.setParam("ratio", 4.f); c.setParam("character", 100.f); c.setParam("sc_hpf_hz", 500.f);
        c.prepare({fs, 128, 2});
        auto x = makeNoise((size_t)fs, 4.0, 3);
        x[100] = std::numeric_limits<float>::quiet_NaN();
        x[200] = std::numeric_limits<float>::infinity();
        std::vector<float> r = x;
        runEffect(c, x, &r, 128);
        CHECK_MSG(allFinite(x) && analysis::peak(x.data(), x.size()) < 16.0, "fs " << fs);
    }
}

// ------------------------------------------------------------------------------------------ stereo delay (v0.6)
TEST(audio, stereo_delay_v06_cross_feedback_width_and_free_right_time) {
    auto run = [](std::initializer_list<std::pair<const char*, float>> ps, bool leftOnly) {
        Delay d;
        d.setParam("mode", 0.f); d.setParam("routing", 1.f); d.setParam("mix", 100.f); d.setParam("mod_depth", 0.f);
        d.setParam("time_ms", 100.f); d.setParam("offset", 0.f); d.setParam("feedback", 60.f);
        d.setParam("low_cut_hz", 20.f); d.setParam("high_cut_hz", 20000.f);
        for (auto& p : ps) d.setParam(p.first, p.second);
        d.prepare({48000, 256, 2});
        std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
        l[0] = 0.5f;
        if (!leftOnly) r[0] = 0.5f;
        runEffect(d, l, &r, 256);
        return St{l, r};
    };
    // without cross feedback a left impulse never reaches the right channel
    auto a = run({}, true);
    CHECK(rms(a.r, 0, 48000) < 1e-9);
    auto b = run({{"cross_fb", 100.f}}, true);
    const size_t p = argmaxAbs(b.r, 4800 + 2400, 48000);
    record("stereo_delay_cross_first_right_echo_ms", (double)p / 48.0, "ms");
    CHECK(std::abs((long)p - 9600) < 4); // second repeat crosses to the right
    // width 0 = identical L/R wet
    auto c = run({{"width", 0.f}, {"time_r_mode", 1.f}, {"time_r_ms", 150.f}}, false);
    double e = 0; for (size_t i = 0; i < 48000; ++i) e = std::max(e, (double)std::fabs(c.l[i] - c.r[i]));
    CHECK(e < 1e-6);
    // free right time
    auto f = run({{"time_r_mode", 1.f}, {"time_r_ms", 170.f}, {"feedback", 0.f}}, false);
    CHECK(std::abs((long)argmaxAbs(f.r, 100, 48000) - 8160) < 3);
    CHECK(std::abs((long)argmaxAbs(f.l, 100, 48000) - 4800) < 3);
    // L/R feedback offset: right decays slower
    auto g = run({{"fb_lr_offset", 40.f}}, false);
    CHECK(rms(g.r, 24000, 48000) > 3.0 * rms(g.l, 24000, 48000));
}

TEST(audio, v06_new_blocks_keep_trails_on_bypass) {
    for (const char* id : {"delay_tape_premium", "delay_bbd_premium"}) {
        Engine e;
        e.prepare(48000, 256);
        auto fx = createEffect(id);
        fx->setParam("mix", 50.f); fx->setParam("feedback", 50.f);
        e.addEffect(std::move(fx));
        const int slot = e.allSlotsInOrder()[0];
        std::vector<float> l(256), r(256);
        auto burst = makeNoise(256, 0.5, 4);
        static const std::vector<float> zero(256, 0.0f);
        double tail = 0;
        for (int b = 0; b < 300; ++b) {
            const float* in[1] = {b < 10 ? burst.data() : zero.data()};
            float* out[2] = {l.data(), r.data()};
            if (b == 12) e.setBypass(slot, true);
            e.process(in, 1, out, 2, 256);
            if (b > 40) for (float v : l) tail += (double)v * v;
        }
        CHECK_MSG(tail > 1e-6, id);
    }
}
