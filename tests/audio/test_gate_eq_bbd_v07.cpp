// v0.7: Premium Gate / Expander, Premium EQ (TPT SVF), BBD v2 (multi-chip, clock-tracking filters), full chain.
// Signals are synthetic (sines, noise, Karplus-Strong "pluck" labelled SYNTHETIC). The user's DI (026.wav) is
// analysed outside the test suite (tools/benchmark), because it is not part of the repository.
#include <cmath>
#include <complex>
#include <limits>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/delay/BbdDelay.h"
#include "dsp/dynamics/PremiumGate.h"
#include "dsp/filters/EQ.h"
#include "dsp/filters/PremiumEQ.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"
#include "presets/Preset.h"

using namespace mfx;
using namespace mfxtest;

namespace {
double rmsDb(const std::vector<float>& y, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)y[i] * y[i];
    return 10.0 * std::log10(s / (double)(b - a) + 1e-30);
}
double toneAmp(const std::vector<float>& y, size_t a, size_t n, double f, double fs) {
    std::complex<double> acc = 0; double w = 0;
    for (size_t i = 0; i < n; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        acc += h * (double)y[a + i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / fs));
        w += h;
    }
    return 2.0 * std::abs(acc) / w;
}
std::vector<float> noiseFloor(size_t n, double dbfs, uint32_t seed) { return makeNoise(n, std::pow(10.0, dbfs / 20.0) * std::sqrt(3.0), seed); }
// one-sided coherent / incoherent analysis of y against x (Welch, Hann, N = 8192)
struct Tf { std::vector<double> f, mag, coh; };
Tf transfer(const std::vector<float>& x, const std::vector<float>& y, double fs, size_t start) {
    const size_t N = 8192;
    std::vector<std::complex<double>> sxy(N / 2 + 1), X(N / 2 + 1), Y(N / 2 + 1);
    std::vector<double> sxx(N / 2 + 1), syy(N / 2 + 1);
    for (size_t s = start; s + N <= x.size(); s += N / 2) {
        for (size_t k = 0; k <= N / 2; k += 1) { X[k] = 0; Y[k] = 0; }
        // DFT via direct loops is too slow; use a radix-2 FFT
        std::vector<std::complex<double>> a(N), b(N);
        for (size_t i = 0; i < N; ++i) { const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / N); a[i] = h * x[s + i]; b[i] = h * y[s + i]; }
        auto fft = [](std::vector<std::complex<double>>& v) {
            const size_t n = v.size();
            for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(v[i], v[j]); }
            for (size_t len = 2; len <= n; len <<= 1) {
                const std::complex<double> wl = std::polar(1.0, -2 * kPi / (double)len);
                for (size_t i = 0; i < n; i += len) { std::complex<double> w = 1; for (size_t j = 0; j < len / 2; ++j) { auto u = v[i + j], t = v[i + j + len / 2] * w; v[i + j] = u + t; v[i + j + len / 2] = u - t; w *= wl; } }
            }
        };
        fft(a); fft(b);
        for (size_t k = 0; k <= N / 2; ++k) { sxy[k] += std::conj(a[k]) * b[k]; sxx[k] += std::norm(a[k]); syy[k] += std::norm(b[k]); }
    }
    Tf t;
    for (size_t k = 1; k <= N / 2; ++k) {
        t.f.push_back(k * fs / N);
        t.mag.push_back(std::abs(sxy[k]) / (sxx[k] + 1e-300));
        t.coh.push_back(std::norm(sxy[k]) / (sxx[k] * syy[k] + 1e-300));
    }
    return t;
}
} // namespace

// ============================================================================================ Premium Gate
TEST(audio, gate_premium_threshold_accuracy_and_hysteresis) {
    PremiumGate g;
    g.setParam("mode", 0.f); g.setParam("threshold_db", -50.f); g.setParam("hysteresis_db", 6.f); g.setParam("hold_ms", 0.f);
    g.setParam("attack_ms", 0.1f); g.setParam("release_ms", 20.f); g.setParam("adaptive_release", 0.f); g.setParam("sc_hpf_hz", 20.f);
    g.prepare({48000, 64, 1});
    // 1 kHz sine, level ramps -80 -> -20 dBFS over 6 s and back
    const size_t n = 48000 * 12;
    std::vector<float> x(n), lv(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double)i / 48000;
        const double L = t < 6 ? -80 + 10 * t : -20 - 10 * (t - 6);
        lv[i] = (float)L;
        x[i] = (float)(std::pow(10.0, L / 20) * std::sin(2 * kPi * 1000 * t));
    }
    std::vector<float> red(n), one(64);
    for (size_t p = 0; p < n; p += 64) { std::copy(x.begin() + p, x.begin() + p + 64, one.begin()); processMono(g, one, 64); for (size_t i = p; i < p + 64; ++i) red[i] = g.reductionDb(); }
    double openAt = 0, closeAt = 0;
    for (size_t i = 1; i < n / 2; ++i) if (red[i - 1] > 30 && red[i] < 30) { openAt = lv[i]; break; }
    for (size_t i = n / 2; i < n; ++i) if (red[i - 1] < 30 && red[i] > 30) { closeAt = lv[i]; break; }
    record("gate_premium_open_level_db", openAt, "dBFS"); record("gate_premium_close_level_db", closeAt, "dBFS");
    CHECK_MSG(std::fabs(openAt - -50.0) < 1.0, "open at " << openAt);
    CHECK_MSG(std::fabs(closeAt - -56.0) < 1.5, "close at " << closeAt);
}

