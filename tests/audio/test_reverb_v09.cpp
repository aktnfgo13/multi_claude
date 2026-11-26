// v0.9 Premium Hall / Spring / Shimmer / Freeze / Ducking - measurements on synthetic signals.
#include <cmath>
#include <complex>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/filters/Biquad.h"
#include "dsp/reverb/PremiumSpace.h"
#include "engine/EffectFactory.h"

using namespace mfx;
using namespace mfxtest;

namespace {
struct St { std::vector<float> l, r; };
St run(Effect& fx, std::vector<float> l, std::vector<float> r, int block = 256) { runEffect(fx, l, &r, block); return {l, r}; }
St impulseRun(Effect& fx, double fs, double secs) {
    std::vector<float> x((size_t)(fs * secs), 0.0f); x[10] = 1.0f;
    fx.prepare({fs, 256, 2});
    return run(fx, x, x);
}
double rt60(const std::vector<float>& y, double fs) { // Schroeder EDC, T20 (-5..-25 dB)
    std::vector<double> e(y.size()); double acc = 0;
    for (size_t i = y.size(); i-- > 0;) { acc += (double)y[i] * y[i]; e[i] = acc; }
    size_t a = 0, b = 0;
    for (size_t i = 0; i < e.size(); ++i) { const double d = 10 * std::log10(e[i] / e[0] + 1e-30); if (!a && d < -5) a = i; if (d < -25) { b = i; break; } }
    if (b <= a) return NAN;
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = (double)(b - a);
    for (size_t i = a; i < b; ++i) { const double x = i / fs, d = 10 * std::log10(e[i] / e[0] + 1e-30); sx += x; sy += d; sxx += x * x; sxy += x * d; }
    return -60.0 / ((n * sxy - sx * sy) / (n * sxx - sx * sx));
}
std::vector<float> band(const std::vector<float>& x, double fs, double fc, double q = 1.4) {
    Biquad a, b; a.design(FilterType::BandPass, fs, fc, q); b.design(FilterType::BandPass, fs, fc, q);
    std::vector<float> y(x.size());
    for (size_t i = 0; i < x.size(); ++i) y[i] = b.process(a.process(x[i], 0), 0);
    return y;
}
double rmsDb(const std::vector<float>& y, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b && i < y.size(); ++i) s += (double)y[i] * y[i];
    return 10 * std::log10(s / (double)(b - a) + 1e-30);
}
double corr(const std::vector<float>& l, const std::vector<float>& r, size_t a, size_t b) {
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = a; i < b; ++i) { sl += l[i] * l[i]; sr += r[i] * r[i]; slr += l[i] * r[i]; }
    return slr / std::sqrt(sl * sr + 1e-30);
}
double ampAt(const std::vector<float>& y, size_t a, size_t n, double f, double fs) {
    std::complex<double> acc = 0; double w = 0;
    for (size_t i = 0; i < n; ++i) {
        const double h = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
        acc += h * (double)y[a + i] * std::exp(std::complex<double>(0, -2 * kPi * f * i / fs)); w += h;
    }
    return 2.0 * std::abs(acc) / w;
}
double maxStep(const std::vector<float>& y, size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < b; ++i) m = std::max(m, (double)std::fabs(y[i] - y[i - 1])); return m; }
} // namespace

// ================================================================================================ Hall
TEST(audio, hall_premium_rt60_per_band_tracks_settings) {
    const double fs = 48000;
    for (float T : {1.f, 3.f, 8.f}) {
        PremiumHall h; h.setParam("decay_s", T); h.setParam("mix", 100.f); h.setParam("early", 0.f); h.setParam("predelay_ms", 0.f);
        auto s = impulseRun(h, fs, T * 2.5 + 1);
        const double mid = rt60(band(s.l, fs, 1000), fs);
        record("hall_premium_rt60_1k_set" + std::to_string((int)T), mid, "s");
        CHECK_MSG(std::fabs(mid / T - 1) < 0.15, "T " << T << " 1 kHz RT60 " << mid);
    }
    PremiumHall h; h.setParam("decay_s", 2.f); h.setParam("bass_mult", 2.f); h.setParam("treble_mult", 0.3f);
    h.setParam("mix", 100.f); h.setParam("early", 0.f); h.setParam("predelay_ms", 0.f);
    auto s = impulseRun(h, fs, 7);
    const double lo = rt60(band(s.l, fs, 80, 1.0), fs), mid = rt60(band(s.l, fs, 1000), fs), hi = rt60(band(s.l, fs, 10000), fs);
    record("hall_premium_rt60_80hz_bass2", lo, "s"); record("hall_premium_rt60_1k", mid, "s"); record("hall_premium_rt60_10k_treble03", hi, "s");
    CHECK(std::fabs(lo / 4.0 - 1) < 0.25 && std::fabs(hi / 0.6 - 1) < 0.35);
}

