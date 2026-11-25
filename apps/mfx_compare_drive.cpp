// mfx_compare_drive — common benchmark for the Custom Drive of this project ("claude") and of the
// reference project ("codex", read-only, compiled from MFX_REFERENCE_DIR). Optionally also the
// previous Claude mapping ("claude_v1") to measure before/after.
//
//   mfx_compare_drive <out_dir> [--fs 48000] [--quick]
//
// Two comparisons are kept separate (spec §7):
//   1. same user parameter values (knob numbers mean different DSP in the two projects!)
//   2. matched operating point: for each reference setting, the Claude Drive/Headroom pair whose
//      small-signal gain and 1 dB compression onset are closest
// Outputs: grid.csv / grid.json (Drive x Headroom x input peak), matched.json, cpu.json,
// renders/*.wav (raw + level-matched to -24 dBFS RMS), abx/ (randomised names + hidden key).
// Test signals are SYNTHETIC (sine, Karplus-Strong plucks); no real guitar DI is bundled.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "dsp/analysis/Analysis.h"
#include "dsp/drive/CustomDrive.h"
#include "platform/WavFile.h"
#include "presets/Json.h"

#if MFX_HAS_REFERENCE
#include "effects/CustomDrive.h" // reference (Codex) project, header-only
#endif

namespace fs = std::filesystem;
using namespace mfx;

struct Knobs { float drive = 25, headroom = 50, tone = 50, tight = 50, crunch = 50, grain = 50, mid = 50, level = -6; };

struct DUT {
    virtual ~DUT() = default;
    virtual const char* name() const = 0;
    virtual void prepare(double fs, int osFactor) = 0;
    virtual void set(const Knobs& k) = 0;
    virtual void process(float* x, int n) = 0;
    virtual int latency() const = 0;
};

static int gClaudeModel = CustomDrive::kModelV2;
static int gClaudeAdaaComp = 1;
struct ClaudeDUT : DUT {
    CustomDrive d;
    double fs = 48000; int os = 2;
    const char* name() const override { return "claude"; }
    void prepare(double f, int o) override { fs = f; os = o; }
    void set(const Knobs& k) override {
        d.setParam("drive", k.drive); d.setParam("headroom", k.headroom); d.setParam("tone", k.tone);
        d.setParam("tight", k.tight); d.setParam("crunch", k.crunch); d.setParam("grain", k.grain);
        d.setParam("mid", k.mid); d.setParam("level", k.level); d.setParam("mix", 100);
        d.setParam("oversampling", os == 1 ? 0.f : os == 2 ? 1.f : os == 4 ? 2.f : 3.f);
        d.setParam("model", (float)gClaudeModel);
        d.setParam("adaa_comp", (float)gClaudeAdaaComp);
        d.prepare({fs, 512, 1}); // parameters applied immediately (identical initial state)
    }
    void process(float* x, int n) override {
        for (int p = 0; p < n; p += 256) {
            AudioBlock b; b.numChannels = 1; b.numSamples = std::min(256, n - p); b.ch[0] = x + p;
            d.process(b);
        }
    }
    int latency() const override { return d.latencySamples(); }
};

#if MFX_HAS_REFERENCE
struct CodexDUT : DUT {
    multi_effects::CustomDrive d;
    multi_effects::CustomDrive::Controls c;
    double fs = 48000; int os = 2;
    const char* name() const override { return "codex"; }
    void prepare(double f, int o) override { fs = f; os = std::min(o, 4); }
    void set(const Knobs& k) override {
        c = {};
        c.drive = k.drive; c.headroom = k.headroom; c.tone = k.tone; c.tight = k.tight; c.crunch = k.crunch;
        c.grain = k.grain; c.midCharacter = k.mid; c.level = k.level; c.mix = 1.0f; // bias/symmetry default 50
        d = multi_effects::CustomDrive{};
        d.prepare(fs, os);
    }
    void process(float* x, int n) override { for (int i = 0; i < n; ++i) x[i] = d.processSample(x[i], c); }
    int latency() const override { return os == 1 ? 0 : 64; }
};
#endif

static std::vector<float> sine(double fs, double f, double peakDb, size_t n) {
    std::vector<float> x(n);
    const double a = std::pow(10.0, peakDb / 20.0);
    for (size_t i = 0; i < n; ++i) x[i] = (float)(a * std::sin(2.0 * kPi * f * (double)i / fs));
    return x;
}

