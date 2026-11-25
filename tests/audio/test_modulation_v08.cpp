// v0.8 Premium Modulation: measurements and regression checks (synthetic signals only; no guitar DI in the repo).
#include <chrono>
#include <cmath>
#include <complex>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/modulation/Modulation.h"
#include "dsp/modulation/PremiumMod.h"
#include "engine/EffectFactory.h"
#include <limits>

using namespace mfx;
using namespace mfxtest;

namespace {
double rmsOf(const std::vector<float>& y, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)y[i] * y[i];
    return std::sqrt(s / (double)(b - a));
}
double ampAt(const std::vector<float>& y, size_t a, size_t n, double f, double fs) {
    std::complex<double> acc = 0; double w = 0;
    for (size_t i = 0; i < n; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        acc += h * (double)y[a + i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / fs));
        w += h;
    }
    return 2.0 * std::abs(acc) / w;
}
// zero-crossing instantaneous pitch (cents re f0) with times (s)
void pitchTrack(const std::vector<float>& y, size_t a, size_t b, double fs, double f0, std::vector<double>& cents, std::vector<double>& t) {
    double last = -1;
    for (size_t i = a + 1; i < b; ++i)
        if (y[i - 1] < 0 && y[i] >= 0) {
            const double tc = (double)(i - 1) + y[i - 1] / (y[i - 1] - y[i]);
            if (last >= 0) { cents.push_back(1200 * std::log2(fs / (tc - last) / f0)); t.push_back(0.5 * (tc + last) / fs); }
            last = tc;
        }
}
double dominantHz(const std::vector<double>& v, const std::vector<double>& t, double fmax) {
    double m = 0; for (double x : v) m += x; m /= (double)v.size();
    double best = 0, bf = 0;
    for (double f = 0.1; f < fmax; f += 0.01) {
        std::complex<double> acc = 0;
        for (size_t i = 0; i < v.size(); ++i) acc += (v[i] - m) * std::exp(std::complex<double>(0, -2 * kPi * f * t[i]));
        if (std::abs(acc) > best) { best = std::abs(acc); bf = f; }
    }
    return bf;
}
// envelope (RMS in 2 ms frames) for AM measurements
std::vector<double> envelope(const std::vector<float>& y, size_t a, size_t b, size_t w, std::vector<double>& t, double fs) {
    std::vector<double> e;
    for (size_t i = a; i + w <= b; i += w) { e.push_back(rmsOf(y, i, i + w)); t.push_back((i + w * 0.5) / fs); }
    return e;
}
std::vector<float> stereoRun(Effect& fx, std::vector<float>& l, std::vector<float>& r, int block = 256) { runEffect(fx, l, &r, block); return l; }
double maxStep(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < b; ++i) m = std::max(m, (double)std::fabs(y[i] - y[i - 1])); return m; }
} // namespace

// ------------------------------------------------------------------------------------------ interpolator study
TEST(audio, mod_fractional_delay_interpolator_comparison) {
    // vibrato-like modulated delay of a 6 kHz sine vs the analytic result: error and cost per interpolator
    const double fs = 48000, f = 6000, B = 240, A = 40, fr = 5;
    const size_t n = 48000;
    std::vector<float> x(n + 1024);
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float)std::sin(2 * kPi * f * (double)i / fs);
    auto run = [&](int kind, double* ns) {
        double err = 0, sig = 0, thiranState = 0, prevIn = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 600; i < n; ++i) {
            const double d = B + A * std::sin(2 * kPi * fr * i / fs);
            const double pos = (double)i - d;
            const long k = (long)std::floor(pos);
            const double u = pos - k;
            const float xm1 = x[k - 1], x0 = x[k], x1 = x[k + 1], x2 = x[k + 2];
            double y;
            if (kind == 0) y = x0 + u * (x1 - x0);
            else if (kind == 1) {
                const double c1 = 0.5 * (x1 - xm1), c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2, c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
                y = ((c3 * u + c2) * u + c1) * u + x0;
            } else if (kind == 2) { // 4-point Lagrange (3rd order)
                const double um = u + 1, u1 = u - 1, u2 = u - 2;
                y = -xm1 * u * u1 * u2 / 6 + x0 * um * u1 * u2 / 2 - x1 * um * u * u2 / 2 + x2 * um * u * u1 / 6;
            } else { // first-order Thiran all-pass (delay 0.5..1.5 around the integer part)
                double uu = u; long kk = k;
                if (uu < 0.5) { uu += 1; kk -= 1; }
                const double a = (1 - uu) / (1 + uu);
                const double in = x[kk];
                y = a * in + prevIn - a * thiranState;
                prevIn = in; thiranState = y;
                (void)x1;
            }
            const double ideal = std::sin(2 * kPi * f * pos / fs);
            if (i > 2000) { err += (y - ideal) * (y - ideal); sig += ideal * ideal; }
        }
        *ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)(n - 600);
        return 10 * std::log10(err / sig);
    };
    const char* names[] = {"linear", "hermite", "lagrange3", "thiran1"};
    double e[4], c[4];
    for (int k = 0; k < 4; ++k) { e[k] = run(k, &c[k]); record(std::string("interp_err_db_6k_") + names[k], e[k], "dB"); record(std::string("interp_ns_") + names[k], c[k], "ns/sample"); }
    CHECK(e[1] < e[0] - 10.0); // Hermite clearly better than linear at 6 kHz
}

