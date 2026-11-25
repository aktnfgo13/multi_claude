// AB763 Normal amp (M1) and Custom Drive model selection (v1 / v2 / v2 + ADAA).
#include <cmath>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/amp/AB763Amp.h"
#include "dsp/analysis/Analysis.h"
#include "dsp/drive/CustomDrive.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"
#include "presets/Preset.h"

using namespace mfx;
using namespace mfxtest;

namespace {
analysis::HarmonicReport sineReport(Effect& fx, double fs, double f0, double dbfs, double secs = 1.0) {
    fx.prepare({fs, 256, 1});
    auto x = makeSine(fs, f0, std::pow(10.0, dbfs / 20.0), (size_t)(fs * secs));
    auto y = processMono(fx, x, 256);
    const size_t skip = y.size() / 2;
    return analysis::analyzeHarmonics(y.data() + skip, y.size() - skip, fs, f0);
}
} // namespace

TEST(audio, ab763_tone_stack_matches_analog_prototype_and_scoops_mids) {
    const BlackfaceToneStack::Values v;
    const double fs = 96000.0;
    for (double treble : {0.0, 0.6, 1.0}) {
        for (double bassKnob : {0.0, 4.0, 10.0}) {
            const double l = AB763Amp::bassTaper(bassKnob);
            BlackfaceToneStack ts;
            ts.design(v, treble, l, fs);
            double worst = 0.0;
            for (double f : {40.0, 100.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0}) {
                const double a = db(BlackfaceToneStack::analogMagnitude(v, treble, l, f));
                const double d = db(ts.digitalMagnitude(f, fs));
                worst = std::max(worst, std::fabs(a - d));
            }
            CHECK_MSG(worst < 0.35, "treble " << treble << " bass " << bassKnob << ": digital vs analog " << worst << " dB");
        }
    }
    // default knobs (treble 6, bass 4): blackface mid scoop
    const double l = AB763Amp::bassTaper(4.0);
    const double h100 = db(BlackfaceToneStack::analogMagnitude(v, 0.6, l, 100.0));
    const double h500 = db(BlackfaceToneStack::analogMagnitude(v, 0.6, l, 500.0));
    const double h3k = db(BlackfaceToneStack::analogMagnitude(v, 0.6, l, 3000.0));
    record("ab763_stack_100_500_3k_db", h500, "dB");
    CHECK(h500 < h100 && h500 < h3k);
}

TEST(audio, ab763_triode_stage_operating_point_and_gain) {
    TriodeStageTable t;
    t.build({250.0, 100e3, 1.5e3, 68e3});
    record("ab763_v1_plate_quiescent", t.quiescentPlate(), "V");
    record("ab763_v1_cathode_quiescent", t.quiescentCathode(), "V");
    CHECK(t.quiescentPlate() > 120.0 && t.quiescentPlate() < 220.0);
    CHECK(t.quiescentCathode() > 0.5 && t.quiescentCathode() < 2.5);
    const double g = (t.process(0.01f) - t.process(-0.01f)) / 0.02;
    record("ab763_v1_small_signal_gain", g, "V/V");
    CHECK(g < -30.0 && g > -80.0); // inverting, 12AX7 with bypassed cathode
    // asymmetric clipping: positive grid swing limited by grid conduction
    CHECK(std::fabs(t.process(3.0f)) < std::fabs(t.process(-3.0f)) * 1.5f);
}

TEST(audio, ab763_volume_controls_breakup) {
    double thd[3];
    const float vols[3] = {2.f, 6.f, 10.f};
    for (int i = 0; i < 3; ++i) {
        AB763Amp a;
        a.setParam("model", 0.f); // M1
        a.setParam("volume", vols[i]);
        thd[i] = sineReport(a, 48000, 220, -12.0).thd;
    }
    record("ab763_thd_vol2", thd[0] * 100, "%");
    record("ab763_thd_vol10", thd[2] * 100, "%");
    CHECK(thd[0] < 0.03);
    CHECK(thd[0] < thd[1] && thd[1] < thd[2]);
    CHECK(thd[2] > 0.10);
}