/// Synthetic guitar-like phrase: plucks with varying velocity (soft/normal/hard), a "volume knob"
/// roll-off section and a 3-note chord. SYNTHETIC (Karplus-Strong) — labelled as such in outputs.
static std::vector<float> phrase(double fs, double peakDb) {
    std::vector<float> out((size_t)(fs * 6.0), 0.0f);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> U(-1.f, 1.f);
    auto pluck = [&](double t0, double f, double vel, double dur) {
        const int L = std::max(2, (int)(fs / f));
        std::vector<float> buf((size_t)L);
        for (auto& v : buf) v = U(rng);
        int p = 0;
        const size_t s0 = (size_t)(t0 * fs), n = (size_t)(dur * fs);
        for (size_t i = 0; i < n && s0 + i < out.size(); ++i) {
            const int q = (p + 1) % L;
            out[s0 + i] += (float)(vel * buf[(size_t)p]);
            buf[(size_t)p] = 0.4985f * (buf[(size_t)p] + buf[(size_t)q]);
            p = q;
        }
    };
    const double vels[] = {0.25, 0.6, 1.0, 0.25, 0.6, 1.0};
    for (int i = 0; i < 6; ++i) pluck(0.05 + 0.45 * i, i % 2 ? 110.0 : 82.41, vels[i], 1.2);
    const double vol[] = {1.0, 0.5, 0.25, 0.125}; // guitar volume roll-off (clean-up)
    for (int i = 0; i < 4; ++i) pluck(2.9 + 0.55 * i, 146.8, vol[i], 1.0);
    for (double f : {110.0, 164.8, 220.0}) pluck(5.1, f, 0.55, 0.9);
    float pk = 0; for (float v : out) pk = std::max(pk, std::fabs(v));
    const float g = (float)(std::pow(10.0, peakDb / 20.0) / pk);
    for (auto& v : out) v *= g;
    return out;
}

static double rmsDb(const float* x, size_t n) { return 20.0 * std::log10(std::max(1e-12, analysis::rms(x, n))); }

struct Point {
    double inPeakDb, outPeakDb, outRmsDb, gainDb, thd, h2, h3, h5, h7, evenOdd;
};

static Point measureSine(DUT& d, const Knobs& k, double fs, double inPeakDb, double f0 = 440.0) {
    auto x = sine(fs, f0, inPeakDb, (size_t)fs); // 1 s, analyse last 0.5 s (as the reference project)
    d.set(k);
    d.process(x.data(), (int)x.size());
    const size_t a = x.size() / 2, n = x.size() / 2;
    const auto h = analysis::analyzeHarmonics(x.data() + a, std::min<size_t>(n, 16384), fs, f0);
    Point p;
    p.inPeakDb = inPeakDb;
    p.outPeakDb = 20.0 * std::log10(std::max(1e-12, analysis::peak(x.data() + a, n)));
    p.outRmsDb = rmsDb(x.data() + a, n);
    p.gainDb = p.outRmsDb - (inPeakDb - 3.0103);
    p.thd = h.thd; p.h2 = h.harmonicsDb[2]; p.h3 = h.harmonicsDb[3]; p.h5 = h.harmonicsDb[5]; p.h7 = h.harmonicsDb[7];
    p.evenOdd = h.evenOddRatioDb;
    return p;
}

static double smallSignalGain(DUT& d, const Knobs& k, double fs) { return measureSine(d, k, fs, -80.0).gainDb; }

static double onset1dB(DUT& d, const Knobs& k, double fs) {
    const double g0 = smallSignalGain(d, k, fs);
    for (double in = -70.0; in <= 0.0; in += 1.0)
        if (g0 - measureSine(d, k, fs, in).gainDb > 1.0) return in;
    return 1.0; // no compression up to 0 dBFS
}

/// Audible-band aliasing: non-harmonic energy below 16 kHz relative to the fundamental (3953 Hz is
/// not a divisor of 48 kHz, so aliases fall between harmonics).
static double aliasDb(DUT& d, const Knobs& k, double fs, double inPeakDb) {
    auto x = sine(fs, 3953.0, inPeakDb, 65536 + 8192);
    d.set(k);
    d.process(x.data(), (int)x.size());
    size_t N = 0;
    const auto mag = analysis::spectrum(x.data() + 8192, 65536, &N);
    const double bin = fs / (double)N;
    double e1 = 0, eNon = 0;
    for (size_t b = 1; b < mag.size(); ++b) {
        const double f = b * bin;
        if (f > 16000.0) break;
        const double harm = f / 3953.0, dist = std::fabs(harm - std::round(harm)) * 3953.0;
        const double e = mag[b] * mag[b];
        if (std::round(harm) == 1 && dist < 6 * bin) e1 += e;
        else if (dist >= 6 * bin && f > 30.0) eNon += e;
    }
    return 10.0 * std::log10((eNon + 1e-30) / (e1 + 1e-30));
}

