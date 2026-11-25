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
    double g[3], thd[3];
    const double rates[3] = {44100.0, 48000.0, 96000.0};
    for (int i = 0; i < 3; ++i) {
        AB763Amp a; a.setParam("volume", 4.f);   // small-signal gain from RMS (window-independent)
        a.prepare({rates[i], 256, 1});
        auto y = processMono(a, makeSine(rates[i], 1000, std::pow(10.0, -30.0 / 20.0), (size_t)rates[i]), 256);
        g[i] = db(analysis::rms(y.data() + y.size() / 2, y.size() / 2));
        AB763Amp b; b.setParam("volume", 8.f);
        thd[i] = sineReport(b, rates[i], 220, -12.0).thd;
    }
    CHECK_MSG(std::fabs(g[0] - g[2]) < 0.3 && std::fabs(g[1] - g[2]) < 0.3, "gain " << g[0] << " " << g[1] << " " << g[2]);
    CHECK_MSG(std::fabs(thd[0] - thd[2]) < 0.2 * thd[2], "thd " << thd[0] << " vs " << thd[2]);
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
