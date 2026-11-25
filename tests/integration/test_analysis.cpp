// Analysis tap / worker, input meters and cabinet IR references.
#include <cmath>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/cabinet/Cabinet.h"
#include "engine/AnalysisWorker.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"
#include "platform/WavFile.h"
#include "presets/Preset.h"

using namespace mfx;
using namespace mfxtest;

namespace {
// chain with latency (Custom Drive 2x) + time-based effect, stereo-creating chorus
void buildChain(Engine& e) {
    e.addEffect(createEffect("custom_drive"));
    e.addEffect(createEffect("chorus"));
    e.addEffect(createEffect("delay"));
}
std::vector<float> render(Engine& e, const std::vector<float>& x, int block, std::vector<float>* rOut,
                          AnalysisWorker* w = nullptr) {
    std::vector<float> l(x.size()), r(x.size());
    for (size_t p = 0; p < x.size(); p += (size_t)block) {
        const int n = (int)std::min<size_t>((size_t)block, x.size() - p);
        const float* in[1] = {x.data() + p};
        float* out[2] = {l.data() + p, r.data() + p};
        e.process(in, 1, out, 2, n);
        if (w) w->poll(e.analysisTap());
    }
    if (rOut) *rOut = r;
    return l;
}
} // namespace

TEST(integration, analysis_tap_does_not_change_output) {
    const auto x = makePluck(48000, 110, 2.0, 0.6);
    std::vector<float> rOff, rOn;
    Engine a; a.prepare(48000, 480); buildChain(a);
    const auto lOff = render(a, x, 480, &rOff);
    Engine b; b.prepare(48000, 480); buildChain(b);
    b.analysisTap().setEnabled(true);
    AnalysisWorker w;
    const auto lOn = render(b, x, 480, &rOn, &w);
    bool same = lOff.size() == lOn.size();
    for (size_t i = 0; same && i < lOff.size(); ++i) same = lOff[i] == lOn[i] && rOff[i] == rOn[i];
    CHECK_MSG(same, "analysis tap changed the DSP output");
    CHECK(w.info().framesCaptured > 0);
}

TEST(integration, analysis_tap_delivers_real_input_samples_with_metadata) {
    Engine e;
    e.prepare(44100, 300);   // host block larger than the internal chunk -> several packets per block
    e.setInputGainDb(-6.0f); // tap sees the chain input = after input gain
    e.addEffect(createEffect("utility"));
    e.analysisTap().setEnabled(true);
    const auto x = makeNoise(44100 / 2, 0.5, 99);
    AnalysisWorker w;
    render(e, x, 300, nullptr, &w);
    const auto inf = w.info();
    CHECK_NEAR(inf.sampleRate, 44100.0, 1e-9);
    CHECK(inf.inChannels == 1);
    CHECK(inf.dropped == 0);
    CHECK(inf.framesCaptured == x.size());
    CHECK(inf.lastFrame == x.size());
    std::vector<float> got(4096);
    CHECK(w.latest(AnalysisWorker::Source::Input, AnalysisWorker::Channel::Left, 4096, got.data()));
    const float g = (float)std::pow(10.0, -6.0 / 20.0);
    double err = 0.0;
    for (int i = 0; i < 4096; ++i) err = std::max(err, (double)std::fabs(got[(size_t)i] - g * x[x.size() - 4096 + (size_t)i]));
    CHECK_MSG(err < 1e-6, "captured input differs from the chain input by " << err);
}

TEST(integration, analysis_aligns_input_and_output_by_chain_latency) {
    // Custom Drive at mix 0 = latency-aligned dry path -> aligned output must equal aligned input
    Engine e;
    e.prepare(48000, 256);
    auto cd = createEffect("custom_drive");
    cd->setParam("mix", 0.0f);
    cd->setParam("level", 0.0f);
    cd->setParam("oversampling", 1.0f); // 2x
    e.addEffect(std::move(cd));
    e.analysisTap().setEnabled(true);
    AnalysisWorker w;
    const auto x = makeSine(48000, 997, 0.5, 48000);
    render(e, x, 256, nullptr, &w);
    CHECK(w.info().latency > 0);
    record("analysis_alignment_latency", w.info().latency, "samples");
    std::vector<float> a(2048), b(2048);
    CHECK(w.latest(AnalysisWorker::Source::Input, AnalysisWorker::Channel::Left, 2048, a.data()));
    CHECK(w.latest(AnalysisWorker::Source::Output, AnalysisWorker::Channel::Left, 2048, b.data()));
    double err = 0.0;
    for (int i = 0; i < 2048; ++i) err = std::max(err, (double)std::fabs(a[(size_t)i] - b[(size_t)i]));
    CHECK_MSG(err < 1e-4, "aligned in/out differ by " << err);
}