/// Distortion residual on a musical phrase: ESR between the output and the same phrase rendered at
/// -60 dB (linear reference) rescaled, after RMS matching. Also short-term gain compression.
struct PhraseStats { double residualPct, attackGrDb, sustainGrDb, cleanupResidualPct, outRmsDb, outPeakDb; };
static PhraseStats measurePhrase(DUT& d, const Knobs& k, double fs, double peakDb, std::vector<float>* outWav) {
    auto x = phrase(fs, peakDb);
    auto lin = x;
    const float s = (float)std::pow(10.0, -60.0 / 20.0);
    for (auto& v : lin) v *= s;
    auto y = x;
    d.set(k); d.process(y.data(), (int)y.size());
    d.set(k); d.process(lin.data(), (int)lin.size());
    for (auto& v : lin) v /= s;
    const int L = d.latency();
    // align nothing: both renders have identical latency
    auto resid = [&](size_t a, size_t n) {
        const double ry = analysis::rms(y.data() + a, n), rl = analysis::rms(lin.data() + a, n);
        double num = 0, den = 0;
        for (size_t i = a; i < a + n; ++i) { const double yy = y[i] / ry, ll = lin[i] / rl; num += (yy - ll) * (yy - ll); den += ll * ll; }
        return 100.0 * std::sqrt(num / den);
    };
    PhraseStats st{};
    st.residualPct = resid(0, (size_t)(2.8 * fs));
    st.cleanupResidualPct = resid((size_t)(4.55 * fs), (size_t)(0.5 * fs)); // quietest roll-off note
    // short-term gain compression for the hard pick at 0.95 s: first 15 ms vs 250..300 ms
    const size_t t0 = (size_t)(0.95 * fs) + (size_t)L;
    auto gr = [&](size_t a, size_t n) { return rmsDb(y.data() + a, n) - rmsDb(lin.data() + a, n); };
    st.attackGrDb = gr(t0, (size_t)(0.015 * fs));
    st.sustainGrDb = gr(t0 + (size_t)(0.25 * fs), (size_t)(0.05 * fs));
    st.outRmsDb = rmsDb(y.data(), y.size());
    st.outPeakDb = 20.0 * std::log10(std::max(1e-12, analysis::peak(y.data(), y.size())));
    if (outWav) *outWav = y;
    return st;
}

static double cpuNsPerSample(DUT& d, const Knobs& k, double fs) {
    auto x = sine(fs, 440.0, -6.0, (size_t)(fs * 4));
    d.set(k);
    const auto t0 = std::chrono::steady_clock::now();
    d.process(x.data(), (int)x.size());
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)x.size();
}

static void writeWav(const fs::path& p, double sr, std::vector<float> y, double targetRmsDb = 0.0) {
    if (targetRmsDb < 0.0) {
        const double g = std::pow(10.0, (targetRmsDb - rmsDb(y.data(), y.size())) / 20.0);
        for (auto& v : y) v = (float)(v * g);
    }
    WavData w; w.sampleRate = sr; w.channels = {y};
    writeWav(p.string(), w, 32);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("usage: mfx_compare_drive <out_dir> [--fs 48000] [--quick]"); return 2; }
    const fs::path out = argv[1];
    double sr = 48000.0;
    bool quick = false, claudeOnly = false;
    int osMain = 2;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--fs" && i + 1 < argc) sr = std::stod(argv[++i]);
        else if (a == "--quick") quick = true;
        else if (a == "--claude-only") claudeOnly = true;
        else if (a == "--os" && i + 1 < argc) osMain = std::atoi(argv[++i]);
        else if (a == "--claude-model" && i + 1 < argc) gClaudeModel = std::atoi(argv[++i]); // 0 v1, 1 v2, 2 v2+ADAA
        else if (a == "--claude-adaa-comp" && i + 1 < argc) gClaudeAdaaComp = std::atoi(argv[++i]);
    }
    fs::create_directories(out / "renders");
    fs::create_directories(out / "abx");

    std::vector<std::unique_ptr<DUT>> duts;
    duts.push_back(std::make_unique<ClaudeDUT>());