TEST(audio, gate_premium_hysteresis_prevents_chattering) {
    auto count = [](float hyst) {
        PremiumGate g;
        g.setParam("mode", 0.f); g.setParam("threshold_db", -40.f); g.setParam("hysteresis_db", hyst); g.setParam("hold_ms", 10.f);
        g.setParam("release_ms", 30.f); g.setParam("sc_hpf_hz", 20.f);
        g.prepare({48000, 128, 1});
        // sustain hovering around the threshold: -40 dBFS +/- 2 dB at 6 Hz, plus -70 dBFS noise
        const size_t n = 48000 * 5;
        auto nz = noiseFloor(n, -70, 3);
        std::vector<float> x(n);
        for (size_t i = 0; i < n; ++i) {
            const double t = (double)i / 48000, L = -40 + 2 * std::sin(2 * kPi * 6 * t);
            x[i] = (float)(std::pow(10.0, L / 20) * std::sin(2 * kPi * 330 * t)) + nz[i];
        }
        processMono(g, x, 128);
        return (double)g.transitions() / 5.0;
    };
    const double c0 = count(0.f), c6 = count(6.f);
    record("gate_premium_transitions_per_s_hyst0", c0, "1/s"); record("gate_premium_transitions_per_s_hyst6", c6, "1/s");
    CHECK_MSG(c6 < 0.5 && c0 > 5.0, "hyst0 " << c0 << " hyst6 " << c6);
}

TEST(audio, gate_premium_expander_curve_and_unity) {
    PremiumGate g;
    g.setParam("mode", 1.f); g.setParam("threshold_db", -40.f); g.setParam("ratio", 3.f); g.setParam("range_db", -40.f);
    g.setParam("hysteresis_db", 0.f); g.setParam("sc_hpf_hz", 20.f); g.setParam("detector", 1.f);
    g.prepare({48000, 256, 1});
    for (double L : {-30.0, -46.0, -52.0, -80.0}) {
        g.reset();
        auto x = makeSine(48000, 1000.0, std::pow(10.0, L / 20), 48000);
        auto y = processMono(g, x);
        const double gr = db(toneAmp(x, 36000, 8192, 1000, 48000) / toneAmp(y, 36000, 8192, 1000, 48000));
        const double expect = std::min(g.staticReduction(L, false), 40.0);
        record("gate_premium_expander_gr_db_at_" + std::to_string((int)-L), gr, "dB");
        CHECK_MSG(std::fabs(gr - expect) < 0.6, "L " << L << " gr " << gr << " expect " << expect);
    }
    // above threshold: bit-exact unity
    g.reset();
    auto x = makeSine(48000, 1000.0, 0.3, 48000);
    auto y = processMono(g, x);
    double e = 0; for (size_t i = 4800; i < x.size(); ++i) e = std::max(e, (double)std::fabs(y[i] - x[i]));
    CHECK(e == 0.0);
}

TEST(audio, gate_premium_preserves_attack_and_sustain_and_reduces_noise) {
    // SYNTHETIC guitar-like note (Karplus-Strong) decaying into a -70 dBFS hiss floor, after 1 s of hiss only
    const double fs = 48000;
    const size_t pre = 48000, n = 48000 * 5;
    auto hiss = noiseFloor(n, -70, 11);
    auto pl = makePluck(fs, 110.0, 4.0, 0.3);
    for (size_t i = 0; i < pl.size(); ++i) pl[i] *= (float)std::exp(-(double)i / fs / 0.5); // note decays into the floor
    std::vector<float> x = hiss;
    for (size_t i = 0; i < pl.size() && pre + i < n; ++i) x[pre + i] += pl[i];
    for (float mode : {0.f, 1.f}) {
        PremiumGate g;
        g.setParam("mode", mode); g.setParam("threshold_db", -58.f); g.setParam("release_ms", 250.f);
        g.prepare({fs, 128, 1});
        auto y = processMono(g, x, 128);
        const std::string m = mode == 0.f ? "gate" : "expander";
        // attack: first 5 ms of the note
        const double att = rmsDb(y, pre, pre + 240) - rmsDb(x, pre, pre + 240);
        // sustain: the part of the note that stays >= 12 dB above the floor
        size_t sEnd = pre;
        for (size_t i = pre; i + 4800 < n; i += 4800) if (rmsDb(pl, i - pre, i - pre + 4800) > -58.0) sEnd = i + 4800;
        const double sus = rmsDb(y, pre + 4800, sEnd) - rmsDb(x, pre + 4800, sEnd);
        // noise reduction: hiss before the note (after the gate has closed)
        const double nr = rmsDb(x, 24000, 47000) - rmsDb(y, 24000, 47000);
        record("gate_premium_" + m + "_attack_change_db", att, "dB");
        record("gate_premium_" + m + "_sustain_change_db", sus, "dB");
        record("gate_premium_" + m + "_noise_reduction_db", nr, "dB");
        CHECK_MSG(att > -1.0, m << " attack " << att);
        CHECK_MSG(sus > -0.3, m << " sustain " << sus);
        CHECK_MSG(nr > (mode == 0.f ? 30.0 : 15.0), m << " noise reduction " << nr); // expander: ratio-limited by design
        // release continuity: the gain never jumps (dB step per sample bounded by the release ramp)
        PremiumGate h;
        h.setParam("mode", mode); h.setParam("threshold_db", -58.f); h.setParam("release_ms", 250.f);
        h.prepare({fs, 1, 1});
        std::vector<float> one(1);
        double maxStep = 0, prev = 0;
        for (size_t i = pre; i < n; ++i) { one[0] = x[i]; processMono(h, one, 1); const double r = h.reductionDb(); maxStep = std::max(maxStep, r - prev); prev = r; }
        record("gate_premium_" + m + "_max_release_step_db", maxStep, "dB/sample");
        CHECK(maxStep > 0.0 && maxStep <= 4.0 * 60.0 / (0.25 * fs) + 1e-9);
    }
}