TEST(audio, hall_premium_early_reflections_stereo_and_size) {
    const double fs = 48000;
    PremiumHall h; h.setParam("mix", 100.f); h.setParam("predelay_ms", 10.f); h.setParam("early", 100.f); h.setParam("size", 50.f);
    auto s = impulseRun(h, fs, 0.2);
    // first early reflection: pre-delay + 7.1 ms * size factor (0.4 + 1.2 * 0.5 = 1.0)
    size_t first = 0; for (size_t i = 10; i < s.l.size(); ++i) if (std::fabs(s.l[i]) > 0.02) { first = i; break; }
    record("hall_premium_first_er_ms", (first - 10) / 48.0, "ms");
    CHECK(std::fabs((first - 10) / 48.0 - 17.1) < 1.0);
    CHECK(corr(s.l, s.r, 0, 9600) < 0.5); // distinct L/R reflection patterns
    // late tail: decorrelated, no anti-phase (mono sum keeps about half the power)
    PremiumHall t; t.setParam("mix", 100.f); auto u = impulseRun(t, fs, 4);
    const double c = corr(u.l, u.r, (size_t)(0.3 * fs), (size_t)(2.0 * fs));
    double pm = 0, ps = 0; for (size_t i = (size_t)(0.3 * fs); i < (size_t)(2.0 * fs); ++i) { const double m = 0.5 * (u.l[i] + u.r[i]); pm += m * m; ps += 0.5 * (u.l[i] * u.l[i] + u.r[i] * u.r[i]); }
    record("hall_premium_tail_lr_corr", c, ""); record("hall_premium_mono_power_ratio", pm / ps, "");
    CHECK(std::fabs(c) < 0.3 && pm / ps > 0.3);
    // size change while running (0 <-> 100 every 0.5 s) with a 330 Hz tone: a click shows up as broadband
    // energy; compare the loudest 10 ms of > 3 kHz content against the same render at a fixed size
    auto sizeRun = [&](bool toggle) {
        PremiumHall z; z.setParam("mix", 100.f); z.prepare({fs, 64, 2});
        auto l = makeSine(fs, 330, 0.3, (size_t)fs * 4), r = l;
        for (size_t p = 0; p < l.size(); p += 64) {
            if (toggle && p % 24000 == 0) z.setParam("size", (p / 24000) % 2 ? 100.f : 0.f);
            AudioBlock b; b.numChannels = 2; b.numSamples = 64; b.ch[0] = l.data() + p; b.ch[1] = r.data() + p; z.process(b);
        }
        Biquad h1, h2; h1.design(FilterType::HighPass, fs, 3000, 0.7071); h2.design(FilterType::HighPass, fs, 3000, 0.7071);
        for (auto& v : l) v = h2.process(h1.process(v, 0), 0);
        double worst = -300;
        for (size_t a = 48000; a + 480 < l.size(); a += 240) worst = std::max(worst, rmsDb(l, a, a + 480));
        return worst;
    };
    const double hfToggle = sizeRun(true), hfFixed = sizeRun(false);
    record("hall_premium_size_change_hf_db", hfToggle, "dBFS"); record("hall_premium_size_fixed_hf_db", hfFixed, "dBFS");
    CHECK(hfToggle < -70.0); // fixed-size value is just the 330 Hz leakage through the 3 kHz high-pass
}