TEST(integration, analysis_spectrum_and_waveform_basic) {
    Engine e;
    e.prepare(48000, 256);
    e.addEffect(createEffect("utility"));
    e.analysisTap().setEnabled(true);
    AnalysisWorker w;
    const double amp = std::pow(10.0, -6.0 / 20.0);
    // bin-centred tone for both FFT sizes (43 x 48000/2048 Hz): Hann reads the exact level there;
    // between bins the Hann window under-reads by up to 1.42 dB (scalloping, inherent to the window)
    const double fTone = 43.0 * 48000.0 / 2048.0;
    render(e, makeSine(48000, fTone, amp, 24000), 256, nullptr, &w);
    for (int n : {2048, 4096}) {
        const int pts = 400;
        std::vector<float> s((size_t)pts);
        CHECK(w.spectrum(AnalysisWorker::Source::Output, AnalysisWorker::Channel::Mono, n, pts, 20.f, 20000.f, s.data()));
        int pk = 0;
        for (int i = 1; i < pts; ++i) if (s[(size_t)i] > s[(size_t)pk]) pk = i;
        const float f = AnalysisWorker::pointFrequency(pk, pts, 20.f, 20000.f);
        record("spectrum_peak_db_fft" + std::to_string(n), s[(size_t)pk], "dBFS");
        CHECK_MSG(std::fabs(f - (float)fTone) < 40.f, "peak at " << f << " Hz");
        CHECK_MSG(std::fabs(s[(size_t)pk] - (-6.0f)) < 0.2f, "peak level " << s[(size_t)pk] << " dBFS");
        CHECK(s[10] < -60.f); // ~24 Hz: far from the tone
    }
    // off-bin tone: within the Hann scalloping bound
    render(e, makeSine(48000, 1000.0, amp, 24000), 256, nullptr, &w);
    {
        std::vector<float> s(400);
        CHECK(w.spectrum(AnalysisWorker::Source::Output, AnalysisWorker::Channel::Mono, 2048, 400, 20.f, 20000.f, s.data()));
        float pk = -200.f; for (float v : s) pk = std::max(pk, v);
        record("spectrum_peak_db_offbin_1k", pk, "dBFS");
        CHECK(pk <= -6.0f + 0.05f && pk >= -6.0f - 1.45f);
    }
    std::vector<float> mn(200), mx(200);
    CHECK(w.waveform(AnalysisWorker::Source::Output, AnalysisWorker::Channel::Left, 4800, 200, mn.data(), mx.data()));
    float hi = 0.f;
    for (int i = 0; i < 200; ++i) hi = std::max(hi, mx[(size_t)i]);
    CHECK_NEAR(hi, amp, 0.01);
    // pause freezes the history
    w.setPaused(true);
    const auto before = w.info().framesCaptured;
    render(e, makeSine(48000, 500, 0.1, 4800), 256, nullptr, &w);
    CHECK(w.info().framesCaptured == before);
}

TEST(integration, input_meters_source_peak_hot_and_nonfinite_guard) {
    Engine e;
    e.prepare(48000, 64);
    e.addEffect(createEffect("parametric_eq"));
    e.setInputGainDb(6.0f);
    auto x = makeSine(48000, 440, 0.5, 4800); // -6 dBFS source, 0 dBFS after +6 dB
    std::vector<float> l(64), r(64);
    for (size_t p = 0; p + 64 <= x.size(); p += 64) {
        const float* in[1] = {x.data() + p};
        float* out[2] = {l.data(), r.data()};
        e.process(in, 1, out, 2, 64);
    }
    auto m = e.readMeters();
    CHECK_NEAR(m.srcPeak, 0.5, 0.01);
    CHECK(m.inHot);
    CHECK(!m.inNonFinite);
    std::vector<float> bad(64, 0.1f);
    bad[10] = std::nanf(""); bad[20] = INFINITY;
    const float* in[1] = {bad.data()};
    float* out[2] = {l.data(), r.data()};
    for (int i = 0; i < 20; ++i) e.process(in, 1, out, 2, 64);
    m = e.readMeters();
    CHECK(m.inNonFinite);
    CHECK(allFinite(l) && allFinite(r));
}