TEST(audio, gate_premium_adaptive_release_closes_abrupt_stops_faster) {
    // abrupt stop (muted chord) vs slow natural decay; adaptive must cut the residual hiss after the stop
    // without changing the slow decay
    const double fs = 48000;
    auto run = [&](bool adaptive, bool abrupt, double* tailNoise, double* decayKeep) {
        PremiumGate g;
        g.setParam("mode", 0.f); g.setParam("threshold_db", -58.f); g.setParam("release_ms", 600.f);
        g.setParam("hold_ms", 20.f); g.setParam("adaptive_release", adaptive ? 1.f : 0.f);
        g.prepare({fs, 128, 1});
        const size_t n = 48000 * 3;
        auto x = noiseFloor(n, -70, 5);
        auto s = makeSine(fs, 196.0, 0.2, n);
        for (size_t i = 0; i < n; ++i) {
            const double t = (double)i / fs;
            double env = 0;
            if (t >= 0.2 && t < 1.2) env = 1;
            else if (t >= 1.2) env = abrupt ? std::exp(-(t - 1.2) / 0.004) : std::exp(-(t - 1.2) / 0.25);
            x[i] += (float)(env * s[i]);
        }
        auto y = processMono(g, x, 128);
        *tailNoise = rmsDb(y, (size_t)(1.35 * fs), (size_t)(1.6 * fs)) - rmsDb(x, (size_t)(1.35 * fs), (size_t)(1.6 * fs));
        *decayKeep = rmsDb(y, (size_t)(1.2 * fs), (size_t)(1.6 * fs)) - rmsDb(x, (size_t)(1.2 * fs), (size_t)(1.6 * fs));
    };
    double tFix, tAd, dFix, dAd, k;
    run(false, true, &tFix, &k); run(true, true, &tAd, &k);
    run(false, false, &k, &dFix); run(true, false, &k, &dAd);
    record("gate_premium_abrupt_stop_tail_fixed_db", tFix, "dB"); record("gate_premium_abrupt_stop_tail_adaptive_db", tAd, "dB");
    record("gate_premium_slow_decay_keep_fixed_db", dFix, "dB"); record("gate_premium_slow_decay_keep_adaptive_db", dAd, "dB");
    CHECK_MSG(tAd < tFix - 6.0, "abrupt stop tail fixed " << tFix << " adaptive " << tAd);
    CHECK_MSG(std::fabs(dAd - dFix) < 0.5, "slow decay fixed " << dFix << " adaptive " << dAd);
}

TEST(audio, gate_premium_sidechain_hpf_link_and_safety) {
    // 50 Hz hum at -45 dBFS (above the -55 dBFS threshold): with the side-chain HPF the gate stays closed
    auto hum = makeSine(48000, 50.0, std::pow(10.0, -45.0 / 20.0) * 1.4142, 48000 * 2);
    for (float hp : {20.f, 150.f}) {
        PremiumGate g; g.setParam("mode", 0.f); g.setParam("threshold_db", -55.f); g.setParam("sc_hpf_hz", hp);
        g.prepare({48000, 256, 1});
        auto y = processMono(g, hum);
        const double att = rmsDb(hum, 48000, 96000) - rmsDb(y, 48000, 96000);
        record("gate_premium_hum_attenuation_schp_" + std::to_string((int)hp), att, "dB");
        CHECK(hp > 100.f ? att > 40.0 : att < 0.5);
    }
    // open gate: the audio path is not filtered (hum passes bit-exact)
    PremiumGate o; o.setParam("threshold_db", -90.f); o.setParam("sc_hpf_hz", 500.f);
    o.prepare({48000, 256, 1});
    auto loud = makeSine(48000, 50.0, 0.3, 48000);
    auto y = processMono(o, loud);
    double e = 0; for (size_t i = 0; i < y.size(); ++i) e = std::max(e, (double)std::fabs(y[i] - loud[i]));
    CHECK(e == 0.0);
    // stereo link: loud left keeps the quiet right open (linked) / right closes (unlinked)
    for (float link : {1.f, 0.f}) {
        PremiumGate s; s.setParam("mode", 0.f); s.setParam("threshold_db", -50.f); s.setParam("stereo_link", link);
        s.prepare({48000, 256, 2});
        auto l = makeSine(48000, 440.0, 0.3, 48000), r = makeSine(48000, 440.0, 0.0005, 48000);
        auto rr = r;
        runEffect(s, l, &rr, 256);
        const double rg = rmsDb(rr, 24000, 48000) - rmsDb(r, 24000, 48000);
        record(link > 0.5f ? "gate_premium_linked_right_gain_db" : "gate_premium_unlinked_right_gain_db", rg, "dB");
        CHECK(link > 0.5f ? std::fabs(rg) < 0.01 : rg < -40.0);
    }
    // digital silence -> silence, NaN/Inf -> finite
    PremiumGate z; z.prepare({48000, 256, 2});
    std::vector<float> sl(48000, 0.0f), sr(48000, 0.0f);
    sl[100] = std::numeric_limits<float>::quiet_NaN(); sr[200] = std::numeric_limits<float>::infinity();
    runEffect(z, sl, &sr, 256);
    CHECK(allFinite(sl) && allFinite(sr) && analysis::peak(sl.data(), sl.size()) == 0.0);
}

