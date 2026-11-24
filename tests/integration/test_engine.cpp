#include "TestFramework.h"
#include "TestUtil.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"

using namespace mfx;
using namespace mfxtest;

namespace {
struct Host {
    Engine& e;
    int block;
    std::vector<float> run(const std::vector<float>& x, int numIn = 1, std::vector<float>* right = nullptr) {
        std::vector<float> l(x.size()), r(x.size());
        for (size_t p = 0; p < x.size(); p += (size_t)block) {
            const int n = (int)std::min<size_t>((size_t)block, x.size() - p);
            const float* in[2] = {x.data() + p, x.data() + p};
            float* out[2] = {l.data() + p, r.data() + p};
            e.process(in, numIn, out, 2, n);
        }
        if (right) *right = r;
        return l;
    }
};
size_t argmaxAbs(const std::vector<float>& y) {
    size_t a = 0;
    for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[a])) a = i;
    return a;
}
} // namespace

TEST(integration, empty_engine_is_bit_transparent) {
    Engine e; e.prepare(48000, 256);
    Host h{e, 100};
    auto x = makeNoise(10000, 0.7);
    auto y = h.run(x);
    CHECK(y == x);
}

TEST(integration, bypass_signal_integrity_with_latency_compensation) {
    Engine e; e.prepare(48000, 256);
    auto d = createEffect("custom_drive");
    d->setParam("oversampling", 3.0f);
    const int s = e.addEffect(std::move(d));
    e.setBypass(s, true);
    e.prepare(48000, 256); // starts bypassed
    Host h{e, 64};
    auto x = makeNoise(8000, 0.5);
    auto y = h.run(x);
    const int lat = e.latencySamples();
    CHECK(lat == 41);
    double err = 0.0;
    for (size_t i = (size_t)lat; i < x.size(); ++i) err = std::max(err, (double)std::fabs(y[i] - x[i - (size_t)lat]));
    CHECK(err == 0.0); // bit exact, latency preserved while bypassed
}

TEST(integration, reported_latency_equals_measured_delay) {
    Engine e; e.prepare(48000, 256);
    for (const char* id : {"custom_drive", "limiter", "overdrive"}) {
        auto fx = createEffect(id);
        if (fx->params().indexOf("mix") >= 0) fx->setParam("mix", 0.0f); // dry path only -> pure delay
        if (std::string(id) == "limiter") fx->setParam("ceiling_db", 0.0f);
        e.addEffect(std::move(fx));
    }
    e.prepare(48000, 256);
    Host h{e, 128};
    auto x = makeImpulse(4000, 0.5f);
    auto y = h.run(x);
    record("chain_latency_samples", e.latencySamples(), "samples");
    CHECK_MSG((int)argmaxAbs(y) == e.latencySamples(), "measured " << argmaxAbs(y) << " reported " << e.latencySamples());
}

TEST(integration, bypass_toggle_is_click_free) {
    Engine e; e.prepare(48000, 256);
    auto g = createEffect("utility");
    g->setParam("gain", -24.0f);
    const int s = e.addEffect(std::move(g));
    Host h{e, 128};
    auto x = makeSine(48000, 100, 0.5, 48000);
    std::vector<float> y(x.size()), r(x.size());
    for (size_t p = 0; p < x.size(); p += 128) {
        if (p == 24064) e.setBypass(s, true);
        const float* in[1] = {x.data() + p};
        float* out[2] = {y.data() + p, r.data() + p};
        e.process(in, 1, out, 2, 128);
    }
    double maxStep = 0.0;
    for (size_t i = 24000; i < 25000; ++i) maxStep = std::max(maxStep, (double)std::fabs(y[i] - y[i - 1]));
    const double sineStep = 0.5 * 2 * kPi * 100 / 48000; // max slope of the dry sine
    CHECK_MSG(maxStep < 1.5 * sineStep, "step " << maxStep);
}

TEST(integration, routing_changes_while_running_are_safe_and_faded) {
    Engine e; e.prepare(48000, 256);
    Host h{e, 128};
    auto x = makeSine(48000, 220, 0.5, 4800);
    h.run(x); // audio running
    const int a = e.addEffect(createEffect("overdrive"));
    const int b = e.addEffect(createEffect("chorus"));
    h.run(x);
    e.moveEffect(b, Lane::Pre, 0);
    auto y = h.run(x);
    CHECK(allFinite(y));
    CHECK(e.allSlotsInOrder().front() == b);
    e.removeEffect(a);
    for (int i = 0; i < 5; ++i) h.run(x); // removal retires the effect after the audio thread moved on
    e.collectGarbage();
    CHECK(e.allSlotsInOrder().size() == 1);
    // max step during a routing swap stays bounded (3 ms fades)
    e.addEffect(createEffect("utility"));
    auto z = h.run(makeSine(48000, 100, 0.5, 9600));
    double maxStep = 0.0;
    for (size_t i = 1; i < z.size(); ++i) maxStep = std::max(maxStep, (double)std::fabs(z[i] - z[i - 1]));
    CHECK(maxStep < 0.1);
}