// ------------------------------------------------------------------------------------------ chorus
TEST(audio, chorus_premium_rate_depth_stereo_and_mono) {
    const double fs = 48000;
    PremiumChorus ch;
    ch.setParam("rate_hz", 1.0f); ch.setParam("depth", 50.f); ch.setParam("mix", 100.f); ch.setParam("drift", 0.f);
    ch.setParam("voices", 0.f); ch.setParam("character", 0.f); ch.setParam("tone_hz", 18000.f); ch.setParam("spread_deg", 180.f);
    ch.prepare({fs, 256, 2});
    auto l = makeSine(fs, 1000, 0.3, (size_t)fs * 8), r = l;
    stereoRun(ch, l, r);
    std::vector<double> c, t;
    pitchTrack(l, (size_t)fs, (size_t)fs * 8, fs, 1000, c, t);
    const double rate = dominantHz(c, t, 5.0);
    double mx = -1e9, mn = 1e9; for (double v : c) { mx = std::max(mx, v); mn = std::min(mn, v); }
    // d = base + swing/2 (1 + sin): peak pitch deviation = pi f_lfo swing / fs  (swing = 3 ms at depth 50 %)
    const double expect = 1200 * std::log2(1 + kPi * 1.0 * 0.003);
    record("chorus_premium_rate_hz_set1", rate, "Hz"); record("chorus_premium_peak_cents", 0.5 * (mx - mn), "cents");
    record("chorus_premium_peak_cents_expected", expect, "cents");
    CHECK(std::fabs(rate - 1.0) < 0.03);
    CHECK(std::fabs(0.5 * (mx - mn) - expect) < 0.15 * expect);
    // stereo: L/R decorrelated; mono fold-down level stability (dry + wet, mix 50 %) with noise
    PremiumChorus s; s.setParam("mix", 50.f); s.prepare({fs, 256, 2});
    auto nl_ = makeNoise((size_t)fs * 6, 0.2, 3), nr = nl_;
    stereoRun(s, nl_, nr);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = (size_t)fs; i < nl_.size(); ++i) { sl += nl_[i] * nl_[i]; sr += nr[i] * nr[i]; slr += nl_[i] * nr[i]; }
    record("chorus_premium_lr_corr", slr / std::sqrt(sl * sr), "");
    std::vector<float> mono(nl_.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (nl_[i] + nr[i]);
    std::vector<double> lv; for (size_t i = (size_t)fs; i + 4800 <= mono.size(); i += 4800) lv.push_back(20 * std::log10(rmsOf(mono, i, i + 4800)));
    double lmx = -1e9, lmn = 1e9; for (double v : lv) { lmx = std::max(lmx, v); lmn = std::min(lmn, v); }
    record("chorus_premium_mono_level_range_db", lmx - lmn, "dB");
    CHECK(lmx - lmn < 1.5);
}

TEST(audio, chorus_premium_metallic_artifacts_and_fast_changes) {
    // long-term spectrum peakiness of the 50 % mix (comb filtering): voices 1 vs 3 and vs the legacy chorus
    const double fs = 48000;
    auto peaky = [&](Effect& fx) {
        fx.prepare({fs, 256, 2});
        auto l = makeNoise((size_t)fs * 8, 0.2, 5), r = l;
        stereoRun(fx, l, r);
        const size_t N = 8192; std::vector<double> P(N / 2, 0.0);
        for (size_t s0 = (size_t)fs; s0 + N <= l.size(); s0 += N / 2)
            for (size_t k = 20; k < N / 2; k += 1) { // coarse DFT on a band of bins is too slow: use Goertzel on log-spaced bins
                (void)k; break;
            }
        // Goertzel on 1/24-octave centres 200 Hz - 8 kHz, averaged over frames
        std::vector<double> band;
        for (double f = 200; f < 8000; f *= std::pow(2.0, 1.0 / 24)) {
            double acc = 0; int nf = 0;
            for (size_t s0 = (size_t)fs; s0 + 4096 <= l.size(); s0 += 4096, ++nf) acc += std::pow(ampAt(l, s0, 4096, f, fs), 2);
            band.push_back(10 * std::log10(acc / nf));
        }
        double peak = 0;
        for (size_t k = 3; k + 3 < band.size(); ++k) { double m = 0; for (int j = -3; j <= 3; ++j) m += band[k + j]; peak = std::max(peak, std::fabs(band[k] - m / 7)); }
        (void)P;
        return peak;
    };
    PremiumChorus v1; v1.setParam("voices", 0.f);
    PremiumChorus v2; v2.setParam("voices", 1.f);
    PremiumChorus v3; v3.setParam("voices", 2.f);
    PremiumChorus d; // defaults
    Chorus legacy;
    const double p1 = peaky(v1), p2 = peaky(v2), p3 = peaky(v3), pd = peaky(d), pl = peaky(legacy);
    record("chorus_premium_comb_peakiness_db_1voice", p1, "dB"); record("chorus_premium_comb_peakiness_db_2voices", p2, "dB");
    record("chorus_premium_comb_peakiness_db_3voices", p3, "dB"); record("chorus_legacy_comb_peakiness_db", pl, "dB");
    record("chorus_premium_comb_peakiness_db_default", pd, "dB");
    CHECK(pd <= pl + 0.25); // the default must not be more comb-like than the legacy chorus
    // fast depth / rate / delay changes on a sine: no step beyond the signal's own slope (x pitch factor)
    PremiumChorus f; f.setParam("mix", 50.f); f.prepare({fs, 64, 2});
    auto l = makeSine(fs, 440, 0.3, (size_t)fs * 3), r = l;
    for (size_t p = 0; p < l.size(); p += 64) {
        if (p % 9600 == 0) { f.setParam("depth", (p / 9600) % 2 ? 100.f : 0.f); f.setParam("rate_hz", (p / 9600) % 2 ? 8.f : 0.1f); f.setParam("delay_ms", (p / 9600) % 2 ? 25.f : 5.f); }
        AudioBlock b; b.numChannels = 2; b.numSamples = 64; b.ch[0] = l.data() + p; b.ch[1] = r.data() + p;
        f.process(b);
    }
    const double st = maxStep(l, 4800, l.size()) / (0.3 * 2 * kPi * 440 / fs);
    record("chorus_premium_fast_change_maxstep_ratio", st, "");
    CHECK(st < 1.5);
}