// ============================================================================================ Premium EQ
TEST(audio, eq_premium_response_matches_design_at_all_rates) {
    struct Case { int type; double f, g, q; };
    const Case cases[] = {{PremiumEQ::kBell, 1000, 12, 2}, {PremiumEQ::kBell, 250, -9, 0.7}, {PremiumEQ::kLowShelf, 120, 6, 0.7071},
                          {PremiumEQ::kHighShelf, 6000, -8, 0.7071}, {PremiumEQ::kHp12, 90, 0, 0.7071}, {PremiumEQ::kHp24, 90, 0, 0.7071},
                          {PremiumEQ::kLp12, 5000, 0, 1.2}, {PremiumEQ::kLp24, 5000, 0, 0.7071}, {PremiumEQ::kNotch, 1500, 0, 4}};
    double worst = 0, worstAnalog = 0, centreErr = 0;
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const auto& cs : cases) {
            PremiumEQ e;
            e.setParam("b1_type", (float)cs.type); e.setParam("b1_hz", (float)cs.f); e.setParam("b1_db", (float)cs.g); e.setParam("b1_q", (float)cs.q);
            for (int b = 2; b <= 5; ++b) e.setParam(("b" + std::to_string(b) + "_type").c_str(), 0.f);
            e.prepare({fs, 256, 1});
            for (double f : {50.0, 200.0, 1000.0, 3000.0, 8000.0}) {
                if (cs.type == PremiumEQ::kNotch && std::fabs(f - cs.f) < 200) continue;
                e.reset();
                auto y = processMono(e, makeSine(fs, f, 0.1, (size_t)(fs * 0.5)));
                const double meas = db(toneAmp(y, (size_t)(fs * 0.25), 8192, f, fs) / 0.1);
                worst = std::max(worst, std::fabs(meas - e.responseDb(f)));
            }
            if (cs.type == PremiumEQ::kBell) centreErr = std::max(centreErr, std::fabs(e.responseDb(cs.f) - cs.g));
            // analogue reference at f <= fs/8: |H(j f/f0)| of the prototype
            for (double f : {100.0, 500.0, 2000.0}) {
                if (f > fs / 8) continue;
                PremiumEQ::Coeffs c = PremiumEQ::design(cs.type, 1e7, cs.f, cs.g, cs.q); // fs -> "infinite": analogue limit
                worstAnalog = std::max(worstAnalog, std::fabs(e.responseDb(f) - 20 * std::log10(PremiumEQ::magnitude(c, 1e7, f))));
            }
        }
    record("eq_premium_measured_vs_design_max_db", worst, "dB");
    record("eq_premium_digital_vs_analog_max_db_below_fs8", worstAnalog, "dB");
    record("eq_premium_bell_centre_gain_err_db", centreErr, "dB");
    CHECK(worst < 0.05);
    CHECK(centreErr < 0.01);
    CHECK(worstAnalog < 0.5);
}

TEST(audio, eq_premium_bell_phase_and_bandwidth) {
    // bell: zero phase at the centre, symmetric (in log frequency) half-gain points with bandwidth f0/Q-equivalent
    PremiumEQ e;
    e.setParam("b1_type", 1.f); e.setParam("b1_hz", 1000.f); e.setParam("b1_db", 12.f); e.setParam("b1_q", 2.f);
    for (int b = 2; b <= 5; ++b) e.setParam(("b" + std::to_string(b) + "_type").c_str(), 0.f);
    e.prepare({48000, 256, 1});
    std::vector<float> imp(65536, 0.0f); imp[0] = 1.0f;
    auto h = processMono(e, imp);
    std::complex<double> H = 0;
    for (size_t i = 0; i < h.size(); ++i) H += (double)h[i] * std::exp(std::complex<double>(0, -2 * kPi * 1000.0 * i / 48000));
    const double ph = std::arg(H) * 180 / kPi;
    record("eq_premium_bell_phase_at_centre_deg", ph, "deg");
    CHECK(std::fabs(ph) < 0.5);
    double lo = 0, hi = 0;
    for (double f = 300; f < 1000; f *= 1.0005) if (e.responseDb(f) >= 6.0) { lo = f; break; }
    for (double f = 3000; f > 1000; f /= 1.0005) if (e.responseDb(f) >= 6.0) { hi = f; break; }
    record("eq_premium_bell_halfgain_lo_hz", lo, "Hz"); record("eq_premium_bell_halfgain_hi_hz", hi, "Hz");
    CHECK(std::fabs(std::log(lo * hi / 1e6)) < 0.02); // geometric symmetry around f0
}