TEST(audio, ab763_sample_rate_independent) {
  for (float model : {0.f, 1.f}) {
    double g[3], thd[3];
    const double rates[3] = {44100.0, 48000.0, 96000.0};
    for (int i = 0; i < 3; ++i) {
        AB763Amp a; a.setParam("volume", 4.f); a.setParam("model", model);   // small-signal gain from RMS (window-independent)
        a.prepare({rates[i], 256, 1});
        auto y = processMono(a, makeSine(rates[i], 1000, std::pow(10.0, -30.0 / 20.0), (size_t)rates[i]), 256);
        g[i] = db(analysis::rms(y.data() + y.size() / 2, y.size() / 2));
        AB763Amp b; b.setParam("volume", 8.f); b.setParam("model", model);
        thd[i] = sineReport(b, rates[i], 220, -12.0).thd;
    }
    CHECK_MSG(std::fabs(g[0] - g[2]) < 0.3 && std::fabs(g[1] - g[2]) < 0.3, "model " << model << " gain " << g[0] << " " << g[1] << " " << g[2]);
    CHECK_MSG(std::fabs(thd[0] - thd[2]) < 0.2 * thd[2], "model " << model << " thd " << thd[0] << " vs " << thd[2]);
  }
}

TEST(audio, ab763_m2_model_parameter_and_legacy_default) {
    AB763Amp a;
    CHECK(std::string(a.params().info(AB763Amp::kModelParam).id) == "model");
    CHECK(a.params().getInt(AB763Amp::kModelParam) == AB763Amp::kM2); // new instances: M2
    // a save written before the parameter existed (v0.4) loads as M1
    Engine e;
    e.prepare(48000, 256);
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"format":"mfx-preset","version":1,"name":"v04","chain":[{"type":"amp_ab763","params":{"volume":6}}]})", j, &err));
    CHECK(presetFromJson(e, j).ok);
    CHECK(e.effect(e.allSlotsInOrder()[0])->params().getInt(AB763Amp::kModelParam) == AB763Amp::kM1);
}

TEST(audio, ab763_m2_breakup_harmonics_and_nfb) {
    double thd[3];
    const float vols[3] = {2.f, 6.f, 10.f};
    for (int i = 0; i < 3; ++i) {
        AB763Amp a; a.setParam("volume", vols[i]);
        const auto r = sineReport(a, 48000, 220, -12.0);
        thd[i] = r.thd;
        record("ab763_m2_thd_vol" + std::to_string((int)vols[i]), r.thd * 100, "%");
        if (i == 2) record("ab763_m2_even_odd_vol10", r.evenOddRatioDb, "dB");
    }
    CHECK(thd[0] < 0.03);
    CHECK(thd[0] < thd[1] && thd[1] < thd[2]);
    CHECK(thd[2] > 0.08);
    // NFB: less distortion than M1 at the same setting (M1 has no feedback)
    AB763Amp m1; m1.setParam("model", 0.f); m1.setParam("volume", 10.f);
    CHECK(sineReport(m1, 48000, 220, -12.0).thd > thd[2]);
}

TEST(audio, ab763_m2_reactive_load_shapes_response) {
    // small-signal response re 1 kHz: speaker resonance (~95 Hz) and voice-coil inductance (treble rise)
    // appear in M2 only (current-driven reactive load), M1 has neither.
    auto respDb = [](float model, double f) {
        AB763Amp a; a.setParam("model", model); a.setParam("volume", 4.f);
        a.prepare({48000, 256, 1});
        auto y = processMono(a, makeSine(48000, f, 0.01, 48000), 256);
        return db(analysis::rms(y.data() + 24000, 24000));
    };
    const double m1lo = respDb(0.f, 95.0) - respDb(0.f, 1000.0), m2lo = respDb(1.f, 95.0) - respDb(1.f, 1000.0);
    const double m1hi = respDb(0.f, 10000.0) - respDb(0.f, 1000.0), m2hi = respDb(1.f, 10000.0) - respDb(1.f, 1000.0);
    record("ab763_m2_minus_m1_95hz", m2lo - m1lo, "dB");
    record("ab763_m2_minus_m1_10khz", m2hi - m1hi, "dB");
    CHECK(m2lo - m1lo > 3.0);
    CHECK(m2hi - m1hi > 2.0);
}

TEST(audio, ab763_m2_sag_attack_and_recovery) {
    AB763Amp a; a.setParam("volume", 10.f);
    a.prepare({48000, 64, 1});
    auto idle = processMono(a, std::vector<float>(48000 / 2, 0.0f), 64);
    const float vIdle = a.lastSupplyVolts();
    record("ab763_m2_bplus_idle", vIdle, "V");
    // 300 ms loud burst, sample B+ every 8 samples
    auto burst = makeSine(48000, 110, 0.7, 48000 * 3 / 10);
    std::vector<float> trace;
    for (size_t p = 0; p < burst.size(); p += 8) {
        std::vector<float> blk(burst.begin() + (long)p, burst.begin() + (long)std::min(burst.size(), p + 8));
        processMono(a, blk, 8);
        trace.push_back(a.lastSupplyVolts());
    }
    const float vLoaded = trace.back();
    const float drop = vIdle - vLoaded;
    size_t i63 = 0;
    while (i63 < trace.size() && vIdle - trace[i63] < 0.63f * drop) ++i63;
    const double attackMs = 1000.0 * (double)(i63 * 8) / 48000.0;
    record("ab763_m2_sag_drop", drop, "V");
    record("ab763_m2_sag_attack_63pct", attackMs, "ms");
    CHECK(drop > 5.0f);
    CHECK_MSG(attackMs > 0.3 && attackMs < 80.0, "attack " << attackMs << " ms, drop " << drop << " idle " << vIdle);
    // recovery after the burst
    processMono(a, std::vector<float>(48000 / 5, 0.0f), 64);
    record("ab763_m2_bplus_after_200ms", a.lastSupplyVolts(), "V");
    CHECK(std::fabs(a.lastSupplyVolts() - vIdle) < 0.3f * drop);
    CHECK(std::isfinite(a.lastSupplyVolts()));
}