TEST(integration, cabinet_ir_reference_survives_moves_and_missing_files) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "mfx_ir_ref_test";
    fs::remove_all(root);
    fs::create_directories(root / "a");
    fs::create_directories(root / "moved" / "ir");
    WavData ir;
    ir.sampleRate = 44100;
    ir.channels.push_back(makeNoise(2048, 0.5, 5));
    for (size_t i = 0; i < ir.channels[0].size(); ++i) ir.channels[0][i] *= (float)std::exp(-(double)i / 300.0);
    const std::string orig = (root / "a" / "cab.wav").string();
    CHECK(writeWav(orig, ir));

    // save a preset referencing the IR
    Engine e;
    e.prepare(48000, 256);
    auto cab = createEffect("cabinet");
    cab->setParam("source", 1.0f);
    std::string err;
    CHECK(static_cast<Cabinet*>(cab.get())->loadIRFile(orig, &err));
    const uint64_t h = static_cast<Cabinet*>(cab.get())->irHash();
    e.addEffect(std::move(cab));
    const std::string presetPath = (root / "a" / "p.json").string();
    CHECK(savePresetFile(e, "ir test", presetPath));
    const std::string st = e.effect(e.allSlotsInOrder()[0])->getExtraState();
    CHECK(st.rfind("mfx-ir:1", 0) == 0);

    // 1) original location
    { Engine f; f.prepare(48000, 256); auto rep = loadPresetFile(f, presetPath);
      CHECK(rep.ok && rep.warnings.empty());
      CHECK(static_cast<Cabinet*>(f.effect(f.allSlotsInOrder()[0]))->irHash() == h); }
    // 2) preset + IR moved together (IR in <preset dir>/ir/) -> found by name, verified by hash
    fs::rename(root / "a" / "cab.wav", root / "moved" / "ir" / "cab.wav");
    fs::copy_file(presetPath, root / "moved" / "p.json");
    { Engine f; f.prepare(48000, 256); auto rep = loadPresetFile(f, (root / "moved" / "p.json").string());
      CHECK_MSG(rep.ok && rep.warnings.empty(), (rep.warnings.empty() ? "" : rep.warnings[0]));
      auto* c = static_cast<Cabinet*>(f.effect(f.allSlotsInOrder()[0]));
      CHECK(c->hasIR() && c->irHash() == h); }
    // 3) missing everywhere -> warning, built-in simulation, finite output, reference kept on re-save
    { Engine f; f.prepare(48000, 256); auto rep = loadPresetFile(f, presetPath);
      CHECK(rep.ok);
      CHECK(!rep.warnings.empty());
      auto* c = f.effect(f.allSlotsInOrder()[0]);
      CHECK(!static_cast<Cabinet*>(c)->hasIR());
      CHECK(c->getExtraState() == st);
      auto x = makeNoise(4800, 0.3);
      std::vector<float> l(x.size()), r(x.size());
      for (size_t p = 0; p < x.size(); p += 256) {
          const int n = (int)std::min<size_t>(256, x.size() - p);
          const float* in[1] = {x.data() + p}; float* out[2] = {l.data() + p, r.data() + p};
          f.process(in, 1, out, 2, n);
      }
      double energy = 0; for (float v : l) energy += (double)v * v;
      CHECK(allFinite(l) && energy > 0.0); }
    // 4) same file name, different content -> loaded with a warning
    WavData other = ir; other.channels[0][5] += 0.25f;
    CHECK(writeWav((root / "a" / "cab.wav").string(), other));
    { Engine f; f.prepare(48000, 256); auto rep = loadPresetFile(f, presetPath);
      CHECK(rep.ok && !rep.warnings.empty());
      CHECK(static_cast<Cabinet*>(f.effect(f.allSlotsInOrder()[0]))->hasIR()); }
    // 5) legacy state (plain path) still loads
    { Cabinet c; std::string e2; CHECK(c.setExtraState((root / "a" / "cab.wav").string(), &e2)); CHECK(c.hasIR()); }
    fs::remove_all(root);
}

TEST(integration, analysis_tap_concurrent_producer_consumer) {
    // audio thread processes while a UI-like thread polls the worker and toggles the tap (TSan target)
    Engine e;
    e.prepare(48000, 128);
    buildChain(e);
    e.analysisTap().setEnabled(true);
    std::atomic<bool> stop{false};
    std::atomic<long> polled{0};
    std::thread ui([&] {
        AnalysisWorker w;
        std::vector<float> s(128), mn(64), mx(64);
        int i = 0;
        while (!stop.load()) {
            polled += w.poll(e.analysisTap());
            w.spectrum(AnalysisWorker::Source::Output, AnalysisWorker::Channel::Mono, 2048, 128, 20.f, 20000.f, s.data());
            w.waveform(AnalysisWorker::Source::Input, AnalysisWorker::Channel::Left, 2400, 64, mn.data(), mx.data());
            if (++i % 50 == 0) e.analysisTap().setEnabled(i % 100 != 0);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    const auto x = makeNoise(128, 0.3);
    std::vector<float> l(128), r(128);
    const float* in[1] = {x.data()};
    float* out[2] = {l.data(), r.data()};
    for (int b = 0; b < 3000; ++b) e.process(in, 1, out, 2, 128);
    stop = true;
    ui.join();
    CHECK(polled.load() > 0);
    CHECK(allFinite(l) && allFinite(r));
}