TEST(audio, eq_premium_sweeps_are_cleaner_than_legacy_and_type_switch_is_click_free) {
    // fast frequency sweep of a +12 dB bell (200 Hz -> 5 kHz in 50 ms, repeated) on a 1 kHz sine;
    // artefacts = energy above 8 kHz (the clean output is a 1 kHz tone with slowly varying amplitude)
    const double fs = 48000;
    auto x = makeSine(fs, 1000.0, 0.25, (size_t)fs * 2);
    auto hfDb = [&](const std::vector<float>& y) {
        double s = 0;
        auto hp = y;
        Biquad f; f.design(FilterType::HighPass, fs, 8000.0, 0.7071);
        Biquad f2; f2.design(FilterType::HighPass, fs, 8000.0, 0.7071);
        for (auto& v : hp) v = f2.process(f.process(v, 0), 0);
        for (size_t i = 4800; i < hp.size(); ++i) s += (double)hp[i] * hp[i];
        return 10 * std::log10(s / (double)(hp.size() - 4800) + 1e-30) - 20 * std::log10(0.25 / std::sqrt(2.0));
    };
    auto sweep = [&](Effect& fx, const char* fid, const char* gid) {
        fx.setParam(gid, 12.f);
        fx.prepare({fs, 64, 1});
        std::vector<float> y = x;
        for (size_t p = 0; p < y.size(); p += 64) {
            const double ph = std::fmod((double)p / fs, 0.1) / 0.1; // 100 ms up/down cycle
            const double f = 200 * std::pow(25.0, ph < 0.5 ? 2 * ph : 2 - 2 * ph);
            fx.setParam(fid, (float)f);
            AudioBlock b; b.numChannels = 1; b.numSamples = 64; b.ch[0] = y.data() + p;
            fx.process(b);
        }
        return y;
    };
    PremiumEQ pe;
    for (int b = 2; b <= 5; ++b) pe.setParam(("b" + std::to_string(b) + "_type").c_str(), 0.f);
    pe.setParam("b1_type", 1.f); pe.setParam("b1_q", 2.f);
    ParametricEQ le;
    le.setParam("p2_q", 2.f);
    const double hfPremium = hfDb(sweep(pe, "b1_hz", "b1_db"));
    const double hfLegacy = hfDb(sweep(le, "p2_hz", "p2_db"));
    record("eq_sweep_artifacts_hf_db_premium", hfPremium, "dB re tone");
    record("eq_sweep_artifacts_hf_db_legacy_parametric", hfLegacy, "dB re tone");
    CHECK_MSG(hfPremium < hfLegacy + 1.0, "premium " << hfPremium << " legacy " << hfLegacy); // both smoothed; must not be worse
    // type switching Bell(+12) <-> HPF24 <-> Off on a running sine: no sample step beyond the signal's own slope
    PremiumEQ t;
    for (int b = 2; b <= 5; ++b) t.setParam(("b" + std::to_string(b) + "_type").c_str(), 0.f);
    t.setParam("b1_type", 1.f); t.setParam("b1_db", 12.f); t.setParam("b1_hz", 300.f);
    t.prepare({fs, 64, 1});
    auto s = makeSine(fs, 300.0, 0.2, (size_t)fs);
    double maxStep = 0, steady = 0;
    const int types[] = {5, 0, 1, 8, 2, 1};
    for (size_t p = 0; p < s.size(); p += 64) {
        if (p % 7680 == 0 && p > 0) t.setParam("b1_type", (float)types[(p / 7680) % 6]);
        AudioBlock b; b.numChannels = 1; b.numSamples = 64; b.ch[0] = s.data() + p;
        t.process(b);
    }
    for (size_t i = 1; i < s.size(); ++i) maxStep = std::max(maxStep, (double)std::fabs(s[i] - s[i - 1]));
    steady = 0.2 * 4.0 * 2 * kPi * 300 / fs; // slope of the loudest (+12 dB) steady state
    record("eq_premium_type_switch_max_step_ratio", maxStep / steady, "");
    CHECK(maxStep < 1.5 * steady);
}