#if MFX_HAS_REFERENCE
    if (!claudeOnly) duts.push_back(std::make_unique<CodexDUT>());
#endif
    for (auto& d : duts) d->prepare(sr, osMain);

    const float drives[] = {0, 25, 50, 75, 100};
    const float heads[] = {0, 25, 50, 75, 100};
    const double peaks[] = {-60, -48, -36, -24, -18, -12, -7.2, -6, -3};
    Json report = Json::object();
    report["sample_rate"] = sr;
    report["oversampling"] = osMain;
    report["signals"] = "440 Hz sine (1 s, last 0.5 s analysed); synthetic Karplus-Strong phrase; 3953 Hz sine for aliasing";
    report["defaults"] = "Tone/Tight/Crunch/Grain/Mid 50, Mix 100 %, Level -6 dB (reference: Bias/Symmetry 50)";

    // ---------------------------------------------------------------- 1. same user parameters
    FILE* csv = std::fopen((out / "grid.csv").string().c_str(), "w");
    std::fprintf(csv, "engine,drive,headroom,in_peak_dbfs,out_peak_dbfs,out_rms_dbfs,gain_db,thd_pct,h2_db,h3_db,h5_db,h7_db,even_odd_db\n");
    Json summary = Json::array();
    for (auto& d : duts) {
        for (float dr : drives)
            for (float hd : heads) {
                Knobs k; k.drive = dr; k.headroom = hd;
                for (double pk : peaks) {
                    if (quick && pk != -7.2 && pk != -24 && pk != -60) continue;
                    const Point p = measureSine(*d, k, sr, pk);
                    std::fprintf(csv, "%s,%.0f,%.0f,%.1f,%.2f,%.2f,%.2f,%.3f,%.1f,%.1f,%.1f,%.1f,%.1f\n", d->name(), dr, hd, pk,
                                 p.outPeakDb, p.outRmsDb, p.gainDb, 100 * p.thd, p.h2, p.h3, p.h5, p.h7, p.evenOdd);
                }
                Json s = Json::object();
                s["engine"] = d->name(); s["drive"] = dr; s["headroom"] = hd;
                s["small_signal_gain_db"] = smallSignalGain(*d, k, sr);
                s["onset_1db_dbfs"] = onset1dB(*d, k, sr);
                s["thd_pct_at_-7.2dBFS"] = 100 * measureSine(*d, k, sr, -7.2).thd;
                s["out_rms_dbfs_at_-7.2dBFS"] = measureSine(*d, k, sr, -7.2).outRmsDb;
                s["alias_db_at_-7.2dBFS"] = aliasDb(*d, k, sr, -7.2);
                const auto ph = measurePhrase(*d, k, sr, -7.2, nullptr);
                s["phrase_residual_pct"] = ph.residualPct;
                s["cleanup_residual_pct"] = ph.cleanupResidualPct;
                s["attack_gain_change_db"] = ph.attackGrDb;
                s["sustain_gain_change_db"] = ph.sustainGrDb;
                s["phrase_out_rms_dbfs"] = ph.outRmsDb;
                summary.push(s);
                std::fprintf(stderr, "%s D%.0f H%.0f\n", d->name(), dr, hd);
            }
    }
    std::fclose(csv);
    report["same_parameter_summary"] = summary;

    // ---------------------------------------------------------------- 2. matched operating points
    Json matched = Json::array();