// ------------------------------------------------------------------------------------------ vibrato
TEST(audio, vibrato_premium_depth_accuracy_zero_depth_and_rate_changes) {
    const double fs = 48000;
    struct C { int shape; float rate, cents; };
    double worst = 0;
    for (C cs : {C{0, 5.f, 25.f}, C{0, 2.f, 10.f}, C{1, 6.f, 30.f}, C{2, 4.f, 20.f}, C{0, 9.f, 50.f}}) {
        PremiumVibrato v;
        v.setParam("shape", (float)cs.shape); v.setParam("rate_hz", cs.rate); v.setParam("depth_cents", cs.cents); v.setParam("drift", 0.f);
        v.prepare({fs, 256, 1});
        auto y = processMono(v, makeSine(fs, 800, 0.3, (size_t)fs * 6));
        std::vector<double> c, t;
        pitchTrack(y, (size_t)fs, (size_t)fs * 6, fs, 800, c, t);
        double mx = 0; for (double e : c) mx = std::max(mx, std::fabs(e)); // peak deviation, either direction
        const double rate = dominantHz(c, t, 15.0);
        worst = std::max(worst, std::fabs(mx - cs.cents) / cs.cents);
        record("vibrato_premium_peak_cents_shape" + std::to_string(cs.shape) + "_" + std::to_string((int)cs.rate) + "hz_set" + std::to_string((int)cs.cents), mx, "cents");
        CHECK_MSG(std::fabs(rate - cs.rate) < 0.05, "rate " << rate);
    }
    record("vibrato_premium_depth_max_rel_err", worst, "");
    CHECK(worst < 0.12); // zero-crossing estimate of a frequency-modulated 800 Hz tone
    // zero depth: exact copy delayed by the reported latency
    PremiumVibrato z; z.setParam("depth_cents", 0.f); z.prepare({fs, 256, 1});
    auto x = makeNoise(9600, 0.5, 2);
    auto y = processMono(z, x);
    double e = 0; for (size_t i = (size_t)z.latencySamples(); i < x.size(); ++i) e = std::max(e, (double)std::fabs(y[i] - x[i - z.latencySamples()]));
    record("vibrato_premium_latency_samples_48k", z.latencySamples(), "samples");
    CHECK(e == 0.0);
    // abrupt rate changes: the phase is continuous (no step beyond the slope of the pitch-shifted sine)
    PremiumVibrato r; r.setParam("depth_cents", 40.f); r.prepare({fs, 64, 1});
    auto s = makeSine(fs, 440, 0.3, (size_t)fs * 3);
    for (size_t p = 0; p < s.size(); p += 64) {
        if (p % 12000 == 0) r.setParam("rate_hz", (p / 12000) % 2 ? 12.f : 0.5f);
        AudioBlock b; b.numChannels = 1; b.numSamples = 64; b.ch[0] = s.data() + p;
        r.process(b);
    }
    const double st = maxStep(s, 4800, s.size()) / (0.3 * 2 * kPi * 440 / fs);
    record("vibrato_premium_rate_change_maxstep_ratio", st, "");
    CHECK(st < 1.2);
}

