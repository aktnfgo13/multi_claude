// Audio-level tests for every effect block.
#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/amp/AmpSim.h"
#include "dsp/cabinet/Cabinet.h"
#include "dsp/delay/Delay.h"
#include "dsp/dynamics/Dynamics.h"
#include "dsp/gain/Utility.h"
#include "dsp/reverb/Reverb.h"
#include "dsp/utility/Tuner.h"
#include "engine/EffectFactory.h"
#include "neural/NamAmp.h"

using namespace mfx;
using namespace mfxtest;

static std::vector<float> runStereo(Effect& fx, std::vector<float> l, std::vector<float>& rOut, int block) {
    rOut = l;
    runEffect(fx, l, &rOut, block);
    return l;
}

TEST(audio, all_effects_finite_bounded_at_all_rates_and_blocks) {
    for (const auto& t : availableEffects()) {
        for (double fs : {44100.0, 48000.0, 96000.0}) {
            auto fx = createEffect(t.typeId);
            fx->prepare({fs, 512, 2});
            auto x = makeNoise((size_t)(fs * 0.5), 0.9);
            auto s = makeSine(fs, 220.0, 1.0, (size_t)(fs * 0.5));
            x.insert(x.end(), s.begin(), s.end());
            std::vector<float> r;
            for (int block : {1, 17, 512}) {
                if (block == 1 && fs > 48000) continue;
                fx->reset();
                auto l = runStereo(*fx, x, r, block);
                CHECK_MSG(allFinite(l) && allFinite(r), t.typeId << " fs " << fs << " block " << block);
                const double pk = std::max(analysis::peak(l.data(), l.size()), analysis::peak(r.data(), r.size()));
                CHECK_MSG(pk < 16.0, t.typeId << " peak " << pk); // < +24 dBFS even at extreme settings
            }
        }
    }
}

TEST(audio, block_size_invariance) {
    // With static parameters, every block must produce identical output regardless of host block size.
    for (const auto& t : availableEffects()) {
        auto a = createEffect(t.typeId), b = createEffect(t.typeId);
        a->prepare({48000.0, 512, 2});
        b->prepare({48000.0, 512, 2});
        auto x = makePluck(48000.0, 110.0, 0.4, 0.5);
        std::vector<float> ra, rb;
        auto la = runStereo(*a, x, ra, 512);
        auto lb = runStereo(*b, x, rb, 37);
        double err = 0.0;
        for (size_t i = 0; i < la.size(); ++i) err = std::max(err, (double)std::fabs(la[i] - lb[i]));
        CHECK_MSG(err < 1e-5, t.typeId << " max diff " << err);
    }
}

TEST(audio, long_duration_stability_60s) {
    // one minute of noise bursts / silence through the heavy blocks: no NaN, no runaway, decays to silence
    for (const char* id : {"custom_drive", "amp", "delay", "reverb", "flanger", "phaser", "distortion", "fuzz"}) {
        auto fx = createEffect(id);
        if (std::string(id) == "delay") fx->setParam("feedback", 100.0f);
        if (std::string(id) == "reverb") fx->setParam("decay_s", 20.0f);
        fx->prepare({48000.0, 256, 2});
        std::vector<float> l(256), r(256);
        double lastPeak = 0.0;
        uint32_t seed = 1;
        for (int blk = 0; blk < 48000 * 60 / 256; ++blk) {
            const bool loud = (blk / 200) % 2 == 0 && blk < 48000 * 50 / 256;
            for (int i = 0; i < 256; ++i) { seed = seed * 1664525u + 1013904223u; l[(size_t)i] = r[(size_t)i] = loud ? (float)((seed >> 8) / 8388608.0 - 1.0) * 0.8f : 0.0f; }
            AudioBlock b; b.numChannels = 2; b.numSamples = 256; b.ch[0] = l.data(); b.ch[1] = r.data();
            fx->process(b);
            CHECK_MSG(allFinite(l) && allFinite(r), id << " block " << blk);
            lastPeak = analysis::peak(l.data(), 256);
            CHECK_MSG(lastPeak < 16.0, id << " runaway " << lastPeak);
        }
        record(std::string(id) + "_peak_after_10s_silence", db(lastPeak), "dBFS");
    }
}

TEST(audio, utility_gain_accuracy_and_phase) {
    GainUtility g;
    g.setParam("gain", 6.0206f);
    g.setParam("invert", 1.0f);
    g.prepare({48000.0, 128, 1});
    auto y = processMono(g, std::vector<float>(1000, 0.25f));
    CHECK_NEAR(y[999], -0.5, 1e-4);
}

