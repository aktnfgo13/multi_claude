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
static std::atomic<long> gAllocs{0}, gFrees{0};

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
static void countFree(void* p) { if (p && gCountAllocs.load(std::memory_order_relaxed)) gFrees.fetch_add(1); }
void operator delete(void* p) noexcept { countFree(p); std::free(p); }
void operator delete[](void* p) noexcept { countFree(p); std::free(p); }
void operator delete(void* p, std::size_t) noexcept { countFree(p); std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { countFree(p); std::free(p); }

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
    gAllocs = 0; gFrees = 0;
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
    CHECK_MSG(gFrees.load() == 0, gFrees.load() << " frees in process()");
}

TEST(performance, no_heap_allocation_or_free_with_analysis_tap) {
    // analysis tap enabled, queue overflowing (no consumer) and toggled while processing
    Engine e;
    e.prepare(48000, 512);
    buildFullChain(e);
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    e.analysisTap().setEnabled(true);
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 300; ++i) {
        if (i == 100) e.analysisTap().setEnabled(false);
        if (i == 120) e.analysisTap().setEnabled(true);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees in process()");
    CHECK(e.analysisTap().droppedPackets() > 0); // queue full without consumer -> dropped, audio unaffected
    CHECK(allFinite(l) && allFinite(r));
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

TEST(performance, no_heap_allocation_or_free_ab763_and_custom_drive_adaa) {
    Engine e;
    e.prepare(48000, 512);
    auto cd = createEffect("custom_drive");
    cd->setParam("model", 2.0f);
    cd->setParam("drive", 90.0f);
    e.addEffect(std::move(cd));
    auto amp = createEffect("amp_ab763");   // default model: M2 (PI, 6V6, sag, NFB, load)
    amp->setParam("volume", 8.0f);
    e.addEffect(std::move(amp));
    e.addEffect(createEffect("cabinet"));
    e.addEffect(createEffect("reverb_plate_premium"));
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    const auto slots = e.allSlotsInOrder(); // (allocates: outside the counted region)
    Effect* drive = e.effect(slots[0]);
    Effect* ampFx = e.effect(slots[1]);
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 200; ++i) {
        if (i == 50) drive->setParam("model", 1.0f);  // model switch while running
        if (i == 80) ampFx->setParam("bass", 9.0f);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees");
    CHECK(allFinite(l) && allFinite(r));
}

TEST(performance, no_heap_allocation_or_free_v06_delays_and_fet) {
    Engine e;
    e.prepare(48000, 512);
    e.addEffect(createEffect("comp_fet"));
    e.addEffect(createEffect("delay_tape_premium"));
    e.addEffect(createEffect("delay_bbd_premium"));
    auto d = createEffect("delay");
    d->setParam("cross_fb", 50.0f); d->setParam("width", 150.0f); d->setParam("time_r_mode", 1.0f);
    e.addEffect(std::move(d));
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    const auto slots = e.allSlotsInOrder(); // (allocates: outside the counted region)
    Effect* fet = e.effect(slots[0]);
    Effect* tape = e.effect(slots[1]);
    Effect* bbd = e.effect(slots[2]);
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 200; ++i) {
        if (i == 40) tape->setParam("time_ms", 900.0f); // motor speed change while running
        if (i == 60) tape->setParam("heads", 2.0f);
        if (i == 80) bbd->setParam("time_ms", 50.0f);
        if (i == 90) fet->setParam("ratio", 4.0f);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees");
    CHECK(allFinite(l) && allFinite(r));
}

TEST(performance, no_heap_allocation_or_free_v07_gate_eq_bbd2) {
    Engine e;
    e.prepare(48000, 512);
    e.addEffect(createEffect("gate_premium"));
    e.addEffect(createEffect("eq_parametric_premium"));
    e.addEffect(createEffect("delay_bbd_premium")); // v2 default
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    const auto slots = e.allSlotsInOrder(); // (allocates: outside the counted region)
    Effect* gate = e.effect(slots[0]);
    Effect* eq = e.effect(slots[1]);
    Effect* bbd = e.effect(slots[2]);
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 200; ++i) {
        if (i == 20) eq->setParam("b2_type", 5.0f);      // type cross-fade
        if (i == 30) eq->setParam("b3_hz", 9000.0f);     // sweep
        if (i == 40) gate->setParam("mode", 0.0f);
        if (i == 60) bbd->setParam("chips", 2.0f);        // structure change (ring cleared, no allocation)
        if (i == 80) bbd->setParam("model", 0.0f);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees");
    CHECK(allFinite(l) && allFinite(r));
}

TEST(performance, no_heap_allocation_or_free_v08_modulation) {
    Engine e;
    e.prepare(48000, 512);
    for (const char* id : {"chorus_premium", "vibrato_premium", "phaser_premium", "univibe_premium", "flanger_premium",
                           "tremolo_premium", "rotary_premium"})
        e.addEffect(createEffect(id));
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    const auto slots = e.allSlotsInOrder(); // (allocates: outside the counted region)
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 200; ++i) {
        if (i == 20) e.effect(slots[0])->setParam("voices", 2.0f);
        if (i == 30) e.effect(slots[2])->setParam("stages", 4.0f);
        if (i == 40) e.effect(slots[4])->setParam("mode", 0.0f);
        if (i == 50) e.effect(slots[5])->setParam("model", 2.0f);
        if (i == 60) e.effect(slots[6])->setParam("speed", 1.0f);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees");
    CHECK(allFinite(l) && allFinite(r));
}

TEST(performance, no_heap_allocation_or_free_v09_reverbs) {
    Engine e;
    e.prepare(48000, 512);
    e.addEffect(createEffect("reverb_spring_premium"));
    e.addEffect(createEffect("reverb_hall_premium"));
    e.addEffect(createEffect("reverb_shimmer"));
    std::vector<float> x = makeNoise(512, 0.5), l(512), r(512);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    e.process(in, 1, out, 2, 512);
    const auto slots = e.allSlotsInOrder();
    Effect* sp = e.effect(slots[0]);
    Effect* hall = e.effect(slots[1]);
    Effect* shim = e.effect(slots[2]);
    gAllocs = 0; gFrees = 0;
    gCountAllocs = true;
    for (int i = 0; i < 200; ++i) {   // size / freeze / interval / structure / character changes while running
        if (i == 30) hall->setParam("size", 100.0f);
        if (i == 40) hall->setParam("freeze", 1.0f);
        if (i == 60) shim->setParam("interval", 2.0f);
        if (i == 70) shim->setParam("structure", 1.0f);
        if (i == 80) shim->setParam("freeze", 1.0f);
        if (i == 90) sp->setParam("character", 90.0f);
        if (i == 120) hall->setParam("freeze", 0.0f);
        e.process(in, 1, out, 2, 512);
    }
    gCountAllocs = false;
    CHECK_MSG(gAllocs.load() == 0 && gFrees.load() == 0, gAllocs.load() << " allocations, " << gFrees.load() << " frees");
    CHECK(allFinite(l) && allFinite(r));
}