// ------------------------------------------------------------------------------------------ phaser
TEST(audio, phaser_premium_notches_sweep_feedback_and_stereo) {
    const double fs = 48000;
    // static (depth 0): centre = sqrt(lo*hi); N identical first-order all-pass stages, 50/50 mix:
    // notches at fc * tan((2k+1) pi / (2N)), k = 0 .. N/2-1
    for (int st : {1, 3}) { // 4 and 8 stages
        PremiumPhaser p;
        p.setParam("depth", 0.f); p.setParam("freq_lo_hz", 250.f); p.setParam("freq_hi_hz", 4000.f); p.setParam("feedback", 0.f);
        p.setParam("mix", 50.f); p.setParam("stages", (float)st); p.setParam("character", 0.f);
        p.prepare({fs, 256, 1});
        std::vector<float> imp(32768, 0.0f); imp[0] = 1.0f;
        auto h = processMono(p, imp);
        const int N = st == 1 ? 4 : 8;
        const double fc = std::sqrt(250.0 * 4000.0);
        for (int k = 0; k < N / 2; ++k) {
            // digital (TPT) prototype: tan(pi f/fs) = tan(pi fc/fs) * tan((2k+1) pi / 2N)
            const double fn = fs / kPi * std::atan(std::tan(kPi * fc / fs) * std::tan((2 * k + 1) * kPi / (2 * N)));
            double best = 1e9, bf = 0;
            for (double f = fn * 0.8; f < std::min(fn * 1.25, 0.49 * fs); f *= 1.0005) {
                std::complex<double> H = 0;
                for (size_t i = 0; i < 8192; ++i) H += (double)h[i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / fs));
                if (std::abs(H) < best) { best = std::abs(H); bf = f; }
            }
            record("phaser_premium_notch_err_pct_N" + std::to_string(N) + "_k" + std::to_string(k), 100 * (bf / fn - 1), "%");
            CHECK_MSG(std::fabs(bf / fn - 1) < 0.01, "N " << N << " k " << k << " " << bf << " vs " << fn);
        }
    }
    // sweep range follows lo / hi
    PremiumPhaser s; s.setParam("depth", 100.f); s.setParam("freq_lo_hz", 300.f); s.setParam("freq_hi_hz", 3000.f); s.setParam("rate_hz", 1.f);
    s.setParam("character", 0.f); s.prepare({fs, 64, 1});
    double mn = 1e9, mx = 0;
    std::vector<float> blk(64, 0.0f);
    for (int k = 0; k < 48000 / 64 * 2; ++k) { processMono(s, blk, 64); mn = std::min(mn, s.stageHz(0, 0)); mx = std::max(mx, s.stageHz(0, 0)); }
    record("phaser_premium_sweep_min_hz", mn, "Hz"); record("phaser_premium_sweep_max_hz", mx, "Hz");
    CHECK(std::fabs(mn / 300 - 1) < 0.02 && std::fabs(mx / 3000 - 1) < 0.02);
    // feedback: resonance peak grows with |feedback|, bounded at +/-95 %, stereo decorrelation
    for (float fb : {0.f, 60.f, 95.f, -95.f}) {
        PremiumPhaser p; p.setParam("feedback", fb); p.setParam("mix", 50.f); p.setParam("depth", 0.f);
        p.prepare({fs, 256, 2});
        auto l = makeNoise((size_t)fs * 3, 0.5, 7), r = l;
        stereoRun(p, l, r);
        const double pk = analysis::peak(l.data(), l.size());
        record("phaser_premium_peak_fb_" + std::to_string((int)fb), pk, "");
        CHECK(allFinite(l) && pk < 8.0);
    }
    PremiumPhaser c; c.setParam("stereo_deg", 180.f); c.setParam("mix", 100.f); c.prepare({fs, 256, 2});
    auto l = makeNoise((size_t)fs * 4, 0.3, 8), r = l;
    stereoRun(c, l, r);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = (size_t)fs; i < l.size(); ++i) { sl += l[i] * l[i]; sr += r[i] * r[i]; slr += l[i] * r[i]; }
    record("phaser_premium_lr_corr_180deg", slr / std::sqrt(sl * sr), "");
    CHECK(slr / std::sqrt(sl * sr) < 0.95);
    // parameter jumps (range / stages) on a sine: no clicks
    PremiumPhaser j; j.setParam("mix", 50.f); j.prepare({fs, 64, 1});
    auto x = makeSine(fs, 440, 0.3, (size_t)fs * 2);
    for (size_t p = 0; p < x.size(); p += 64) {
        if (p % 9600 == 0) { j.setParam("freq_lo_hz", (p / 9600) % 2 ? 1500.f : 60.f); j.setParam("freq_hi_hz", (p / 9600) % 2 ? 9000.f : 400.f); }
        AudioBlock b; b.numChannels = 1; b.numSamples = 64; b.ch[0] = x.data() + p;
        j.process(b);
    }
    const double stp = maxStep(x, 4800, x.size()) / (0.3 * 2 * kPi * 440 / fs);
    record("phaser_premium_param_jump_maxstep_ratio", stp, "");
    CHECK(stp < 2.0);
}

