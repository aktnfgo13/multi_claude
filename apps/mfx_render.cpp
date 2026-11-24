// mfx_render — offline renderer / benchmark / comparison harness.
//
//   mfx_render --list
//   mfx_render --describe custom_drive
//   mfx_render -i in.wav -o out.wav --preset presets/factory/Crunch.json [--block 128] [--sr 48000]
//   mfx_render -i in.wav -o out.wav --effect custom_drive --set drive=70 --set oversampling=3
//   mfx_render ... --bench bench.json [--analyze-thd 1000] [--tail 2.0] [--compensate-latency]
//
// Output: WAV (32-bit float by default) and a benchmark JSON (timing, memory, latency, analysis).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

#include "core/BuildInfo.h"
#include "dsp/analysis/Analysis.h"
#include "dsp/utility/Resample.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"
#include "platform/WavFile.h"
#include "presets/Preset.h"

using namespace mfx;

static long peakRssKb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return (long)(pmc.PeakWorkingSetSize / 1024);
    return -1;
#else
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
#if defined(__APPLE__)
    return ru.ru_maxrss / 1024;
#else
    return ru.ru_maxrss;
#endif
#endif
}

static void usage() {
    std::puts(
        "mfx_render (MultiEffects offline renderer)\n"
        "  --list                         list effect types\n"
        "  --describe TYPE                parameter description (JSON)\n"
        "  -i FILE / -o FILE              input / output WAV\n"
        "  --preset FILE                  preset JSON (mfx-preset)\n"
        "  --effect TYPE                  single-effect chain (repeatable, appended in order)\n"
        "  --set [N.]ID=VALUE             set parameter on effect N (default: last added)\n"
        "  --at SAMPLE:[N.]ID=VALUE       parameter event at an exact sample (block is split there)\n"
        "  --state [N.]VALUE              set extra state (IR / .nam path) on effect N\n"
        "  --sr HZ                        processing sample rate (input is resampled)\n"
        "  --block N                      host block size (default 256)\n"
        "  --tail SECONDS                 append silence to capture tails (default 0)\n"
        "  --compensate-latency           remove the engine latency from the output\n"
        "  --mono-out                     write a mono file (L+R)/2\n"
        "  --bits 16|24|32                output format (default 32 float)\n"
        "  --bench FILE                   write benchmark/analysis JSON\n"
        "  --analyze-thd F0               harmonic analysis of the output (steady state, last 50%)\n"
        "  --save-preset FILE             write the resulting chain as a preset");
}