TEST(audio, hall_premium_modulation_pitch_stability_and_noise_floor) {
    const double fs = 48000;
    // sustained 1 kHz through the hall (wet only, max modulation): energy stays within +/-4 Hz of the tone
    PremiumHall h; h.setParam("mix", 100.f); h.setParam("mod_depth", 100.f); h.setParam("mod_rate_hz", 2.f);
    h.prepare({fs, 256, 2});
    auto x = makeSine(fs, 1000, 0.2, (size_t)fs * 10);
    auto s = run(h, x, x);
    const size_t a = (size_t)fs * 4, n = 1 << 18;
    std::vector<double> P;
    double inBand = 0, total = 0;
    for (double f = 950; f <= 1050; f += 0.5) { const double v = std::pow(ampAt(s.l, a, n, f, fs), 2); total += v; if (std::fabs(f - 1000) <= 4) inBand += v; }
    record("hall_premium_mod_tone_energy_within_4hz", inBand / total, "");
    CHECK(inBand / total > 0.9);
    // digital silence in -> exact silence out; long after an impulse the tail is gone (no denormal / limit cycle)
    PremiumHall z; z.setParam("mix", 100.f); z.setParam("decay_s", 2.f);
    auto t = impulseRun(z, fs, 40);
    record("hall_premium_floor_after_38s_dbfs", rmsDb(t.l, (size_t)(38 * fs), (size_t)(40 * fs)), "dBFS");
    CHECK(rmsDb(t.l, (size_t)(38 * fs), (size_t)(40 * fs)) < -200);
    record("hall_premium_memory_kib_48k", (double)z.memoryBytes() / 1024, "KiB");
    PremiumHall m; m.prepare({192000, 64, 2}); record("hall_premium_memory_kib_192k", (double)m.memoryBytes() / 1024, "KiB");
}

// ================================================================================================ Spring
TEST(audio, spring_premium_dispersion_decay_and_drive) {
    const double fs = 48000;
    PremiumSpring sp; sp.setParam("mix", 100.f); sp.setParam("drive", 0.f); sp.setParam("width", 0.f);
    auto s = impulseRun(sp, fs, 3);
    // first arrival (direct pass through the dispersive chain): group delay grows with frequency below fC
    auto arrival = [&](double f) {
        auto y = band(s.l, fs, f, 3.0);
        size_t k = 0; float m = 0;
        for (size_t i = 0; i < (size_t)(0.06 * fs); ++i) if (std::fabs(y[i]) > m) { m = std::fabs(y[i]); k = i; }
        return (double)k / fs * 1000.0;
    };
    const double a300 = arrival(300), a1k = arrival(1000), a3k = arrival(3000);
    record("spring_arrival_ms_300hz", a300, "ms"); record("spring_arrival_ms_1k", a1k, "ms"); record("spring_arrival_ms_3k", a3k, "ms");
    record("spring_transition_hz", sp.transitionHz(0), "Hz");
    CHECK(a300 < a1k && a1k < a3k && a3k - a300 > 5.0);
    // RT60 (low band) follows Decay
    for (float T : {1.5f, 4.f}) {
        PremiumSpring d; d.setParam("decay_s", T); d.setParam("mix", 100.f);
        auto u = impulseRun(d, fs, T * 2.5 + 1);
        const double rt = rt60(band(u.l, fs, 500, 1.0), fs);
        record("spring_rt60_500hz_set" + std::to_string((int)(T * 10)), rt, "s");
        CHECK_MSG(std::fabs(rt / T - 1) < 0.3, "T " << T << " rt " << rt);
    }
    // width 0 -> mono (physical tank structure), width 100 -> two tanks
    PremiumSpring m; m.setParam("width", 0.f); m.setParam("mix", 100.f);
    auto mm = impulseRun(m, fs, 1);
    double e = 0; for (size_t i = 0; i < mm.l.size(); ++i) e = std::max(e, (double)std::fabs(mm.l[i] - mm.r[i]));
    CHECK(e < 1e-6);
    // drive: 3rd-harmonic band energy relative to the fundamental band (0.25 peak sine, a typical guitar level)
    auto h3 = [&](float drive) {
        PremiumSpring d; d.setParam("drive", drive); d.setParam("mix", 100.f); d.setParam("decay_s", 1.f);
        d.prepare({fs, 256, 2});
        auto x = makeSine(fs, 200, 0.25, (size_t)fs * 3);
        auto o = run(d, x, x);
        return rmsDb(band(o.l, fs, 600, 4.0), (size_t)fs * 2, (size_t)fs * 3) - rmsDb(band(o.l, fs, 200, 4.0), (size_t)fs * 2, (size_t)fs * 3);
    };
    const double d0 = h3(0.f), d30 = h3(30.f), d100 = h3(100.f);
    record("spring_h3_rel_db_drive0", d0, "dB"); record("spring_h3_rel_db_drive30", d30, "dB"); record("spring_h3_rel_db_drive100", d100, "dB");
    CHECK(d100 > d0 + 10.0 && d30 < -20.0);
}