// ------------------------------------------------------------------------------------------ univibe
TEST(audio, univibe_premium_differs_from_phaser) {
    const double fs = 48000;
    PremiumUnivibe u; u.setParam("speed_hz", 1.f); u.setParam("intensity", 100.f); u.prepare({fs, 64, 1});
    std::vector<float> blk(64, 0.0f);
    std::vector<double> f1, tt;
    for (int k = 0; k < 48000 / 64 * 4; ++k) { processMono(u, blk, 64); f1.push_back(u.stageHz(0)); tt.push_back(k * 64 / fs); }
    // non-uniform stages: ratio between the highest and lowest stage corner (phaser stages are equal)
    const double ratio = u.stageHz(2) / u.stageHz(1);
    record("univibe_stage_corner_ratio_max_min", ratio, "");
    CHECK(ratio > 100.0);
    // sweep asymmetry: time to rise from 10 % to 90 % of the log-frequency swing vs time to fall back
    double lo = 1e9, hi = 0; for (size_t i = f1.size() / 2; i < f1.size(); ++i) { lo = std::min(lo, f1[i]); hi = std::max(hi, f1[i]); }
    const double l10 = std::log(lo) + 0.1 * std::log(hi / lo), l90 = std::log(lo) + 0.9 * std::log(hi / lo);
    double rise = 0, fall = 0; int nr = 0, nf = 0;
    double tUp = -1, tDn = -1;
    for (size_t i = f1.size() / 2 + 1; i < f1.size(); ++i) {
        const double a = std::log(f1[i - 1]), b = std::log(f1[i]);
        if (a < l10 && b >= l10) tUp = tt[i];
        if (a < l90 && b >= l90 && tUp > 0) { rise += tt[i] - tUp; ++nr; tUp = -1; }
        if (a > l90 && b <= l90) tDn = tt[i];
        if (a > l10 && b <= l10 && tDn > 0) { fall += tt[i] - tDn; ++nf; tDn = -1; }
    }
    rise /= std::max(nr, 1); fall /= std::max(nf, 1);
    record("univibe_sweep_rise_s", rise, "s"); record("univibe_sweep_fall_s", fall, "s");
    record("univibe_sweep_range_ratio", hi / lo, "");
    CHECK(nr > 0 && nf > 0 && std::fabs(rise - fall) / std::max(rise, fall) > 0.15);
    // vibrato mode = pitch modulation, chorus mode = notches (level dips) with mostly constant pitch
    auto pitchDev = [&](int mode) {
        PremiumUnivibe v; v.setParam("mode", (float)mode); v.setParam("speed_hz", 4.f); v.prepare({fs, 256, 1});
        auto y = processMono(v, makeSine(fs, 600, 0.3, (size_t)fs * 4));
        std::vector<double> c, t; pitchTrack(y, (size_t)fs, (size_t)fs * 4, fs, 600, c, t);
        double mx = -1e9, mn = 1e9; for (double e : c) { mx = std::max(mx, e); mn = std::min(mn, e); }
        return 0.5 * (mx - mn);
    };
    const double pv = pitchDev(1), pc = pitchDev(0);
    record("univibe_vibrato_mode_peak_cents", pv, "cents"); record("univibe_chorus_mode_peak_cents", pc, "cents");
    CHECK(pv > 3.0);
}