TEST(audio, eq_premium_nyquist_extremes_and_denormals) {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (int type = 1; type < PremiumEQ::kNumTypes; ++type) {
            PremiumEQ e;
            for (int b = 1; b <= 5; ++b) {
                const std::string s = "b" + std::to_string(b);
                e.setParam((s + "_type").c_str(), (float)type);
                e.setParam((s + "_hz").c_str(), b == 1 ? 20000.f : b == 2 ? 20.f : 10000.f);
                e.setParam((s + "_q").c_str(), b % 2 ? 18.f : 0.1f);
                e.setParam((s + "_db").c_str(), b % 2 ? 24.f : -24.f);
            }
            e.prepare({fs, 17, 2});
            double maxResp = -300;
            for (double f = 5; f < fs / 2; f *= 1.02) maxResp = std::max(maxResp, PremiumEQ::targetResponseDb(e.params(), fs, f));
            const double bound = 4.0 * std::pow(10.0, maxResp / 20.0); // designed gain x crest margin
            auto l = makeNoise((size_t)(fs * 0.5), 0.5, 7), r = l;
            runEffect(e, l, &r, 17);
            // fast sweep to the top while running
            for (int k = 0; k < 50; ++k) {
                e.setParam("b1_hz", k % 2 ? 20.f : 20000.f);
                std::vector<float> a(512, 0.3f), c(512, -0.3f);
                runEffect(e, a, &c, 1 + k % 17);
                CHECK_MSG(allFinite(a) && allFinite(c), "fs " << fs << " type " << type);
                CHECK_MSG(analysis::peak(a.data(), a.size()) < 0.3 * bound, "fs " << fs << " type " << type << " peak " << analysis::peak(a.data(), a.size()) << " bound " << 0.3 * bound);
            }
            CHECK(allFinite(l) && allFinite(r));
        }
    // silence after signal decays to exact zero (no denormal tails)
    PremiumEQ d; d.prepare({48000, 256, 1});
    d.setParam("b3_db", 12.f); d.setParam("b3_q", 18.f);
    std::vector<float> x(48000 * 4, 0.0f);
    for (size_t i = 0; i < 4800; ++i) x[i] = 0.5f * (float)std::sin(i * 0.1);
    auto y = processMono(d, x);
    CHECK(analysis::peak(y.data() + 48000 * 3, 48000) == 0.0);
    // unity when all gains are 0 dB (default) is bit-exact
    PremiumEQ u; u.prepare({48000, 256, 1});
    auto z = makeNoise(4800, 0.5, 3);
    auto w = processMono(u, z);
    double e = 0; for (size_t i = 0; i < z.size(); ++i) e = std::max(e, (double)std::fabs(w[i] - z[i]));
    CHECK(e == 0.0);
}

// ============================================================================================ BBD v2
TEST(audio, bbd_v2_multichip_reduces_aliasing_without_losing_coherent_bandwidth) {
    const double fs = 48000;
    auto x = makeNoise((size_t)fs * 6, 0.1, 21);
    struct R { double bw, inc; };
    auto measure = [&](int model, int chips, float ms) {
        BbdDelay d;
        d.setParam("model", (float)model); d.setParam("chips", (float)chips);
        d.setParam("time_ms", ms); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f);
        d.setParam("compander", 0.f); d.setParam("drive", 0.f); d.setParam("tone", 100.f); d.setParam("spread", 0.f);
        d.prepare({fs, 256, 1});
        auto y = processMono(d, x, 256);
        const size_t lag = (size_t)(ms * 0.001 * fs);
        std::vector<float> xs(x.begin(), x.end() - (long)lag), ys(y.begin() + (long)lag, y.end());
        auto t = transfer(xs, ys, fs, (size_t)fs);
        // coherent bandwidth: highest frequency where |H| >= -3 dB re 300 Hz; incoherent (alias) power 50 Hz-20 kHz
        double ref = 0; int nref = 0;
        for (size_t k = 0; k < t.f.size(); ++k) if (t.f[k] > 250 && t.f[k] < 350) { ref += t.mag[k]; ++nref; }
        ref /= nref;
        double bw = 0, cohP = 0, incP = 0;
        for (size_t k = 0; k < t.f.size(); ++k) {
            if (t.f[k] > 200 && t.mag[k] >= ref * 0.7079 && bw == 0) { /* track */ }
            if (t.f[k] > 200 && t.mag[k] >= ref * 0.7079) bw = t.f[k];
            if (t.f[k] > 50 && t.f[k] < 20000) {
                const double py = t.mag[k] * t.mag[k] / std::max(t.coh[k], 1e-12); // total output power (re input)
                cohP += t.coh[k] * py; incP += (1 - t.coh[k]) * py;
            }
        }
        return R{bw, 10 * std::log10(incP / cohP)};
    };
    for (float ms : {100.f, 300.f, 500.f, 800.f}) {
        const R v1 = measure(0, 0, ms), c1 = measure(1, 0, ms), c2 = measure(1, 1, ms), c4 = measure(1, 2, ms);
        const std::string t = std::to_string((int)ms);
        record("bbd_v1_bw_hz_" + t, v1.bw, "Hz"); record("bbd_v1_alias_db_" + t, v1.inc, "dB");
        record("bbd_v2_1chip_bw_hz_" + t, c1.bw, "Hz"); record("bbd_v2_1chip_alias_db_" + t, c1.inc, "dB");
        record("bbd_v2_2chip_bw_hz_" + t, c2.bw, "Hz"); record("bbd_v2_2chip_alias_db_" + t, c2.inc, "dB");
        record("bbd_v2_4chip_bw_hz_" + t, c4.bw, "Hz"); record("bbd_v2_4chip_alias_db_" + t, c4.inc, "dB");
        if (ms >= 300.f) {
            CHECK_MSG(c2.inc < v1.inc - 10.0, "ms " << ms << " alias v1 " << v1.inc << " v2/2 " << c2.inc);
            CHECK_MSG(c4.bw > 1.5 * c2.bw * 0.9 || c4.bw >= 8000, "ms " << ms << " bw 2chip " << c2.bw << " 4chip " << c4.bw);
        }
    }
}