// ================================================================================================ Shimmer
TEST(audio, shimmer_pitch_engine_accuracy_artifacts_latency) {
    const double fs = 48000;
    for (double semis : {12.0, 7.0, 19.0, -12.0}) {
        PitchShifter p; p.prepare(fs, 50.0); p.setRatio(std::pow(2.0, semis / 12.0)); p.reset();
        auto x = makeSine(fs, 440, 0.3, (size_t)fs * 4); // 1 s settle + 2^17 analysis samples
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) { y[i] = p.process(x[i], 0); p.advance(); }
        const double f0 = 440 * std::pow(2.0, semis / 12.0);
        double best = 0, bf = 0;
        for (double f = f0 * 0.99; f < f0 * 1.01; f += 0.02) { const double v = ampAt(y, (size_t)fs, 1 << 17, f, fs); if (v > best) { best = v; bf = f; } }
        const double cents = 1200 * std::log2(bf / f0);
        record("shimmer_shift_cents_err_" + std::to_string((int)semis), cents, "cents");
        CHECK(std::fabs(cents) < 1.0);
        if (semis == 12.0) {
            // window artefacts: amplitude ripple of the shifted sine and energy away from the target tone
            double mx = 0, mn = 1e9;
            for (size_t k = (size_t)fs; k + 480 < y.size(); k += 480) { double s = 0; for (size_t i = k; i < k + 480; ++i) s += y[i] * y[i]; s = std::sqrt(s / 480); mx = std::max(mx, s); mn = std::min(mn, s); }
            record("shimmer_shift_am_ripple_db", 20 * std::log10(mx / mn), "dB");
            record("shimmer_shift_latency_ms", p.latencySamples() / 48.0, "ms");
        }
    }
}

TEST(audio, shimmer_feedback_accumulates_octaves_pre_does_not_and_stays_bounded) {
    const double fs = 48000;
    auto octaves = [&](int structure, double* e2, double* e4) { // band energies at 3.5-4 s relative to the fundamental
        PremiumShimmer s; s.setParam("mix", 100.f); s.setParam("structure", (float)structure); s.setParam("shimmer", 100.f);
        s.setParam("decay_s", 10.f); s.setParam("tone_hz", 12000.f); s.setParam("low_cut_hz", 20.f); s.setParam("predelay_ms", 0.f);
        s.prepare({fs, 256, 2});
        auto x = makeSine(fs, 330, 0.3, (size_t)fs * 6);
        for (size_t i = (size_t)fs; i < x.size(); ++i) x[i] = 0.0f;
        auto o = run(s, x, x);
        const size_t a = (size_t)(3.5 * fs), z = (size_t)(4.0 * fs);
        const double e1 = rmsDb(band(o.l, fs, 330, 4.0), a, z);
        *e2 = rmsDb(band(o.l, fs, 660, 4.0), a, z) - e1;
        *e4 = rmsDb(band(o.l, fs, 1320, 4.0), a, z) - e1;
    };
    double f2, f4, p2, p4;
    octaves(0, &f2, &f4); octaves(1, &p2, &p4);
    record("shimmer_fb_octave1_re_fund_db", f2, "dB"); record("shimmer_fb_octave2_re_fund_db", f4, "dB");
    record("shimmer_pre_octave1_re_fund_db", p2, "dB"); record("shimmer_pre_octave2_re_fund_db", p4, "dB");
    CHECK(f4 > p4 + 10.0); // second octave only builds up in the feedback structure
    // long run: 20 s of noise at full shimmer / max decay, then 40 s of silence: bounded, finite, decaying
    PremiumShimmer s; s.setParam("shimmer", 100.f); s.setParam("decay_s", 30.f); s.setParam("mix", 100.f); s.setParam("tone_hz", 12000.f);
    s.prepare({fs, 512, 2});
    auto x = makeNoise((size_t)fs * 60, 0.5, 3);
    for (size_t i = (size_t)fs * 20; i < x.size(); ++i) x[i] = 0.0f;
    auto o = run(s, x, x, 512);
    const double pk = std::max(analysis::peak(o.l.data(), o.l.size()), analysis::peak(o.r.data(), o.r.size()));
    record("shimmer_long_peak", pk, "");
    record("shimmer_memory_kib_48k", (double)s.memoryBytes() / 1024, "KiB"); record("shimmer_rms_db_10_20s", rmsDb(o.l, (size_t)fs * 10, (size_t)fs * 20), "dBFS");
    record("shimmer_rms_db_55_60s", rmsDb(o.l, (size_t)fs * 55, (size_t)fs * 60), "dBFS");
    record("shimmer_tail_lr_corr", corr(o.l, o.r, (size_t)fs * 21, (size_t)fs * 30), "");
    CHECK(allFinite(o.l) && allFinite(o.r) && pk < 2.0);
    CHECK(rmsDb(o.l, (size_t)fs * 55, (size_t)fs * 60) < rmsDb(o.l, (size_t)fs * 10, (size_t)fs * 20) - 6.0);
}