// ------------------------------------------------------------------------------------------ flanger
TEST(audio, flanger_premium_comb_through_zero_feedback_latency) {
    const double fs = 48000;
    auto response = [&](PremiumFlanger& f, double hz) {
        f.reset();
        auto y = processMono(f, makeSine(fs, hz, 0.1, (size_t)fs / 2));
        return ampAt(y, (size_t)fs / 4, 8192, hz, fs) / 0.1;
    };
    // classic, depth 0, manual 1 ms, mix 50 %, no feedback: notches at (2k+1) / (2 * 1 ms) = 500, 1500 Hz
    PremiumFlanger c; c.setParam("mode", 0.f); c.setParam("depth", 0.f); c.setParam("manual_ms", 1.f); c.setParam("feedback", 0.f); c.setParam("mix", 50.f);
    c.prepare({fs, 256, 1});
    for (double fnotch : {500.0, 1500.0}) {
        const double g = response(c, fnotch), g2 = response(c, fnotch * 2);
        record("flanger_premium_notch_db_" + std::to_string((int)fnotch), 20 * std::log10(g + 1e-9), "dB");
        CHECK(g < 0.01 && g2 > 0.9);
    }
    // through-zero: manual 0, depth 0 -> wet == dry exactly (no comb): bit-exact delayed copy
    PremiumFlanger z; z.setParam("mode", 1.f); z.setParam("depth", 0.f); z.setParam("manual_ms", 0.f); z.setParam("feedback", 0.f);
    z.prepare({fs, 256, 1});
    auto x = makeNoise(9600, 0.5, 3);
    auto y = processMono(z, x);
    double e = 0; for (size_t i = (size_t)z.latencySamples(); i < x.size(); ++i) e = std::max(e, (double)std::fabs(y[i] - x[i - z.latencySamples()]));
    record("flanger_premium_latency_samples_48k", z.latencySamples(), "samples");
    CHECK(e < 1e-6);
    // the relative delay really crosses zero while sweeping
    PremiumFlanger s; s.setParam("mode", 1.f); s.setParam("depth", 60.f); s.setParam("rate_hz", 1.f); s.prepare({fs, 64, 1});
    double mn = 1e9, mx = -1e9; std::vector<float> blk(64, 0.0f);
    for (int k = 0; k < 48000 / 64 * 2; ++k) { processMono(s, blk, 64); mn = std::min(mn, s.relativeDelay(0)); mx = std::max(mx, s.relativeDelay(0)); }
    record("flanger_premium_tz_rel_min_ms", mn / 48, "ms"); record("flanger_premium_tz_rel_max_ms", mx / 48, "ms");
    CHECK(mn < 0 && mx > 0);
    // positive feedback: peaks at k/tau; negative: peaks at (k+0.5)/tau (tau = 1 ms)
    for (float fb : {80.f, -80.f}) {
        PremiumFlanger p; p.setParam("mode", 0.f); p.setParam("depth", 0.f); p.setParam("manual_ms", 1.f); p.setParam("feedback", fb); p.setParam("mix", 100.f);
        p.prepare({fs, 256, 1});
        const double atK = response(p, 2000.0), atHalf = response(p, 2500.0);
        record("flanger_premium_fb" + std::to_string((int)fb) + "_gain_2000_vs_2500_db", 20 * std::log10(atK / atHalf), "dB");
        CHECK(fb > 0 ? atK > 3 * atHalf : atHalf > 3 * atK);
    }
    // +/-95 % feedback with full sweep: bounded
    for (float fb : {95.f, -95.f}) {
        PremiumFlanger p; p.setParam("feedback", fb); p.setParam("depth", 100.f); p.setParam("rate_hz", 5.f); p.prepare({fs, 128, 2});
        auto l = makeNoise((size_t)fs * 4, 0.9, 4), r = l;
        stereoRun(p, l, r, 128);
        CHECK(allFinite(l) && analysis::peak(l.data(), l.size()) < 8.0);
    }
}

