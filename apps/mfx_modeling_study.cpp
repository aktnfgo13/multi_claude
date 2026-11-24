// Clipper modelling comparison study (spec §10):
//   (1) mathematical clipping (tanh)              (2) static Shockley diode pair (Newton)
//   (3) RC + diode pair ODE (trapezoidal+Newton)  (4) Wave Digital Filter diode clipper
// Circuit for (2)-(4): Vin -- R(2.2k) --+-- C(10nF) -- gnd,  antiparallel 1N4148-like diodes to gnd.
// Reference: model (3) run at 16x oversampling in double precision.
// Output: JSON with accuracy (ESR vs reference), THD, aliasing (non-harmonic energy) and CPU cost,
// plus WAV files for SPICE comparison (research/spice/compare_clipper.py).
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "dsp/analysis/Analysis.h"
#include "dsp/nonlinear/Nonlinear.h"
#include "dsp/oversampling/Oversampler.h"
#include "platform/WavFile.h"
#include "presets/Json.h"

using namespace mfx;

static std::vector<double> sine(double fs, double f, double amp, double secs) {
    std::vector<double> x((size_t)(fs * secs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = amp * std::sin(2.0 * kPi * f * (double)i / fs);
    return x;
}

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : "audio/output/modeling_study";
    const double fs = 48000.0;
    const double R = 2200.0, C = 10e-9;
    nl::DiodeParams dp;
    Json report = Json::object();
    report["circuit"] = "Vin-R(2.2k)-node; node-C(10nF)-gnd; antiparallel diodes Is=2.52nA n=1.752";
    report["sample_rate"] = fs;
    Json cases = Json::array();

    struct Model { std::string name; std::function<std::vector<double>(const std::vector<double>&, double)> run; };
    // each model runs at rate fsRun on an already oversampled input
    std::vector<Model> models = {
        {"tanh", [&](const std::vector<double>& x, double) {
             std::vector<double> y(x.size());
             const double a = 0.6; // scaled to similar clipping level as the diode pair
             for (size_t i = 0; i < x.size(); ++i) y[i] = a * std::tanh(x[i] / a);
             return y; }},
        {"diode_static_newton", [&](const std::vector<double>& x, double) {
             nl::StaticDiodeClipper c; c.setParams(R, dp);
             std::vector<double> y(x.size());
             for (size_t i = 0; i < x.size(); ++i) y[i] = c.process(x[i]);
             return y; }},
        {"rc_diode_ode", [&](const std::vector<double>& x, double f) {
             nl::RCDiodeClipper c; c.prepare(f); c.setParams(R, C, dp);
             std::vector<double> y(x.size());
             for (size_t i = 0; i < x.size(); ++i) y[i] = c.process(x[i]);
             return y; }},
        {"wdf_diode", [&](const std::vector<double>& x, double f) {
             nl::WDFDiodeClipper c; c.prepare(f); c.setParams(R, C, dp);
             std::vector<double> y(x.size());
             for (size_t i = 0; i < x.size(); ++i) y[i] = c.process(x[i]);
             return y; }},
    };

    // simple (ideal-ish) up/down via the product oversampler, float path
    auto runOversampled = [&](const Model& m, const std::vector<double>& x, int osIdx, double& nsPerSample) {
        Oversampler os; os.prepare(256, 1); os.setFactorIndex(osIdx); os.reset();
        const int F = 1 << osIdx;
        std::vector<float> blockIn(256), out(x.size());
        std::vector<double> up; up.reserve(x.size() * (size_t)F);
        // upsample everything first
        std::vector<float> upAll;
        for (size_t p = 0; p < x.size(); p += 256) {
            const int n = (int)std::min<size_t>(256, x.size() - p);
            for (int i = 0; i < n; ++i) blockIn[(size_t)i] = (float)x[p + (size_t)i];
            float* u = os.upsample(blockIn.data(), n, 0);
            upAll.insert(upAll.end(), u, u + n * F);
        }
        std::vector<double> ud(upAll.begin(), upAll.end());
        const auto t0 = std::chrono::steady_clock::now();
        auto yd = m.run(ud, fs * F);
        nsPerSample = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)x.size();
        Oversampler os2; os2.prepare(256, 1); os2.setFactorIndex(osIdx); os2.reset();
        // downsample: feed through the second oversampler's internal buffer
        size_t q = 0;
        for (size_t p = 0; p < x.size(); p += 256) {
            const int n = (int)std::min<size_t>(256, x.size() - p);
            float* u = os2.upsample(blockIn.data(), n, 0); // obtain buffer pointer of correct size
            for (int i = 0; i < n * F; ++i) u[i] = (float)yd[q++];
            os2.downsample(out.data() + p, n, 0);
        }
        return std::vector<double>(out.begin(), out.end());
    };

    const double levels[] = {0.1, 0.5, 2.0, 8.0};
    // frequencies that do NOT divide fs, so aliases fall between harmonics and are measurable
    const double freqs[] = {110.0, 1031.0, 3953.0};
    for (double f0 : freqs) {
        for (double amp : levels) {
            const auto x = sine(fs, f0, amp, 0.6);
            // reference: RC ODE at 16x via 8x oversampler + double run (closest to SPICE)
            double dummy;
            const auto ref = runOversampled(models[2], x, 3, dummy);
            for (const auto& m : models) {
                for (int osIdx : {0, 2}) {
                    double ns = 0.0;
                    const auto y = runOversampled(m, x, osIdx, ns);
                    const size_t lat = (size_t)Oversampler::latencyFor(osIdx), latRef = (size_t)Oversampler::latencyFor(3);
                    const size_t start = 12000, len = 16384;
                    std::vector<float> yf(len), rf(len);
                    for (size_t i = 0; i < len; ++i) { yf[i] = (float)y[start + i + lat]; rf[i] = (float)ref[start + i + latRef]; }
                    const auto h = analysis::analyzeHarmonics(yf.data(), len, fs, f0);
                    Json c = Json::object();
                    c["model"] = m.name; c["f0"] = f0; c["input_amp_v"] = amp; c["oversampling"] = 1 << osIdx;
                    c["thd_percent"] = 100.0 * h.thd;
                    c["non_harmonic_db"] = h.nonHarmonicDb;
                    c["esr_vs_reference"] = m.name == "tanh" ? -1.0 : analysis::esr(rf.data(), yf.data(), len);
                    c["ns_per_base_sample"] = ns;
                    cases.push(c);
                }
            }
        }
    }
    report["cases"] = cases;

    // export a sweep through all models for SPICE comparison
    std::string err;
    {
        const double secs = 1.0;
        std::vector<double> x((size_t)(fs * secs));
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = (double)i / fs;
            const double env = 4.0 * t / secs; // 0..4 V amplitude ramp
            x[i] = env * std::sin(2.0 * kPi * 220.0 * t);
        }
        WavData w; w.sampleRate = fs;
        std::vector<float> xf(x.begin(), x.end());
        w.channels = {xf};
        // stored with 1 V = 0.25 FS (WAV float holds > 1 anyway)
        writeWav(outDir + "_input.wav", w, 32, &err);
        for (const auto& m : models) {
            double ns;
            auto y = runOversampled(m, x, 2, ns);
            const size_t lat = (size_t)Oversampler::latencyFor(2);
            std::vector<float> yf(x.size(), 0.0f);
            for (size_t i = 0; i + lat < y.size(); ++i) yf[i] = (float)y[i + lat];
            WavData o; o.sampleRate = fs; o.channels = {yf};
            writeWav(outDir + "_" + m.name + ".wav", o, 32, &err);
        }
    }
    report.save(outDir + ".json");
    std::printf("wrote %s.json (%zu cases)\n", outDir.c_str(), cases.size());
    return 0;
}