TEST(audio, ab763_m2_extreme_input_bounded_all_rates) {
    for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (float os : {0.f, 3.f}) {
            AB763Amp a; a.setParam("volume", 10.f); a.setParam("bass", 10.f); a.setParam("treble", 10.f);
            a.setParam("oversampling", os);
            a.prepare({fs, 512, 1});
            auto x = makeNoise((size_t)fs / 2, 4.0, 9);
            x[100] = NAN;
            auto y = processMono(a, x, 512);
            bool ok = true; double pk = 0;
            for (float v : y) { ok &= std::isfinite(v); pk = std::max(pk, (double)std::fabs(v)); }
            CHECK_MSG(ok && pk < 10.0, "fs " << fs << " os " << os << " peak " << pk);
        }
    }
}

TEST(audio, ab763_preset_roundtrip_with_cabinet) {
    Engine e;
    e.prepare(48000, 256);
    auto amp = createEffect("amp_ab763");
    amp->setParam("volume", 7.f); amp->setParam("treble", 5.5f); amp->setParam("bass", 3.f);
    e.addEffect(std::move(amp));
    e.addEffect(createEffect("cabinet"));
    const Json j = presetToJson(e, "deluxe");
    Engine f;
    f.prepare(48000, 256);
    auto rep = presetFromJson(f, j);
    CHECK(rep.ok && rep.warnings.empty());
    CHECK(presetToJson(f, "deluxe").dump() == j.dump());
    auto x = makePluck(48000, 196, 0.5, 0.4);
    std::vector<float> l1(x.size()), r1(x.size()), l2(x.size()), r2(x.size());
    for (size_t p = 0; p < x.size(); p += 256) {
        const int n = (int)std::min<size_t>(256, x.size() - p);
        const float* in[1] = {x.data() + p};
        float* o1[2] = {l1.data() + p, r1.data() + p};
        float* o2[2] = {l2.data() + p, r2.data() + p};
        e.process(in, 1, o1, 2, n);
        f.process(in, 1, o2, 2, n);
    }
    CHECK(l1 == l2);
    CHECK(allFinite(l1));
}

TEST(audio, custom_drive_model_parameter_and_versions) {
    CustomDrive d;
    CHECK(d.params().size() == CustomDrive::kNumParams);
    CHECK(std::string(d.params().info(CustomDrive::kModel).id) == "model");
    CHECK(d.params().getInt(CustomDrive::kModel) == CustomDrive::kModelV2); // default = v2 sound (unchanged)
    auto run = [](int model, double fs, double f0, double dbfs, float drive) {
        CustomDrive c;
        c.setParam("drive", drive);
        c.setParam("model", (float)model);
        return sineReport(c, fs, f0, dbfs);
    };
    const auto v1 = run(0, 48000, 440, -12, 50), v2 = run(1, 48000, 440, -12, 50);
    CHECK(std::fabs(v1.thd - v2.thd) > 0.01); // v1 path really differs (bit-exactness vs v0.1 checked with mfx_render)
    // ADAA: same tone (THD within 1 % abs), less aliasing at high gain (3953 Hz stress tone, 2x)
    for (float drive : {75.f, 100.f}) {
        const auto a = run(1, 48000, 3953, -7.2, drive), b = run(2, 48000, 3953, -7.2, drive);
        record("cd_alias_v2_drive" + std::to_string((int)drive), a.nonHarmonicDb, "dB");
        record("cd_alias_adaa_drive" + std::to_string((int)drive), b.nonHarmonicDb, "dB");
        CHECK_MSG(b.nonHarmonicDb < a.nonHarmonicDb - 10.0, "drive " << drive << ": v2 " << a.nonHarmonicDb << " adaa " << b.nonHarmonicDb);
    }
    const auto t2 = run(1, 48000, 440, -7.2, 60), ta = run(2, 48000, 440, -7.2, 60);
    CHECK_NEAR(t2.thd, ta.thd, 0.01);
    CHECK_NEAR(t2.fundamentalDb, ta.fundamentalDb, 0.2);
}