// ================================================================================================ Freeze
TEST(audio, freeze_captures_holds_and_releases) {
    const double fs = 48000;
    for (int which = 0; which < 2; ++which) {
        std::unique_ptr<Effect> fx;
        if (which == 0) fx = std::make_unique<PremiumHall>(); else fx = std::make_unique<PremiumShimmer>();
        fx->setParam("mix", 50.f); fx->setParam("decay_s", 2.f);
        if (which == 1) fx->setParam("shimmer", 60.f);
        fx->prepare({fs, 256, 2});
        const size_t n = (size_t)(fs * 70);
        std::vector<float> l(n, 0.0f), r(n, 0.0f);
        auto nz = makeNoise((size_t)(fs * 1.5), 0.3, 5);
        for (size_t i = 0; i < nz.size(); ++i) { l[i] = nz[i]; r[i] = nz[i]; }
        auto tone = makeSine(fs, 3000, 0.3, (size_t)fs * 5);
        for (size_t i = 0; i < tone.size(); ++i) { l[(size_t)(10 * fs) + i] = tone[i]; r[(size_t)(10 * fs) + i] = tone[i]; } // playing over the freeze
        for (size_t p = 0; p < n; p += 256) {
            if (p == (size_t)(1.4 * fs) / 256 * 256) fx->setParam("freeze", 1.f);   // while the noise still plays
            if (p == (size_t)(60 * fs) / 256 * 256) fx->setParam("freeze", 0.f);
            AudioBlock b; b.numChannels = 2; b.numSamples = (int)std::min<size_t>(256, n - p); b.ch[0] = l.data() + p; b.ch[1] = r.data() + p;
            fx->process(b);
        }
        const std::string id = which == 0 ? "hall" : "shimmer";
        const double r1 = rmsDb(l, (size_t)(3 * fs), (size_t)(8 * fs)), r2 = rmsDb(l, (size_t)(50 * fs), (size_t)(58 * fs));
        record("freeze_" + id + "_rms_3_8s_db", r1, "dBFS"); record("freeze_" + id + "_rms_50_58s_db", r2, "dBFS");
        record("freeze_" + id + "_drift_db_per_min", (r2 - r1) / (52.0 / 60.0), "dB/min");
        CHECK_MSG(std::fabs(r2 - r1) < 3.0, id << " drift " << r2 - r1);
        // capture continuity: no jump at the freeze switch
        const double around = maxStep(l, (size_t)(1.3 * fs), (size_t)(1.7 * fs)), steady = maxStep(l, (size_t)(1.0 * fs), (size_t)(1.3 * fs));
        record("freeze_" + id + "_switch_step_ratio", around / steady, "");
        CHECK(around < 1.5 * steady);
        // the frozen layer does not take in new playing: after the 3 kHz phrase ends, no 3 kHz remains
        const double t3 = ampAt(l, (size_t)(20 * fs), 1 << 16, 3000, fs);
        record("freeze_" + id + "_3k_after_phrase", 20 * std::log10(t3 + 1e-12), "dB");
        CHECK(t3 < 0.003);
        // the new phrase itself is heard (dry path untouched)
        CHECK(ampAt(l, (size_t)(12 * fs), 1 << 16, 3000, fs) > 0.25);
        // stereo kept
        record("freeze_" + id + "_lr_corr", corr(l, r, (size_t)(30 * fs), (size_t)(40 * fs)), "");
        // release: decays naturally (> 30 dB within 6 s)
        const double rel = rmsDb(l, (size_t)(66 * fs), (size_t)(68 * fs));
        record("freeze_" + id + "_after_release_db", rel, "dBFS");
        CHECK(rel < r2 - 30.0);
        CHECK(maxStep(l, (size_t)(59.5 * fs), (size_t)(61 * fs)) < 1.5 * steady);
    }
}

