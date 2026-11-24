// Custom Drive verification (spec §17 "Custom Drive 추가 테스트").
#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/drive/CustomDrive.h"

using namespace mfx;
using namespace mfxtest;

namespace {
struct Settings { float drive = 25, headroom = 50, tone = 50, tight = 50, crunch = 50, grain = 50, mid = 50, mix = 100, level = 0; int os = 1; };

std::vector<float> render(const Settings& s, const std::vector<float>& x, double fs = 48000.0, int block = 128) {
    CustomDrive d;
    d.setParam("drive", s.drive); d.setParam("headroom", s.headroom); d.setParam("tone", s.tone);
    d.setParam("tight", s.tight); d.setParam("crunch", s.crunch); d.setParam("grain", s.grain);
    d.setParam("mid", s.mid); d.setParam("mix", s.mix); d.setParam("level", s.level);
    d.setParam("oversampling", (float)s.os);
    d.prepare({fs, 512, 1});
    return processMono(d, x, block);
}

double levelDb(const std::vector<float>& y) { return db(analysis::rms(y.data() + y.size() / 2, y.size() / 2)); }

analysis::HarmonicReport harm(const std::vector<float>& y, double f0, double fs = 48000.0) {
    return analysis::analyzeHarmonics(y.data() + y.size() / 2, std::min<size_t>(y.size() / 2, 32768), fs, f0);
}
} // namespace

TEST(audio, custom_drive_grid_is_finite_bounded_dc_free) {
    // Drive x Headroom x input level x frequency x oversampling
    int combos = 0;
    for (float drive : {0.f, 25.f, 50.f, 75.f, 100.f})
        for (float head : {0.f, 25.f, 50.f, 75.f, 100.f})
            for (double inDb : {-40.0, -20.0, -6.0})
                for (double f : {110.0, 1000.0, 3000.0})
                    for (int os : {0, 1, 2, 3}) {
                        if (os != 1 && !(drive == 100.f && head == 0.f) && !(drive == 50.f && head == 50.f)) continue; // OS sweep on 2 corner cases
                        Settings s; s.drive = drive; s.headroom = head; s.os = os; s.grain = 100;
                        auto y = render(s, makeSine(48000, f, dbToGainD(inDb), 24000));
                        CHECK(allFinite(y));
                        CHECK_MSG(analysis::peak(y.data(), y.size()) < 8.0, "peak");
                        // mean over an integer number of periods (otherwise the window itself creates "DC")
                        const size_t per = (size_t)std::llround(std::floor(10000.0 * f / 48000.0) * 48000.0 / f);
                        const double dc = std::fabs(analysis::mean(y.data() + 12000, per));
                        CHECK_MSG(dc < 2e-3, "dc " << dc << " d" << drive << " h" << head << " in" << inDb << " f" << f);
                        ++combos;
                    }
    record("custom_drive_grid_combinations", combos);
}

TEST(audio, custom_drive_thd_increases_with_drive) {
    double prev = -1.0;
    for (float drive : {0.f, 25.f, 50.f, 75.f, 100.f}) {
        Settings s; s.drive = drive;
        auto h = harm(render(s, makeSine(48000, 1000, dbToGainD(-20.0), 48000)), 1000.0);
        record("thd_percent_drive_" + std::to_string((int)drive), 100.0 * h.thd, "%");
        // strictly increasing until the stage is fully saturated, then a plateau (square-wave limit)
        if (drive <= 50.f) CHECK_MSG(h.thd > prev, "drive " << drive << " thd " << h.thd << " prev " << prev);
        else CHECK_MSG(h.thd > 0.98 * prev, "drive " << drive << " thd " << h.thd << " prev " << prev);
        prev = h.thd;
    }
}