TEST(audio, custom_drive_preset_without_model_loads_v2) {
    Engine e;
    e.prepare(48000, 256);
    Json j;
    std::string err;
    CHECK(Json::parse(R"({"format":"mfx-preset","version":1,"name":"old","chain":[{"type":"custom_drive","params":{"drive":60}}]})", j, &err));
    auto rep = presetFromJson(e, j);
    CHECK(rep.ok);
    CHECK(e.effect(e.allSlotsInOrder()[0])->params().getInt(CustomDrive::kModel) == CustomDrive::kModelV2);
}

TEST(audio, custom_drive_adaa_compensation_v05) {
    CustomDrive d;
    CHECK(std::string(d.params().info(CustomDrive::kAdaaComp).id) == "adaa_comp");
    CHECK(d.params().getInt(CustomDrive::kAdaaComp) == 1);  // new instances: on
    float lv = -1.f;
    CHECK(d.legacyValueForMissingParam(CustomDrive::kAdaaComp, lv) && lv == 0.f); // v0.4 saves: off
    auto render = [](int model, int comp, float mix, double f, double amp, float drive = 0.f) {
        CustomDrive c;
        c.setParam("model", (float)model); c.setParam("adaa_comp", (float)comp);
        c.setParam("drive", drive); c.setParam("tone", 100.f); c.setParam("mix", mix);
        c.prepare({48000, 256, 1});
        return processMono(c, makeSine(48000, f, amp, 24000), 256);
    };
    auto lvl = [](const std::vector<float>& y) { return db(analysis::rms(y.data() + 12000, 12000)); };
    // small-signal 10 kHz: compensated ADAA matches v2 (uncompensated droops ~1 dB at 2x)
    const double v2 = lvl(render(1, 0, 100, 10000, 0.003)), raw = lvl(render(2, 0, 100, 10000, 0.003)),
                 cmp = lvl(render(2, 1, 100, 10000, 0.003));
    record("cd_adaa_raw_minus_v2_10k", raw - v2, "dB");
    record("cd_adaa_comp_minus_v2_10k", cmp - v2, "dB");
    CHECK(std::fabs(cmp - v2) < 0.25 && raw - v2 < -0.5);
    // dry/wet alignment at mix 50 %, 10 kHz
    const double m2 = lvl(render(1, 0, 50, 10000, 0.003)), mc = lvl(render(2, 1, 50, 10000, 0.003));
    CHECK_MSG(std::fabs(mc - m2) < 0.25, "mix50 comp " << mc - m2);
    // v2 output does not depend on the compensation switch
    CHECK(render(1, 0, 100, 440, 0.3, 60) == render(1, 1, 100, 440, 0.3, 60));
    // latency: +1 base sample at 2x (wet padded to a whole sample)
    CustomDrive c; c.setParam("model", 2.f); c.setParam("adaa_comp", 1.f); c.prepare({48000, 256, 1});
    CustomDrive c0; c0.setParam("model", 2.f); c0.setParam("adaa_comp", 0.f); c0.prepare({48000, 256, 1});
    CHECK(c.latencySamples() == c0.latencySamples() + 1);
}

TEST(audio, custom_drive_adaa_numerical_stability) {
    // very slow / tiny / constant inputs exercise the |dx| -> 0 fallback: no spikes, matches v2
    for (double amp : {1e-6, 1e-3, 0.3}) {
        CustomDrive a, b;
        a.setParam("model", 1.f); b.setParam("model", 2.f); b.setParam("adaa_comp", 1.f);
        a.setParam("drive", 100.f); b.setParam("drive", 100.f);
        a.prepare({48000, 256, 1}); b.prepare({48000, 256, 1});
        auto x = makeSine(48000, 3.0, amp, 48000);
        for (size_t i = 30000; i < 34000; ++i) x[i] = (float)amp; // constant segment
        auto ya = processMono(a, x, 256), yb = processMono(b, x, 256);
        double diff = 0, pk = 0;
        for (size_t i = 1000; i + 1 < ya.size(); ++i) { // compensated ADAA is 1 base sample later at 2x
            diff = std::max(diff, (double)std::fabs(ya[i] - yb[i + 1]));
            pk = std::max(pk, (double)std::fabs(ya[i]));
        }
        CHECK(allFinite(yb));
        CHECK_MSG(diff <= 0.05 * std::max(pk, 1e-9) + 1e-7, "amp " << amp << " diff " << diff << " peak " << pk);
    }
}
