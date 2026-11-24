// Concurrency stress: a "message thread" mutates the engine while an "audio thread" processes.
// Meant to be run under ThreadSanitizer (cmake -DMFX_SANITIZE=thread) as well as normally.
#include <atomic>
#include <thread>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/cabinet/Cabinet.h"
#include "dsp/utility/Tuner.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"

using namespace mfx;
using namespace mfxtest;

TEST(integration, concurrent_routing_parameter_and_ir_changes) {
    Engine e;
    e.prepare(48000, 128);
    e.addEffect(createEffect("custom_drive"));
    const int cab = e.addEffect(createEffect("cabinet"));
    e.addEffect(createEffect("delay"));
    std::atomic<bool> stop{false};
    std::atomic<long> blocks{0};
    bool finite = true;
    std::thread audio([&] {
        auto x = makeNoise(128, 0.3);
        std::vector<float> l(128), r(128);
        while (!stop.load()) {
            const float* in[1] = {x.data()};
            float* out[2] = {l.data(), r.data()};
            e.process(in, 1, out, 2, 128);
            e.handleMidi(0xB0, 7, (int)(blocks.load() % 128));
            for (float v : l) if (!std::isfinite(v)) finite = false;
            blocks.fetch_add(1);
        }
    });
    std::vector<float> ir(2000);
    for (size_t i = 0; i < ir.size(); ++i) ir[i] = (float)std::exp(-(double)i / 200.0) * (i % 3 ? 0.2f : -0.3f);
    for (int k = 0; k < 150; ++k) {
        const int s = e.addEffect(createEffect(k % 2 ? "reverb" : "chorus"));
        if (auto* fx = e.effect(0)) fx->setParam("drive", (float)(k % 100));
        e.setBypass(0, k % 3 == 0);
        e.moveEffect(s, k % 2 ? Lane::A : Lane::Post, 0);
        if (k % 25 == 0) e.setParallel(!e.isParallel());
        if (k % 10 == 0) if (auto* c = dynamic_cast<Cabinet*>(e.effect(cab))) { c->setParam("source", 1); c->loadIR(ir, 44100.0); }
        e.readMeters();
        e.removeEffect(s);
        e.collectGarbage();
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    stop.store(true);
    audio.join();
    record("concurrency_audio_blocks", (double)blocks.load());
    CHECK(finite);
    CHECK(blocks.load() > 100);
}

TEST(integration, tuner_ring_is_readable_while_processing) {
    Tuner t;
    t.prepare({48000.0, 256, 1});
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        auto x = makeSine(48000, 110.0, 0.3, 256);
        size_t phase = 0;
        while (!stop.load()) {
            auto y = makeSine(48000, 110.0, 0.3, 256, 2.0 * kPi * 110.0 * (double)phase / 48000.0);
            AudioBlock b; b.numChannels = 1; b.numSamples = 256; b.ch[0] = y.data();
            t.process(b);
            phase += 256;
        }
    });
    int valid = 0;
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (t.analyze().valid) ++valid;
    }
    stop.store(true);
    audio.join();
    CHECK(valid > 0);
}