// ------------------------------------------------------------------------------------------ tremolo
TEST(audio, tremolo_premium_models_differ_rate_depth_crossover) {
    const double fs = 48000;
    auto run = [&](int model, float depth, float rate, double f, std::vector<double>& env, std::vector<double>& t) {
        PremiumTremolo tr; tr.setParam("model", (float)model); tr.setParam("depth", depth); tr.setParam("rate_hz", rate);
        tr.prepare({fs, 256, 1});
        auto y = processMono(tr, makeSine(fs, f, 0.3, (size_t)fs * 4));
        env = envelope(y, (size_t)fs, (size_t)fs * 4, 96, t, fs);
        return y;
    };
    for (int m = 0; m < 3; ++m) {
        std::vector<double> env, t;
        run(m, 100.f, 5.f, m == 2 ? 200.0 : 1000.0, env, t);
        const double rate = dominantHz(env, t, 12.0);
        double mx = 0, mn = 1e9; for (double e : env) { mx = std::max(mx, e); mn = std::min(mn, e); }
        record("tremolo_premium_rate_hz_model" + std::to_string(m), rate, "Hz");
        record("tremolo_premium_depth_db_model" + std::to_string(m), 20 * std::log10(mx / std::max(mn, 1e-9)), "dB");
        CHECK(std::fabs(rate - 5.0) < 0.05);
        // envelope asymmetry: time from min to max vs max to min (optical: fast rise, slow fall)
        size_t imin = 0, imax = 0;
        for (size_t i = env.size() / 2; i < env.size() && i < env.size() / 2 + 40; ++i) { if (env[i] < env[imin] || imin == 0) imin = i; if (env[i] > env[imax] || imax == 0) imax = i; }
        (void)imin; (void)imax;
    }
    // optical envelope shape: rising half (light on) faster than falling half
    {
        std::vector<double> env, t;
        run(0, 100.f, 2.f, 1000.0, env, t);
        double mx = 0, mn = 1e9; for (double e : env) { mx = std::max(mx, e); mn = std::min(mn, e); }
        int up = 0, dn = 0;
        for (size_t i = 1; i < env.size(); ++i) {
            const double a = (env[i - 1] - mn) / (mx - mn), b = (env[i] - mn) / (mx - mn);
            if (a < 0.9 && b > 0.1 && b > a) ++up;
            if (a > 0.1 && b < 0.9 && b < a) ++dn;
        }
        record("tremolo_optical_rise_frames", up, ""); record("tremolo_optical_fall_frames", dn, "");
        CHECK(up < dn);
    }
    // harmonic, depth 0: crossover sums flat (LR4 all-pass)
    double worst = 0;
    for (double f : {100.0, 400.0, 700.0, 1000.0, 3000.0}) {
        PremiumTremolo h; h.setParam("model", 2.f); h.setParam("depth", 0.f); h.prepare({fs, 256, 1});
        auto y = processMono(h, makeSine(fs, f, 0.3, (size_t)fs));
        worst = std::max(worst, std::fabs(20 * std::log10(ampAt(y, 24000, 8192, f, fs) / 0.3)));
    }
    record("tremolo_harmonic_crossover_flatness_db", worst, "dB");
    CHECK(worst < 0.1);
    // harmonic: low and high bands move in opposite phase
    {
        std::vector<double> el, th, eh;
        run(2, 100.f, 3.f, 120.0, el, th);
        std::vector<double> t2;
        run(2, 100.f, 3.f, 4000.0, eh, t2);
        double corr = 0, ml = 0, mh = 0; for (size_t i = 0; i < el.size(); ++i) { ml += el[i]; mh += eh[i]; }
        ml /= el.size(); mh /= eh.size();
        double sl = 0, sh = 0; for (size_t i = 0; i < el.size(); ++i) { corr += (el[i] - ml) * (eh[i] - mh); sl += (el[i] - ml) * (el[i] - ml); sh += (eh[i] - mh) * (eh[i] - mh); }
        record("tremolo_harmonic_band_env_corr", corr / std::sqrt(sl * sh), "");
        CHECK(corr / std::sqrt(sl * sh) < -0.8);
    }
    // THD: bias model adds harmonics, optical does not
    auto thd = [&](int model) {
        PremiumTremolo tr; tr.setParam("model", (float)model); tr.setParam("depth", 80.f); tr.setParam("rate_hz", 0.5f); tr.setParam("character", 100.f);
        tr.prepare({fs, 256, 1});
        auto y = processMono(tr, makeSine(fs, 1000, 0.5, (size_t)fs * 2));
        const double h1 = ampAt(y, 48000, 4096, 1000, fs), h2 = ampAt(y, 48000, 4096, 2000, fs), h3 = ampAt(y, 48000, 4096, 3000, fs);
        return 100 * std::hypot(h2, h3) / h1;
    };
    const double tOpt = thd(0), tBias = thd(1);
    record("tremolo_optical_thd_pct", tOpt, "%"); record("tremolo_bias_thd_pct", tBias, "%");
    CHECK(tBias > 5 * tOpt && tBias > 0.1);
    // square shape: no click (gain steps bounded)
    PremiumTremolo q; q.setParam("shape", 2.f); q.setParam("depth", 100.f); q.setParam("rate_hz", 10.f); q.setParam("model", 1.f);
    q.prepare({fs, 256, 1});
    std::vector<float> dc((size_t)fs, 0.5f);
    auto y = processMono(q, dc);
    record("tremolo_square_max_gain_step", maxStep(y, 1000, y.size()) / 0.5, "per sample");
    CHECK(maxStep(y, 1000, y.size()) / 0.5 < 0.02);
    // stereo pan mode: equal average level L/R
    PremiumTremolo p; p.setParam("stereo", 1.f); p.prepare({fs, 256, 2});
    auto l = makeSine(fs, 500, 0.3, (size_t)fs * 4), r = l;
    stereoRun(p, l, r);
    record("tremolo_pan_lr_balance_db", 20 * std::log10(rmsOf(l, 48000, l.size()) / rmsOf(r, 48000, r.size())), "dB");
    CHECK(std::fabs(20 * std::log10(rmsOf(l, 48000, l.size()) / rmsOf(r, 48000, r.size()))) < 0.2);
}