#if MFX_HAS_REFERENCE
    if (duts.size() > 1) {
        DUT& ref = *duts[1];
        DUT& cl = *duts[0];
        // characterise Claude on a grid once
        struct G { float d, h; double g, o; };
        std::vector<G> grid;
        for (float dr = 0; dr <= 100; dr += (quick ? 10.f : 5.f))
            for (float hd = 0; hd <= 100; hd += (quick ? 25.f : 12.5f)) {
                Knobs k; k.drive = dr; k.headroom = hd;
                grid.push_back({dr, hd, smallSignalGain(cl, k, sr), onset1dB(cl, k, sr)});
            }
        for (float dr : drives)
            for (float hd : {0.f, 50.f, 100.f}) {
                Knobs kr; kr.drive = dr; kr.headroom = hd;
                const double gr = smallSignalGain(ref, kr, sr), orr = onset1dB(ref, kr, sr);
                const G* best = &grid[0];
                for (const auto& g : grid)
                    if (std::fabs(g.g - gr) + std::fabs(g.o - orr) < std::fabs(best->g - gr) + std::fabs(best->o - orr)) best = &g;
                Knobs kc; kc.drive = best->d; kc.headroom = best->h;
                Json m = Json::object();
                m["codex_drive"] = dr; m["codex_headroom"] = hd;
                m["claude_drive"] = best->d; m["claude_headroom"] = best->h;
                m["codex_small_signal_gain_db"] = gr; m["claude_small_signal_gain_db"] = best->g;
                m["codex_onset_dbfs"] = orr; m["claude_onset_dbfs"] = best->o;
                for (auto* d : {&ref, &cl}) {
                    const Knobs& k = d == &ref ? kr : kc;
                    const std::string nm = d->name();
                    const Point p = measureSine(*d, k, sr, -7.2);
                    m[nm + "_thd_pct"] = 100 * p.thd;
                    m[nm + "_h2_h3_h5_h7_db"] = Json::array();
                    Json hs = Json::array(); hs.push(p.h2); hs.push(p.h3); hs.push(p.h5); hs.push(p.h7);
                    m[nm + "_h2_h3_h5_h7_db"] = hs;
                    m[nm + "_alias_db"] = aliasDb(*d, k, sr, -7.2);
                    std::vector<float> wav;
                    const auto ph = measurePhrase(*d, k, sr, -7.2, &wav);
                    m[nm + "_phrase_residual_pct"] = ph.residualPct;
                    m[nm + "_attack_gain_change_db"] = ph.attackGrDb;
                    m[nm + "_cleanup_residual_pct"] = ph.cleanupResidualPct;
                    char base[128];
                    std::snprintf(base, sizeof(base), "codexD%.0fH%.0f_%s", dr, hd, nm.c_str());
                    writeWav(out / "renders" / (std::string(base) + "_raw.wav"), sr, wav);
                    writeWav(out / "renders" / (std::string(base) + "_matched-24dBFS.wav"), sr, wav, -24.0);
                }
                matched.push(m);
            }
    }
#endif
    report["matched_operating_points"] = matched;

    // ---------------------------------------------------------------- CPU (same machine, same signal)
    Json cpu = Json::object();
    for (auto& d : duts)
        for (int os : {1, 2, 4}) {
            d->prepare(sr, os);
            Knobs k; k.drive = 50;
            cpu[std::string(d->name()) + "_" + std::to_string(os) + "x_ns_per_sample"] = cpuNsPerSample(*d, k, sr);
            cpu[std::string(d->name()) + "_" + std::to_string(os) + "x_latency_samples"] = d->latency();
        }
    report["cpu_mono"] = cpu;

    // ---------------------------------------------------------------- ABX set from matched renders
#if MFX_HAS_REFERENCE
    if (duts.size() > 1) {
        std::mt19937 rng(7);
        Json key = Json::array();
        int idx = 0;
        for (const auto& m : matched.elements()) {
            char base[128];
            std::snprintf(base, sizeof(base), "codexD%.0fH%.0f", m.at("codex_drive").asNumber(), m.at("codex_headroom").asNumber());
            const fs::path a = out / "renders" / (std::string(base) + "_codex_matched-24dBFS.wav");
            const fs::path b = out / "renders" / (std::string(base) + "_claude_matched-24dBFS.wav");
            const bool swap = rng() & 1u, xIsA = rng() & 1u;
            char dir[32];
            std::snprintf(dir, sizeof(dir), "trial_%02d", ++idx);
            fs::create_directories(out / "abx" / dir);
            fs::copy_file(swap ? b : a, out / "abx" / dir / "A.wav", fs::copy_options::overwrite_existing);
            fs::copy_file(swap ? a : b, out / "abx" / dir / "B.wav", fs::copy_options::overwrite_existing);
            fs::copy_file(xIsA ? (swap ? b : a) : (swap ? a : b), out / "abx" / dir / "X.wav", fs::copy_options::overwrite_existing);
            Json kk = Json::object();
            kk["trial"] = dir; kk["setting"] = base;
            kk["A"] = swap ? "claude" : "codex"; kk["B"] = swap ? "codex" : "claude"; kk["X"] = xIsA ? "A" : "B";
            key.push(kk);
        }
        key.save((out / "abx_KEY_do_not_open_before_listening.json").string());
    }
#endif
    report.save((out / "report.json").string());
    std::printf("wrote %s\n", (out / "report.json").string().c_str());
    return 0;
}