int main(int argc, char** argv) {
    std::string inPath, outPath, presetPath, benchPath, savePreset;
    std::vector<std::string> effects, sets, states;
    struct Event { size_t sample; std::string spec; };
    std::vector<Event> events;
    double sr = 0.0, tail = 0.0, thdF0 = 0.0;
    int block = 256, bits = 32;
    bool compensate = false, monoOut = false;
    for (int a = 1; a < argc; ++a) {
        std::string s = argv[a];
        auto next = [&]() -> std::string { if (a + 1 >= argc) { std::cerr << "missing value for " << s << "\n"; std::exit(2); } return argv[++a]; };
        if (s == "--version") { std::printf("%s %s\n", mfx::build::productName, mfx::build::buildId); return 0; }
        if (s == "--list") {
            for (const auto& e : availableEffects()) std::printf("%-16s %-12s %s\n", e.typeId.c_str(), e.category.c_str(), e.displayName.c_str());
            return 0;
        } else if (s == "--describe") {
            auto fx = createEffect(next());
            if (!fx) { std::cerr << "unknown type\n"; return 2; }
            fx->prepare({48000.0, 256, 2});
            std::cout << describeEffect(*fx).dump(2) << "\n";
            return 0;
        } else if (s == "-i") inPath = next();
        else if (s == "-o") outPath = next();
        else if (s == "--preset") presetPath = next();
        else if (s == "--effect") effects.push_back(next());
        else if (s == "--set") sets.push_back(std::to_string(effects.size()) + "|" + next());
        else if (s == "--state") states.push_back(std::to_string(effects.size()) + "|" + next());
        else if (s == "--at") {
            const std::string v = next();
            const auto colon = v.find(':');
            if (colon == std::string::npos) { std::cerr << "bad --at\n"; return 2; }
            events.push_back({(size_t)std::stoll(v.substr(0, colon)), std::to_string(effects.size()) + "|" + v.substr(colon + 1)});
        }
        else if (s == "--sr") sr = std::stod(next());
        else if (s == "--block") block = std::stoi(next());
        else if (s == "--tail") tail = std::stod(next());
        else if (s == "--bits") bits = std::stoi(next());
        else if (s == "--bench") benchPath = next();
        else if (s == "--analyze-thd") thdF0 = std::stod(next());
        else if (s == "--compensate-latency") compensate = true;
        else if (s == "--mono-out") monoOut = true;
        else if (s == "--save-preset") savePreset = next();
        else if (s == "-h" || s == "--help") { usage(); return 0; }
        else { std::cerr << "unknown argument " << s << "\n"; usage(); return 2; }
    }
    if (inPath.empty() || (outPath.empty() && benchPath.empty())) { usage(); return 2; }

    WavData in;
    std::string err;
    if (!readWav(inPath, in, &err)) { std::cerr << "input: " << err << "\n"; return 1; }
    if (sr <= 0) sr = in.sampleRate;
    if (std::fabs(sr - in.sampleRate) > 0.5)
        for (auto& ch : in.channels) ch = resampleOffline(ch, in.sampleRate, sr);
    const int numIn = std::min(2, in.numChannels());
    const size_t frames = in.numFrames();
    const size_t tailFrames = (size_t)(tail * sr);

    Engine engine;
    engine.prepare(sr, block);
    std::vector<std::string> warnings;
    if (!presetPath.empty()) {
        auto rep = loadPresetFile(engine, presetPath);
        if (!rep.ok) { std::cerr << "preset: " << rep.error << "\n"; return 1; }
        warnings = rep.warnings;
    }
    std::vector<int> slots;
    for (const auto& t : effects) {
        auto fx = createEffect(t);
        if (!fx) { std::cerr << "unknown effect " << t << "\n"; return 2; }
        slots.push_back(engine.addEffect(std::move(fx)));
    }
    auto target = [&](const std::string& spec, std::string& rest) -> Effect* {
        const auto bar = spec.find('|');
        int idx = std::stoi(spec.substr(0, bar)) - 1; // effect added before this --set
        rest = spec.substr(bar + 1);
        const auto dot = rest.find('.');
        const auto eq = rest.find('=');
        if (dot != std::string::npos && (eq == std::string::npos || dot < eq) && std::isdigit((unsigned char)rest[0])) {
            idx = std::stoi(rest.substr(0, dot));
            rest = rest.substr(dot + 1);
        }
        const auto order = engine.allSlotsInOrder();
        if (!slots.empty() && idx >= 0 && idx < (int)slots.size()) return engine.effect(slots[(size_t)idx]);
        if (idx >= 0 && idx < (int)order.size()) return engine.effect(order[(size_t)idx]);
        return nullptr;
    };
    auto applySet = [&](const std::string& s) {
        std::string rest;
        Effect* fx = target(s, rest);
        const auto eq = rest.find('=');
        if (!fx || eq == std::string::npos) { std::cerr << "bad --set " << rest << "\n"; std::exit(2); }
        const std::string id = rest.substr(0, eq);
        const int pi = fx->params().indexOf(id.c_str());
        if (pi < 0) { std::cerr << "unknown parameter " << id << " on " << fx->typeId() << "\n"; std::exit(2); }
        fx->params().set(pi, std::stof(rest.substr(eq + 1)));
    };
    for (const auto& s : sets) applySet(s);
    for (const auto& s : states) {
        std::string rest;
        Effect* fx = target(s, rest);
        if (!fx || !fx->setExtraState(rest, &err)) { std::cerr << "state: " << err << "\n"; return 1; }
    }
    engine.prepare(sr, block); // re-prepare: parameters applied immediately (no smoothing at t=0)

    const int latency = engine.latencySamples();
    const size_t total = frames + tailFrames + (compensate ? (size_t)latency : 0);
    std::vector<float> inL(total, 0.0f), inR(total, 0.0f), outL(total, 0.0f), outR(total, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
        inL[i] = in.channels[0][i];
        inR[i] = numIn > 1 ? in.channels[1][i] : inL[i];
    }
    const auto t0 = std::chrono::steady_clock::now();
    double worstBlock = 0.0;
    std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.sample < b.sample; });
    size_t ev = 0;
    for (size_t pos = 0, n = 0; pos < total; pos += n) {
        while (ev < events.size() && events[ev].sample <= pos) applySet(events[ev++].spec);
        size_t end = std::min(total, pos + (size_t)block);
        if (ev < events.size() && events[ev].sample < end) end = events[ev].sample;
        n = end - pos;
        const float* ip[2] = {inL.data() + pos, inR.data() + pos};
        float* op[2] = {outL.data() + pos, outR.data() + pos};
        const auto b0 = std::chrono::steady_clock::now();
        engine.process(ip, numIn, op, 2, (int)n);
        worstBlock = std::max(worstBlock, std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count() / (n / sr));
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (compensate && latency > 0) {
        outL.erase(outL.begin(), outL.begin() + latency);
        outR.erase(outR.begin(), outR.begin() + latency);
    }
    const size_t outFrames = outL.size();

    if (!outPath.empty()) {
        WavData o;
        o.sampleRate = sr;
        if (monoOut) {
            std::vector<float> m(outFrames);
            for (size_t i = 0; i < outFrames; ++i) m[i] = 0.5f * (outL[i] + outR[i]);
            o.channels = {m};
        } else o.channels = {outL, outR};
        if (!writeWav(outPath, o, bits, &err)) { std::cerr << "output: " << err << "\n"; return 1; }
    }
    if (!savePreset.empty()) savePresetFile(engine, "rendered", savePreset);

    if (!benchPath.empty()) {
        Json b = Json::object();
        b["tool"] = "mfx_render";
        b["engine"] = std::string(mfx::build::productName) + " " + mfx::build::buildId;
        b["input"] = inPath;
        b["sample_rate"] = sr;
        b["block_size"] = block;
        b["input_channels"] = numIn;
        b["frames"] = (double)frames;
        b["duration_s"] = (double)total / sr;
        b["process_time_s"] = secs;
        b["realtime_factor"] = ((double)total / sr) / secs;
        b["cpu_percent_of_realtime"] = 100.0 * secs / ((double)total / sr);
        b["worst_block_percent_of_budget"] = 100.0 * worstBlock;
        b["peak_rss_kb"] = (double)peakRssKb();
        b["latency_samples"] = latency;
        b["latency_ms"] = 1000.0 * latency / sr;
        b["latency_compensated"] = compensate;
        Json chain = Json::array();
        for (int slot : engine.allSlotsInOrder()) {
            const Effect* fx = engine.effect(slot);
            Json c = Json::object();
            c["type"] = fx->typeId();
            c["latency_samples"] = fx->latencySamples();
            const int os = fx->params().indexOf("oversampling");
            if (os >= 0) c["oversampling"] = 1 << fx->params().getInt(os);
            Json p = Json::object();
            for (int i = 0; i < fx->params().size(); ++i) p[fx->params().info(i).id] = fx->params().get(i);
            c["params"] = p;
            chain.push(c);
        }
        b["chain"] = chain;
        Json st = Json::object();
        st["peak_dbfs"] = 20.0 * std::log10(std::max(1e-12, analysis::peak(outL.data(), outFrames)));
        st["rms_dbfs"] = 20.0 * std::log10(std::max(1e-12, analysis::rms(outL.data(), outFrames)));
        st["dc_offset"] = analysis::mean(outL.data(), outFrames);
        st["crest_factor_db"] = analysis::crestFactorDb(outL.data(), outFrames);
        bool finite = true;
        for (float v : outL) if (!std::isfinite(v)) { finite = false; break; }
        st["all_finite"] = finite;
        b["output_stats_left"] = st;
        if (thdF0 > 0.0 && outFrames > 4096) {
            const size_t start = outFrames / 2, len = std::min<size_t>(outFrames - start, 65536);
            auto h = analysis::analyzeHarmonics(outL.data() + start, len, sr, thdF0);
            Json t = Json::object();
            t["f0"] = thdF0;
            t["fundamental_dbfs"] = h.fundamentalDb;
            t["thd_percent"] = 100.0 * h.thd;
            t["thd_n_percent"] = 100.0 * h.thdN;
            t["even_odd_ratio_db"] = h.evenOddRatioDb;
            t["non_harmonic_db"] = h.nonHarmonicDb;
            Json hs = Json::array();
            for (int k = 1; k <= 10; ++k) hs.push(h.harmonicsDb[k]);
            t["harmonics_db_re_fundamental"] = hs;
            b["harmonic_analysis"] = t;
        }
        Json w = Json::array();
        for (const auto& s : warnings) w.push(s);
        b["warnings"] = w;
        b.save(benchPath);
    }
    for (const auto& s : warnings) std::cerr << "warning: " << s << "\n";
    std::fprintf(stderr, "rendered %.2f s in %.3f s (%.1fx realtime), latency %d samples\n",
                 (double)total / sr, secs, ((double)total / sr) / secs, latency);
    return 0;
}