TEST(audio, pan_constant_power) {
    auto p = createEffect("pan");
    p->setParam("pan", 100.0f);
    p->prepare({48000.0, 128, 2});
    std::vector<float> r;
    auto l = runStereo(*p, std::vector<float>(500, 0.5f), r, 128);
    CHECK_NEAR(l[499], 0.0, 1e-5);
    CHECK_NEAR(r[499], 0.5 * 1.41421356, 1e-4);
}

TEST(audio, compressor_static_curve) {
    Compressor c;
    c.setParam("threshold_db", -20.0f);
    c.setParam("ratio", 4.0f);
    c.setParam("knee_db", 0.0f);
    c.setParam("attack_ms", 1.0f);
    c.setParam("release_ms", 50.0f);
    c.prepare({48000.0, 128, 1});
    // a square-ish (constant magnitude) input makes the peak detector settle exactly
    std::vector<float> x(48000);
    for (size_t i = 0; i < x.size(); ++i) x[i] = ((i / 24) % 2 ? 1.0f : -1.0f) * (float)dbToGainD(-8.0);
    auto y = processMono(c, x);
    const double outDb = db(analysis::peak(y.data() + 40000, 8000));
    CHECK_NEAR(outDb, -20.0 + 12.0 / 4.0, 0.3); // -17 dB
    CHECK_NEAR(compressorGainComputer(-30.0f, -20.0f, 4.0f, 6.0f), 0.0, 1e-6);
}

TEST(audio, limiter_never_exceeds_ceiling) {
    Limiter l;
    l.setParam("ceiling_db", -1.0f);
    l.setParam("input_db", 12.0f);
    l.prepare({48000.0, 128, 1});
    auto x = makeNoise(96000, 1.0);
    auto y = processMono(l, x);
    CHECK(analysis::peak(y.data(), y.size()) <= dbToGain(-1.0f) + 1e-6);
    CHECK(l.latencySamples() == 72);
}

TEST(audio, gate_attenuates_below_threshold_and_opens_above) {
    NoiseGate g;
    g.setParam("threshold_db", -40.0f);
    g.setParam("range_db", -60.0f);
    g.prepare({48000.0, 128, 1});
    auto quiet = makeSine(48000, 1000, dbToGainD(-55.0), 48000);
    auto yq = processMono(g, quiet);
    CHECK(db(analysis::rms(yq.data() + 24000, 24000) / analysis::rms(quiet.data() + 24000, 24000)) < -50.0);
    auto loud = makeSine(48000, 1000, dbToGainD(-20.0), 48000);
    auto yl = processMono(g, loud);
    CHECK_NEAR(db(analysis::rms(yl.data() + 24000, 24000) / analysis::rms(loud.data() + 24000, 24000)), 0.0, 0.1);
}

TEST(audio, digital_delay_echo_time) {
    Delay d;
    d.setParam("mode", 0); d.setParam("routing", 0);
    d.setParam("time_ms", 250.0f); d.setParam("feedback", 0.0f); d.setParam("mix", 100.0f);
    d.setParam("mod_depth", 0.0f); d.setParam("low_cut_hz", 20.0f); d.setParam("high_cut_hz", 20000.0f);
    d.prepare({48000.0, 128, 2});
    auto y = processMono(d, makeImpulse(24000));
    size_t arg = 0;
    for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[arg])) arg = i;
    CHECK_MSG(std::abs((long)arg - 12000) <= 1, "echo at " << arg);
    // tempo sync: 1/4 at 100 BPM = 600 ms
    d.setParam("sync", 3.0f);
    d.setTempo(100.0);
    CHECK_NEAR(d.effectiveTimeMs(), 600.0, 1e-6);
}

TEST(audio, reverb_rt60_order_of_magnitude) {
    for (float T : {1.0f, 4.0f}) {
        Reverb r;
        r.setParam("type", 1.0f); r.setParam("decay_s", T); r.setParam("mix", 100.0f);
        r.setParam("predelay_ms", 0.0f); r.setParam("damping", 0.0f); r.setParam("low_cut_hz", 20.0f); r.setParam("high_cut_hz", 20000.0f);
        r.prepare({48000.0, 256, 2});
        std::vector<float> rr;
        auto x = makeNoise(4800, 0.5);
        x.resize((size_t)(48000 * (T * 1.5 + 0.5)), 0.0f);
        auto l = runStereo(r, x, rr, 256);
        // Schroeder backward integration, T20 fit (-5 .. -25 dB) * 3
        std::vector<double> edc(l.size());
        double acc = 0.0;
        for (size_t i = l.size(); i-- > 4800;) { acc += (double)l[i] * l[i]; edc[i] = acc; }
        const double e0 = edc[4800];
        size_t i5 = 0, i25 = 0;
        for (size_t i = 4800; i < l.size(); ++i) {
            const double d = 10.0 * std::log10(edc[i] / e0 + 1e-30);
            if (!i5 && d < -5.0) i5 = i;
            if (!i25 && d < -25.0) { i25 = i; break; }
        }
        CHECK(i25 > i5);
        const double t60 = 3.0 * (double)(i25 - i5) / 48000.0;
        record("reverb_hall_t60_for_" + std::to_string((int)T) + "s", t60, "s");
        CHECK_MSG(t60 > 0.6 * T && t60 < 1.5 * T, "T60 " << t60 << " for target " << T);
    }
}