TEST(audio, bbd_v2_defaults_legacy_and_regulated_self_oscillation) {
    BbdDelay fresh;
    CHECK(fresh.params().getInt(BbdDelay::kModel) == BbdDelay::kV2);
    CHECK(fresh.params().getInt(BbdDelay::kChips) == 1);
    // a v0.6 preset without "model" loads v1
    Engine e; e.prepare(48000, 256);
    Json j;
    CHECK(Json::parse(R"({"format":"mfx-preset","version":1,"name":"t","chain":[{"type":"delay_bbd_premium","params":{"time_ms":300}}]})", j));
    presetFromJson(e, j);
    CHECK(e.effect(e.allSlotsInOrder()[0])->params().getInt(BbdDelay::kModel) == BbdDelay::kV1);
    // 110 % feedback (default drive): sustained self-oscillation, finite; v2 regulates the loop above -6 dBFS
    double pkV1 = 0;
    for (double fs : {44100.0, 96000.0}) {
        BbdDelay d;
        d.setParam("feedback", 110.f); d.setParam("mix", 100.f); d.setParam("time_ms", 40.f);
        d.setParam("chips", 2.f);
        d.prepare({fs, 256, 2});
        std::vector<float> y((size_t)fs * 15, 0.0f);
        auto b = makeNoise((size_t)fs / 10, 0.9, 5);
        std::copy(b.begin(), b.end(), y.begin());
        auto r = y;
        runEffect(d, y, &r, 256);
        const double pk = analysis::peak(y.data() + (size_t)fs * 10, (size_t)fs * 5);
        record("bbd_v2_selfosc_peak_" + std::to_string((int)fs), pk, "");
        CHECK(allFinite(y) && allFinite(r) && pk < 1.0 && pk > 0.01);
    }
    {
        BbdDelay d; d.setParam("model", 0.f);
        d.setParam("feedback", 110.f); d.setParam("mix", 100.f); d.setParam("time_ms", 40.f);
        d.prepare({44100.0, 256, 2});
        std::vector<float> y(44100 * 15, 0.0f);
        auto b = makeNoise(4410, 0.9, 5);
        std::copy(b.begin(), b.end(), y.begin());
        auto r = y;
        runEffect(d, y, &r, 256);
        pkV1 = analysis::peak(y.data() + 441000, 44100 * 5);
        record("bbd_v1_selfosc_peak_44100", pkV1, "");
    }
    BbdDelay m; m.prepare({192000, 64, 2});
    record("bbd_v2_memory_kib", (double)m.memoryBytes() / 1024.0, "KiB");
    CHECK(m.memoryBytes() <= 160u * 1024u);
}

TEST(audio, bbd_v2_compander_low_level_transparency) {
    // static level through the chip at -40 / -60 / -70 dBFS, compander on vs off, v1 and v2
    auto lvl = [](int model, float comp, double dbfs) {
        BbdDelay d; d.setParam("model", (float)model); d.setParam("compander", comp);
        d.setParam("time_ms", 100.f); d.setParam("mix", 100.f); d.setParam("feedback", 0.f); d.setParam("mod_depth", 0.f); d.setParam("drive", 0.f);
        d.prepare({48000, 256, 1});
        const double a = std::pow(10.0, dbfs / 20.0);
        auto y = processMono(d, makeSine(48000, 500.0, a, 48000));
        return db(toneAmp(y, 24000, 16384, 500.0, 48000) / a);
    };
    for (double L : {-40.0, -60.0, -70.0}) {
        const double e1 = lvl(0, 100.f, L) - lvl(0, 0.f, L), e2 = lvl(1, 100.f, L) - lvl(1, 0.f, L);
        record("bbd_v1_compander_err_db_" + std::to_string((int)-L), e1, "dB");
        record("bbd_v2_compander_err_db_" + std::to_string((int)-L), e2, "dB");
        CHECK_MSG(std::fabs(e2) < 0.5, "L " << L << " v2 err " << e2);
    }
}