TEST(audio, custom_drive_headroom_moves_saturation_onset_not_small_signal_gain) {
    // (a) small signal (-70 dBFS): headroom must not change the level (< 0.5 dB)
    std::vector<double> small;
    for (float head : {0.f, 50.f, 100.f}) {
        Settings s; s.drive = 50; s.headroom = head;
        small.push_back(levelDb(render(s, makeSine(48000, 1000, dbToGainD(-70.0), 24000))));
    }
    CHECK_MSG(std::fabs(small[0] - small[2]) < 0.5, "small-signal level changed by " << small[0] - small[2] << " dB");
    // (b) saturation onset: input level of 1 dB gain compression rises with headroom by ~24 dB
    auto onset = [](float head) {
        for (double in = -90.0; in <= 0.0; in += 1.0) {
            Settings s; s.drive = 50; s.headroom = head; s.tone = 50;
            const double gSmall = levelDb(render(s, makeSine(48000, 1000, dbToGainD(-90.0), 9600))) + 90.0;
            const double g = levelDb(render(s, makeSine(48000, 1000, dbToGainD(in), 9600))) - in;
            if (gSmall - g > 1.0) return in;
        }
        return 0.0;
    };
    const double o0 = onset(0.f), o100 = onset(100.f);
    record("onset_1dB_compression_headroom0", o0, "dBFS");
    record("onset_1dB_compression_headroom100", o100, "dBFS");
    CHECK_MSG(o100 - o0 > 18.0 && o100 - o0 < 30.0, "onset shift " << o100 - o0 << " dB");
    // (c) ceiling (very hot input) rises with headroom
    Settings lo; lo.drive = 100; lo.headroom = 0;
    Settings hi = lo; hi.headroom = 100;
    const auto x = makeSine(48000, 1000, 1.0, 24000);
    const double c0 = levelDb(render(lo, x)), c100 = levelDb(render(hi, x));
    record("ceiling_rise_headroom_0_to_100", c100 - c0, "dB");
    CHECK_MSG(c100 - c0 > 18.0, "ceiling change " << c100 - c0);
}

TEST(audio, custom_drive_drive_and_headroom_are_independent_axes) {
    // Drive: small-signal gain grows ~48 dB, ceiling nearly constant (< 3 dB)
    Settings a; a.drive = 0; a.headroom = 50;
    Settings b = a; b.drive = 100;
    const auto tiny = makeSine(48000, 1000, dbToGainD(-90.0), 24000);
    const auto hot = makeSine(48000, 1000, 1.0, 24000);
    const double dSmall = levelDb(render(b, tiny)) - levelDb(render(a, tiny));
    // compare ceilings at an input that saturates both settings
    Settings a2 = a; a2.drive = 60;
    const double dCeil = levelDb(render(b, hot)) - levelDb(render(a2, hot));
    record("drive_small_signal_gain_range", dSmall, "dB");
    record("drive_ceiling_change_60_to_100", dCeil, "dB");
    CHECK_NEAR(dSmall, 48.0, 1.0);
    CHECK_MSG(std::fabs(dCeil) < 3.0, "ceiling changed " << dCeil);
}

TEST(audio, custom_drive_soft_vs_hard_picking) {
    Settings s; s.drive = 40;
    const double thdSoft = harm(render(s, makeSine(48000, 220, dbToGainD(-40.0), 48000)), 220).thd;
    const double thdHard = harm(render(s, makeSine(48000, 220, dbToGainD(-6.0), 48000)), 220).thd;
    record("thd_soft_pick_-40dBFS", 100 * thdSoft, "%");
    record("thd_hard_pick_-6dBFS", 100 * thdHard, "%");
    CHECK(thdHard > 5.0 * thdSoft);
}

TEST(audio, custom_drive_grain_changes_texture_not_only_level) {
    // level-matched comparison: even/odd balance must move with Grain
    // moderate drive: the regime where clipping symmetry (not only saturation) shapes the spectrum
    Settings g0; g0.drive = 30; g0.grain = 0;
    Settings g100 = g0; g100.grain = 100;
    const auto x = makeSine(48000, 330, dbToGainD(-24.0), 48000);
    auto y0 = render(g0, x), y1 = render(g100, x);
    const double r0 = analysis::rms(y0.data() + 24000, 24000), r1 = analysis::rms(y1.data() + 24000, 24000);
    for (auto& v : y1) v *= (float)(r0 / r1);
    const auto h0 = harm(y0, 330), h1 = harm(y1, 330);
    record("even_odd_db_grain0", h0.evenOddRatioDb, "dB");
    record("even_odd_db_grain100", h1.evenOddRatioDb, "dB");
    CHECK(h1.evenOddRatioDb - h0.evenOddRatioDb > 10.0);
}

TEST(audio, custom_drive_crunch_hardens_knee) {
    // input level chosen to sit around the knee (u ~ Vth), where the knee shape is defined
    Settings soft; soft.drive = 25; soft.crunch = 0; soft.grain = 0;
    Settings hard = soft; hard.crunch = 100;
    const auto x = makeSine(48000, 500, dbToGainD(-24.0), 48000);
    const auto hs = harm(render(soft, x), 500), hh = harm(render(hard, x), 500);
    // harder knee -> relatively more high-order harmonics (7th..9th re 3rd)
    const double hoSoft = hs.harmonicsDb[7] - hs.harmonicsDb[3], hoHard = hh.harmonicsDb[7] - hh.harmonicsDb[3];
    record("h7_minus_h3_crunch0", hoSoft, "dB");
    record("h7_minus_h3_crunch100", hoHard, "dB");
    CHECK(hoHard > hoSoft + 3.0);
}