// ================================================================================================ Ducking
TEST(audio, ducking_is_wet_only_recovers_and_keeps_tails) {
    const double fs = 48000;
    auto render = [&](float duck, float mix) {
        PremiumHall h; h.setParam("duck_db", duck); h.setParam("duck_threshold_db", -30.f); h.setParam("mix", mix); h.prepare({fs, 256, 2});
        auto x = makeSine(fs, 440, 0.3, (size_t)fs * 6);
        for (size_t i = (size_t)(1.5 * fs); i < x.size(); ++i) x[i] = 0.0f;
        return run(h, x, x);
    };
    auto a = render(12.f, 50.f), b = render(0.f, 50.f), wa = render(12.f, 100.f), wb = render(0.f, 100.f);
    // dry untouched: (mix 50 ducked) - (mix 50 plain) == (wet ducked) - (wet plain)   (both wet gains are 1 at mix 50 / 100)
    double e = 0;
    for (size_t i = 0; i < a.l.size(); ++i) e = std::max(e, (double)std::fabs((a.l[i] - b.l[i]) - (wa.l[i] - wb.l[i])));
    record("ducking_dry_path_max_err", e, "");
    CHECK(e < 1e-5);
    // wet reduction while playing, recovery and tail after
    const double during = rmsDb(wa.l, (size_t)(0.8 * fs), (size_t)(1.4 * fs)) - rmsDb(wb.l, (size_t)(0.8 * fs), (size_t)(1.4 * fs));
    const double tail = rmsDb(wa.l, (size_t)(3.0 * fs), (size_t)(4.0 * fs)) - rmsDb(wb.l, (size_t)(3.0 * fs), (size_t)(4.0 * fs));
    record("ducking_wet_reduction_db_playing", during, "dB"); record("ducking_tail_change_db_after_1p5s", tail, "dB");
    CHECK(during < -9.0 && tail > -1.0);
    // smooth gain: per-sample wet-gain change bounded (no click)
    PremiumHall h; h.setParam("duck_db", 24.f); h.prepare({fs, 1, 2});
    std::vector<float> one(1), two(1);
    double mxd = 0, prev = 1;
    for (size_t i = 0; i < (size_t)(2 * fs); ++i) {
        one[0] = two[0] = (i > 4800 && i < 48000) ? 0.5f : 0.0f;
        runEffect(h, one, &two, 1);
        mxd = std::max(mxd, std::fabs(h.duckGain() - prev)); prev = h.duckGain();
    }
    record("ducking_max_gain_step", mxd, "per sample");
    CHECK(mxd < 0.002);
}

// ================================================================================================ Extremes
TEST(audio, premium_reverb_extremes_rates_blocks_nan_silence) {
    for (const char* type : {"reverb_hall_premium", "reverb_spring_premium", "reverb_shimmer"}) {
        for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
            for (int block : {1, 17, 128, 512}) {
                if (fs == 192000.0 && block == 1) continue; // covered by 17 (runtime)
                auto fx = createEffect(type);
                for (int i = 0; i < fx->params().size(); ++i) fx->params().set(i, fx->params().info(i).max); // all max (freeze on)
                fx->setParam("level_db", 0.f); // output trim excluded from the bound
                fx->prepare({fs, block, 2});
                auto l = makeNoise((size_t)(fs * 0.5), 0.9, 7), r = l;
                l[100] = NAN; r[200] = INFINITY;
                runEffect(*fx, l, &r, block);
                CHECK_MSG(allFinite(l) && allFinite(r), type << " fs " << fs << " block " << block);
                CHECK(analysis::peak(l.data(), l.size()) < 4.0);
                // silence after reset -> silence
                fx->reset();
                std::vector<float> z((size_t)(fs * 0.1), 0.0f), zr = z;
                runEffect(*fx, z, &zr, block);
                CHECK(analysis::peak(z.data(), z.size()) == 0.0);
            }
        }
    }
}