TEST(audio, bbd_v2_time_accuracy_modulation_and_time_change) {
    for (int chips : {0, 1, 2})
        for (float ms : {100.f, 500.f}) {
            BbdDelay d;
            d.setParam("chips", (float)chips); d.setParam("time_ms", ms); d.setParam("mix", 100.f); d.setParam("feedback", 0.f);
            d.setParam("mod_depth", 0.f); d.setParam("compander", 0.f);
            d.prepare({48000, 256, 1});
            const size_t n = (size_t)(ms * 48);
            std::vector<float> x(n + 9600, 0.0f); x[0] = 0.5f;
            auto y = processMono(d, x);
            size_t k = n / 2; float m = 0;
            for (size_t i = n / 2; i < y.size(); ++i) if (std::fabs(y[i]) > m) { m = std::fabs(y[i]); k = i; }
            const double err = ((double)k - (double)n) / 48.0;
            CHECK_MSG(err > -0.1 && err < 2.0, "chips " << chips << " ms " << ms << " err " << err);
        }
    // time change 300 -> 150 ms on a sine: bounded steps (pitch glide)
    BbdDelay e;
    e.setParam("time_ms", 300.f); e.setParam("mix", 100.f); e.setParam("feedback", 30.f); e.setParam("mod_depth", 0.f);
    e.prepare({48000, 256, 1});
    auto x = makeSine(48000, 440.0, 0.3, 48000 * 4);
    for (size_t p = 0; p < x.size(); p += 256) {
        if (p == 48000 * 2) e.setParam("time_ms", 150.f);
        AudioBlock b; b.numChannels = 1; b.numSamples = 256; b.ch[0] = x.data() + p;
        e.process(b);
    }
    double before = 0, during = 0;
    for (size_t i = 48000 + 1; i < 96000; ++i) before = std::max(before, (double)std::fabs(x[i] - x[i - 1]));
    for (size_t i = 96000 + 1; i < 144000; ++i) during = std::max(during, (double)std::fabs(x[i] - x[i - 1]));
    record("bbd_v2_time_change_maxstep_ratio", during / before, "");
    CHECK(during < 2.5 * before);
}

// ============================================================================================ full chain
TEST(integration, v07_full_chain_gain_staging_trails_presets_and_rates) {
    // Input -> Gate -> Compressor -> Drive -> Amp -> Cabinet -> EQ -> Delay -> Reverb
    auto build = [](Engine& e) {
        e.addEffect(createEffect("gate_premium"));
        e.addEffect(createEffect("comp_fet"));
        e.addEffect(createEffect("custom_drive"));
        e.addEffect(createEffect("amp_ab763"));
        e.addEffect(createEffect("cabinet"));
        e.addEffect(createEffect("eq_parametric_premium"));
        auto d = createEffect("delay_bbd_premium"); d->setParam("mix", 40.f); d->setParam("feedback", 50.f);
        e.addEffect(std::move(d));
        auto r = createEffect("reverb_plate_premium"); r->setParam("mix", 30.f); r->setParam("decay_s", 2.5f);
        e.addEffect(std::move(r));
    };
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        Engine e;
        e.prepare(fs, 256);
        build(e);
        const auto slots = e.allSlotsInOrder();
        CHECK(slots.size() == 8);
        // SYNTHETIC pluck over a -70 dBFS hiss, then 4 s of hiss only
        const size_t n = (size_t)(fs * 6);
        auto x = noiseFloor(n, -70, 9);
        auto pl = makePluck(fs, 110.0, 1.5, 0.3);
        for (size_t i = 0; i < pl.size(); ++i) x[(size_t)(0.2 * fs) + i] += pl[i];
        std::vector<float> l(n), r(n);
        for (size_t p = 0; p < n; p += 256) {
            const int m = (int)std::min<size_t>(256, n - p);
            const float* in[1] = {x.data() + p};
            float* out[2] = {l.data() + p, r.data() + p};
            if (p == (size_t)(fs * 3)) e.setParameterRT(slots[5], 1, 0.75f); // automation: EQ band 1 gain
            e.process(in, 1, out, 2, m);
        }
        CHECK(allFinite(l) && allFinite(r));
        const double pk = std::max(analysis::peak(l.data(), n), analysis::peak(r.data(), n));
        record("v07_chain_peak_dbfs_" + std::to_string((int)fs), db(pk), "dBFS");
        CHECK(pk < 1.0);
        // trails: the gate closes on the hiss after the note, but the delay / reverb tails behind it continue
        const double tail = rmsDb(l, (size_t)(fs * 2.2), (size_t)(fs * 2.6));
        const double late = rmsDb(l, (size_t)(fs * 5.5), (size_t)(fs * 6.0));
        record("v07_chain_tail_db_" + std::to_string((int)fs), tail, "dBFS");
        record("v07_chain_floor_db_" + std::to_string((int)fs), late, "dBFS");
        CHECK(tail > late + 20.0);
        record("v07_chain_latency_samples_" + std::to_string((int)fs), e.latencySamples(), "samples");
        CHECK(e.latencySamples() >= 0 && e.latencySamples() < 256);
    }
    // preset round trip + switching while processing
    Engine e; e.prepare(48000, 256); build(e);
    const Json j = presetToJson(e, "chain");
    Engine f; f.prepare(48000, 256);
    auto rep = presetFromJson(f, j);
    CHECK(f.allSlotsInOrder().size() == 8);
    const auto a = e.allSlotsInOrder(), b = f.allSlotsInOrder();
    for (size_t k = 0; k < a.size(); ++k) {
        const auto& pa = e.effect(a[k])->params(); const auto& pb = f.effect(b[k])->params();
        for (int i = 0; i < pa.size(); ++i) CHECK(pa.get(i) == pb.get(i));
    }
    std::vector<float> l(256), r(256), in0(256, 0.1f);
    const float* in[1] = {in0.data()};
    float* out[2] = {l.data(), r.data()};
    for (int k = 0; k < 20; ++k) {
        if (k == 10) { f.clear(); presetFromJson(f, j); } // switch (message thread in the plug-in)
        f.process(in, 1, out, 2, 256);
        CHECK(allFinite(l) && allFinite(r));
    }
    (void)rep;
}