TEST(integration, parallel_lanes_are_latency_aligned) {
    Engine e; e.prepare(48000, 256);
    auto d = createEffect("custom_drive");
    d->setParam("mix", 0.0f); d->setParam("level", 0.0f); d->setParam("oversampling", 2.0f); // pure 39-sample delay
    e.addEffect(std::move(d), Lane::A);
    e.addEffect(createEffect("utility"), Lane::B);
    e.setParallel(true);
    e.setParallelMix(0.5f);
    e.prepare(48000, 256);
    Host h{e, 256};
    auto y = h.run(makeImpulse(2000, 0.25f));
    // both lanes arrive together at the latency -> single peak of 0.5 (0.25 * 2 lanes)
    CHECK((int)argmaxAbs(y) == e.latencySamples());
    CHECK_NEAR(y[argmaxAbs(y)], 0.5, 1e-5);
    CHECK(e.latencySamples() == 39);
}

TEST(integration, mono_input_becomes_stereo_after_stereo_effect) {
    Engine e; e.prepare(48000, 256);
    e.addEffect(createEffect("custom_drive"));
    e.addEffect(createEffect("chorus"));
    Host h{e, 128};
    std::vector<float> r;
    auto l = h.run(makePluck(48000, 110, 1.0), 1, &r);
    double diff = 0.0;
    for (size_t i = 0; i < l.size(); ++i) diff += std::fabs(l[i] - r[i]);
    CHECK(diff > 1.0);
    // without stereo effects the output is dual-mono
    Engine m; m.prepare(48000, 256);
    m.addEffect(createEffect("custom_drive"));
    Host hm{m, 128};
    auto l2 = hm.run(makePluck(48000, 110, 0.5), 1, &r);
    CHECK(l2 == r);
}

TEST(integration, meters_and_clip_indicators) {
    Engine e; e.prepare(48000, 256);
    e.setOutputGainDb(6.0f);
    Host h{e, 128};
    h.run(makeSine(48000, 1000, 0.7, 4800));
    auto m = e.readMeters();
    CHECK_NEAR(m.inPeak, 0.7, 1e-3);
    CHECK_NEAR(m.outPeak, 0.7 * 1.9953, 1e-2);
    CHECK(!m.inClip && m.outClip);
    CHECK(m.cpuLoad >= 0.0f);
    auto m2 = e.readMeters();
    CHECK(m2.inPeak == 0.0f && !m2.outClip); // peak hold resets on read
}

TEST(integration, midi_cc_controls_parameter_and_bypass) {
    Engine e; e.prepare(48000, 256);
    const int s = e.addEffect(createEffect("custom_drive"));
    e.setMidiMappings({{-1, 11, s, 0, 0.0f, 1.0f}, {0, 64, s, -1, 0, 1}});
    Host h{e, 128};
    h.run(std::vector<float>(256, 0.0f));
    e.handleMidi(0xB0, 11, 127);
    CHECK_NEAR(e.effect(s)->getParam("drive"), 100.0, 1e-4);
    e.handleMidi(0xB3, 64, 127); // channel 4: mapping is channel 1 only
    CHECK(!e.isBypassed(s));
    e.handleMidi(0xB0, 64, 127);
    CHECK(e.isBypassed(s));
}

TEST(integration, sample_rate_change_reprepares_everything) {
    Engine e;
    e.prepare(44100, 512);
    e.addEffect(createEffect("delay"));
    e.addEffect(createEffect("reverb"));
    e.addEffect(createEffect("custom_drive"));
    for (double fs : {48000.0, 96000.0, 44100.0}) {
        e.prepare(fs, 64);
        Host h{e, 64};
        auto y = h.run(makeNoise((size_t)fs / 2, 0.5));
        CHECK(allFinite(y));
    }
}

TEST(integration, preset_reload_while_running_keeps_slot_ids) {
    // Host automation is bound to slot ids, so reloading the same preset while audio runs must
    // reproduce the same slots (old effects stay alive until the audio thread has moved on).
    Engine e; e.prepare(48000, 256);
    Host h{e, 128};
    auto x = makeSine(48000, 220, 0.3, 2048);
    h.run(x);
    std::vector<int> first;
    for (int k = 0; k < 3; ++k) {
        e.clear();
        e.addEffect(createEffect("custom_drive"));
        e.addEffect(createEffect("amp"));
        e.addEffect(createEffect("reverb"));
        if (k == 0) first = e.allSlotsInOrder();
        CHECK(e.allSlotsInOrder() == first);
        auto y = h.run(x);
        CHECK(allFinite(y));
    }
    CHECK((first == std::vector<int>{0, 1, 2}));
}
