// Real-time safety and performance.
//  - counts heap allocations performed *inside* Engine::process (must be zero)
//  - per-effect and full-chain CPU cost (recorded in the test report)
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>

#include "TestFramework.h"
#include "TestUtil.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"

static std::atomic<bool> gCountAllocs{false};
static std::atomic<long> gAllocs{0};

void* operator new(std::size_t n) {
    if (gCountAllocs.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    if (gCountAllocs.load(std::memory_order_relaxed)) gAllocs.fetch_add(1);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace mfx;
using namespace mfxtest;

static void buildFullChain(Engine& e) {
    e.addEffect(createEffect("noise_gate"));
    e.addEffect(createEffect("compressor"));
    e.addEffect(createEffect("custom_drive"));
    e.addEffect(createEffect("amp"));
    auto cab = createEffect("cabinet");
    e.addEffect(std::move(cab));
    e.addEffect(createEffect("parametric_eq"));
    e.addEffect(createEffect("chorus"));
    e.addEffect(createEffect("delay"));
    e.addEffect(createEffect("reverb"));
    e.addEffect(createEffect("limiter"), Lane::Post);
}

TEST(performance, no_heap_allocation_in_audio_thread) {
    Engine e;
    e.prepare(48000, 256);
    buildFullChain(e);
    std::vector<float> x = makeNoise(256, 0.5), l(256), r(256);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 256); // first block: routing activation
    // include parameter changes, bypass toggles and MIDI during processing
    const int slot = e.allSlotsInOrder()[2];
    gAllocs = 0;
    gCountAllocs = true;
    for (int i = 0; i < 400; ++i) {
        if (i == 50) e.effect(slot)->setParam("drive", 90.0f);
        if (i == 100) e.setBypass(slot, true);
        if (i == 150) e.setBypass(slot, false);
        if (i == 200) e.effect(slot)->setParam("oversampling", 3.0f);
        if (i == 250) e.handleMidi(0xB0, 1, 64);
        e.process(in, 1, out, 2, 256);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0, gAllocs.load() << " allocations in process()");
}

TEST(performance, per_effect_cost) {
    const double fs = 48000.0;
    const int block = 128;
    for (const auto& t : availableEffects()) {
        auto fx = createEffect(t.typeId);
        fx->prepare({fs, block, 2});
        auto l = makeNoise((size_t)fs * 2, 0.5), r = l;
        const auto t0 = std::chrono::steady_clock::now();
        runEffect(*fx, l, &r, block);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        record("cpu_percent_stereo_48k_" + t.typeId, 100.0 * s / 2.0, "%");
        CHECK_MSG(s / 2.0 < 0.25, t.typeId << " uses " << 100.0 * s / 2.0 << " % of one core");
    }
}

TEST(performance, full_chain_realtime_factor) {
    for (int block : {32, 128, 512}) {
        Engine e;
        e.prepare(48000, block);
        buildFullChain(e);
        auto x = makePluck(48000, 82.4, 10.0, 0.5);
        std::vector<float> l(x.size()), r(x.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t p = 0; p < x.size(); p += (size_t)block) {
            const int n = (int)std::min<size_t>((size_t)block, x.size() - p);
            const float* in[1] = {x.data() + p};
            float* out[2] = {l.data() + p, r.data() + p};
            e.process(in, 1, out, 2, n);
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double rtf = 10.0 / s;
        record("full_chain_realtime_factor_block" + std::to_string(block), rtf, "x");
        CHECK_MSG(rtf > 5.0, "realtime factor " << rtf);
        CHECK(allFinite(l) && allFinite(r));
    }
}