TEST(audio, stereo_effects_decorrelate_channels) {
    for (const char* id : {"chorus", "flanger", "phaser", "reverb", "delay"}) {
        auto fx = createEffect(id);
        fx->prepare({48000.0, 128, 2});
        std::vector<float> r;
        auto l = runStereo(*fx, makePluck(48000, 196, 1.0), r, 128);
        double diff = 0.0;
        for (size_t i = 0; i < l.size(); ++i) diff += std::fabs(l[i] - r[i]);
        CHECK_MSG(diff > 1.0, id << " produced identical channels");
    }
}

TEST(audio, tuner_block_detects_pitch_and_mutes) {
    Tuner t;
    t.prepare({48000.0, 256, 1});
    auto x = makeSine(48000, 146.83, 0.3, 48000); // D3
    auto y = processMono(t, x, 256);
    CHECK(analysis::peak(y.data(), y.size()) == 0.0);
    auto r = t.analyze();
    CHECK(r.valid);
    CHECK(std::string(r.noteName()) == "D");
    CHECK(std::fabs(r.cents) < 2.0);
}

TEST(audio, cabinet_ir_delta_and_resampled_ir) {
    Cabinet c;
    c.setParam("source", 1.0f); c.setParam("low_cut_hz", 20.0f); c.setParam("high_cut_hz", 20000.0f); c.setParam("normalize", 0.0f);
    c.prepare({48000.0, 256, 1});
    std::vector<float> ir(400, 0.0f);
    ir[100] = 0.5f;
    CHECK(c.loadIR(ir, 48000.0, "delta"));
    // process enough to complete the fade-out/in hand-over
    auto warm = processMono(c, std::vector<float>(2048, 0.0f), 256);
    auto x = makeNoise(8000, 0.5);
    auto y = processMono(c, x, 256);
    double err = 0.0;
    for (size_t i = 300; i < 7000; ++i) err = std::max(err, (double)std::fabs(y[i] - 0.5f * x[i - 100] * 1.0f));
    // the 20 Hz HPF / 20 kHz LPF are still in the path -> small deviation
    CHECK_MSG(err < 0.05, "err " << err);
    // 44.1k IR resampled to 48k keeps its delay in seconds
    std::vector<float> ir2(441, 0.0f); ir2[441 / 2] = 1.0f;
    CHECK(c.loadIR(ir2, 44100.0, "delta44"));
    processMono(c, std::vector<float>(2048, 0.0f), 256);
    auto y2 = processMono(c, makeImpulse(2000), 256);
    size_t arg = 0;
    for (size_t i = 0; i < y2.size(); ++i) if (std::fabs(y2[i]) > std::fabs(y2[arg])) arg = i;
    CHECK_MSG(std::abs((long)arg - 240) <= 2, "resampled delay at " << arg);
    c.collectGarbage();
}

TEST(audio, amp_models_ordered_by_distortion) {
    double prevThd = -1.0;
    for (int m = 0; m < 3; ++m) {
        AmpSim a;
        a.setParam("model", (float)m);
        a.setParam("gain", 60.0f);
        a.prepare({48000.0, 256, 1});
        auto x = makeSine(48000, 220.0, 0.25, 48000);
        auto y = processMono(a, x);
        auto h = analysis::analyzeHarmonics(y.data() + 24000, 16384, 48000, 220.0);
        record(std::string("amp_thd_model_") + std::to_string(m), 100.0 * h.thd, "%");
        CHECK_MSG(h.thd > prevThd, "model " << m << " thd " << h.thd);
        prevThd = h.thd;
    }
}

TEST(audio, nam_block_reports_status_honestly) {
    NamAmp n;
    n.prepare({48000.0, 256, 1});
    if (!NamAmp::isAvailable()) {
        CHECK(n.info().status.find("not built") != std::string::npos);
        auto x = makeNoise(1000, 0.3);
        auto y = processMono(n, x);
        CHECK(y == x); // pass-through, never pretends to model an amp
    } else {
        std::string err;
        CHECK(!n.loadModel("/nonexistent.nam", &err));
        CHECK(n.info().status.find("load failed") != std::string::npos);
    }
}