TEST(audio, custom_drive_tight_mid_tone_small_signal_responses) {
    auto gainAt = [](Settings s, double f) { return levelDb(render(s, makeSine(48000, f, dbToGainD(-80.0), 24000))) + 80.0; };
    Settings t0; t0.tight = 0; Settings t1; t1.tight = 100;
    CHECK(gainAt(t0, 100) - gainAt(t1, 100) > 15.0);              // tight removes bass before clipping
    Settings m0; m0.mid = 0; Settings m1; m1.mid = 100;
    CHECK((gainAt(m1, 800) - gainAt(m1, 4000)) - (gainAt(m0, 800) - gainAt(m0, 4000)) > 12.0);
    Settings d; d.tone = 0; Settings b; b.tone = 100;
    CHECK((gainAt(b, 5000) - gainAt(b, 200)) - (gainAt(d, 5000) - gainAt(d, 200)) > 12.0);
}

TEST(audio, custom_drive_oversampling_reduces_aliasing) {
    Settings s1; s1.drive = 90; s1.crunch = 90; s1.os = 0;
    Settings s8 = s1; s8.os = 3;
    const auto x = makeSine(48000, 3700, dbToGainD(-6.0), 65536);
    const auto h1 = harm(render(s1, x), 3700), h8 = harm(render(s8, x), 3700);
    record("non_harmonic_db_os1x", h1.nonHarmonicDb, "dB");
    record("non_harmonic_db_os8x", h8.nonHarmonicDb, "dB");
    CHECK_MSG(h1.nonHarmonicDb - h8.nonHarmonicDb > 15.0, "1x " << h1.nonHarmonicDb << " 8x " << h8.nonHarmonicDb);
}

TEST(audio, custom_drive_latency_matches_dry_alignment) {
    for (int os : {0, 1, 2, 3}) {
        Settings s; s.mix = 0; s.os = os; // pure dry path
        CustomDrive d; d.setParam("mix", 0); d.setParam("level", 0); d.setParam("oversampling", (float)os);
        d.prepare({48000, 256, 1});
        auto y = processMono(d, makeImpulse(512));
        size_t arg = 0;
        for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[arg])) arg = i;
        CHECK((int)arg == d.latencySamples());
        CHECK_NEAR(y[arg], 1.0, 1e-6);
    }
}

TEST(audio, custom_drive_parameter_changes_are_smooth_and_reach_dsp) {
    CustomDrive d;
    d.prepare({48000.0, 128, 1});
    CHECK_NEAR(d.physical().gainDb, 12.0, 1e-6); // default drive 25 -> 12 dB
    auto x = makeSine(48000, 200, 0.2, 48000);
    std::vector<float> y = x;
    double maxJump = 0.0, steadyJump = 0.0;
    for (size_t pos = 0; pos < y.size(); pos += 128) {
        if (pos == 24064) { d.setParam("drive", 100.0f); d.setParam("headroom", 0.0f); d.setParam("level", 6.0f); }
        AudioBlock b; b.numChannels = 1; b.numSamples = 128; b.ch[0] = y.data() + pos;
        d.process(b);
    }
    // Click detector: second difference (curvature) is large for discontinuities but bounded for
    // band-limited waveforms. The transition must not exceed the worse of the two steady states
    // (intermediate settings legitimately have their own waveform; a click would be far above both).
    // Calibration: smoothed change = 1.48, unsmoothed state reset/jump = 5.5 -> threshold 2.5.
    auto d2 = [&](size_t a, size_t b) {
        double m = 0.0;
        for (size_t i = a; i < b; ++i) m = std::max(m, (double)std::fabs(y[i] - 2.0f * y[i - 1] + y[i - 2]));
        return m;
    };
    steadyJump = std::max(d2(12000, 24000), d2(36000, 48000));
    maxJump = d2(24064, 27000);
    CHECK_NEAR(d.physical().gainDb, 48.0, 1e-4); // value propagated to DSP after smoothing
    record("transition_curvature_ratio", maxJump / steadyJump);
    CHECK_MSG(maxJump < 2.5 * steadyJump, "curvature " << maxJump << " steady " << steadyJump);
}

TEST(audio, custom_drive_runtime_oversampling_switch_is_safe) {
    CustomDrive d;
    d.prepare({48000.0, 128, 1});
    auto y = makeSine(48000, 440, 0.5, 48000);
    for (size_t pos = 0, k = 0; pos < y.size(); pos += 128, ++k) {
        if (k % 40 == 0) d.setParam("oversampling", (float)((k / 40) % 4));
        AudioBlock b; b.numChannels = 1; b.numSamples = 128; b.ch[0] = y.data() + pos;
        d.process(b);
    }
    CHECK(allFinite(y));
    CHECK(analysis::peak(y.data(), y.size()) < 4.0);
}