// ------------------------------------------------------------------------------------------ rotary
TEST(audio, rotary_premium_speeds_inertia_doppler_stereo) {
    const double fs = 48000;
    auto amRate = [&](int speed, double f, bool horn) {
        PremiumRotary r; r.setParam("speed", (float)speed); r.setParam("horn_db", horn ? 0.f : -24.f); r.setParam("drum_db", horn ? -24.f : 0.f);
        r.prepare({fs, 256, 1});
        auto y = processMono(r, makeSine(fs, f, 0.3, (size_t)fs * 6));
        std::vector<double> t;
        auto e = envelope(y, (size_t)fs, (size_t)fs * 6, 96, t, fs);
        return dominantHz(e, t, 12.0);
    };
    const double hs = amRate(0, 3000, true), hf = amRate(1, 3000, true), ds = amRate(0, 150, false), df = amRate(1, 150, false);
    record("rotary_horn_slow_hz", hs, "Hz"); record("rotary_horn_fast_hz", hf, "Hz"); record("rotary_drum_slow_hz", ds, "Hz"); record("rotary_drum_fast_hz", df, "Hz");
    CHECK(std::fabs(hs - PremiumRotary::kHornSlow) < 0.05 && std::fabs(hf - PremiumRotary::kHornFast) < 0.1);
    CHECK(std::fabs(ds - PremiumRotary::kDrumSlow) < 0.05 && std::fabs(df - PremiumRotary::kDrumFast) < 0.1);
    // inertia: slow -> fast, time to reach 63 % of the speed change
    PremiumRotary a; a.prepare({fs, 64, 1});
    std::vector<float> blk(64, 0.0f);
    processMono(a, blk, 64);
    a.setParam("speed", 1.f);
    double tH = -1, tD = -1;
    for (int k = 1; k < 48000 / 64 * 12; ++k) {
        processMono(a, blk, 64);
        const double t = k * 64 / fs;
        if (tH < 0 && a.hornHz() > PremiumRotary::kHornSlow + 0.632 * (PremiumRotary::kHornFast - PremiumRotary::kHornSlow)) tH = t;
        if (tD < 0 && a.drumHz() > PremiumRotary::kDrumSlow + 0.632 * (PremiumRotary::kDrumFast - PremiumRotary::kDrumSlow)) tD = t;
    }
    record("rotary_horn_spinup_63pct_s", tH, "s"); record("rotary_drum_spinup_63pct_s", tD, "s");
    CHECK(tH > 0.5 && tH < 0.9 && tD > 3.0 && tD < 4.0);
    // Doppler: horn at fast speed, peak deviation ~ 2 pi f_rot r / c
    PremiumRotary d; d.setParam("speed", 1.f); d.setParam("drum_db", -24.f); d.setParam("depth", 100.f); d.prepare({fs, 256, 1});
    auto y = processMono(d, makeSine(fs, 3000, 0.3, (size_t)fs * 4));
    std::vector<double> c, t; pitchTrack(y, (size_t)fs, (size_t)fs * 4, fs, 3000, c, t);
    double mx = -1e9, mn = 1e9; for (double e : c) { mx = std::max(mx, e); mn = std::min(mn, e); }
    const double expect = 1200 * std::log2(1 + kTwoPi * PremiumRotary::kHornFast * PremiumRotary::kHornRadius / PremiumRotary::kSoundSpeed);
    record("rotary_horn_doppler_peak_cents", 0.5 * (mx - mn), "cents"); record("rotary_horn_doppler_expected_cents", expect, "cents");
    CHECK(std::fabs(0.5 * (mx - mn) - expect) < 0.25 * expect);
    // stereo and mono compatibility (fast, noise)
    PremiumRotary s; s.setParam("speed", 1.f); s.prepare({fs, 256, 2});
    auto l = makeNoise((size_t)fs * 4, 0.3, 6), r = l;
    stereoRun(s, l, r);
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = (size_t)fs; i < l.size(); ++i) { sl += l[i] * l[i]; sr += r[i] * r[i]; slr += l[i] * r[i]; }
    record("rotary_lr_corr_fast", slr / std::sqrt(sl * sr), "");
    std::vector<float> mono(l.size()); for (size_t i = 0; i < l.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
    std::vector<double> lv; for (size_t i = (size_t)fs; i + 2400 <= mono.size(); i += 2400) lv.push_back(20 * std::log10(rmsOf(mono, i, i + 2400)));
    double lmx = -1e9, lmn = 1e9; for (double v : lv) { lmx = std::max(lmx, v); lmn = std::min(lmn, v); }
    record("rotary_mono_level_range_db", lmx - lmn, "dB");
    CHECK(slr / std::sqrt(sl * sr) < 0.9 && lmx - lmn < 6.0);
    // crossover: depth 0 gives a flat magnitude (LR4 sum)
    double worst = 0;
    for (double f : {100.0, 500.0, 800.0, 1200.0, 5000.0}) {
        PremiumRotary z; z.setParam("depth", 0.f); z.prepare({fs, 256, 1});
        auto o = processMono(z, makeSine(fs, f, 0.3, (size_t)fs));
        worst = std::max(worst, std::fabs(20 * std::log10(ampAt(o, 24000, 8192, f, fs) / 0.3)));
    }
    record("rotary_crossover_flatness_db", worst, "dB");
    CHECK(worst < 0.1);
    record("rotary_latency_samples_48k", s.latencySamples(), "samples");
}

// ------------------------------------------------------------------------------------------ safety
TEST(audio, premium_modulation_extremes_nan_silence_all_rates) {
    const char* ids[] = {"chorus_premium", "vibrato_premium", "phaser_premium", "univibe_premium", "flanger_premium", "tremolo_premium", "rotary_premium"};
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const char* id : ids) {
            auto fx = createEffect(id);
            for (int p = 0; p < fx->params().size(); ++p) {
                const auto& in = fx->params().info(p);
                if (in.unit != ParamUnit::Decibels) fx->params().set(p, (p % 2) ? in.max : in.min);
            }
            fx->prepare({fs, 17, 2});
            auto l = makeNoise((size_t)(fs * 0.5), 1.0, 9), r = l;
            l[100] = std::numeric_limits<float>::quiet_NaN(); r[200] = std::numeric_limits<float>::infinity();
            runEffect(*fx, l, &r, 17);
            CHECK_MSG(allFinite(l) && allFinite(r) && analysis::peak(l.data(), l.size()) < 16.0, id << " fs " << fs);
            // digital silence afterwards decays to exact zero
            fx->reset();
            std::vector<float> zl((size_t)fs, 0.0f), zr((size_t)fs, 0.0f);
            runEffect(*fx, zl, &zr, 128);
            CHECK_MSG(analysis::peak(zl.data(), zl.size()) == 0.0, id << " silence");
        }
}
