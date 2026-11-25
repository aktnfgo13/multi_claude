// Premium Plate v1 (reverb_plate_premium): decay accuracy, frequency-dependent decay, echo density,
// stereo decorrelation, tail stability, bypass trails.
#include <cmath>

#include "TestFramework.h"
#include "TestUtil.h"
#include "dsp/reverb/PremiumPlate.h"
#include "engine/EffectFactory.h"
#include "engine/Engine.h"

using namespace mfx;
using namespace mfxtest;

namespace {
struct Stereo { std::vector<float> l, r; };
Stereo impulse(PremiumPlate& p, double fs, double secs) {
    p.prepare({fs, 256, 2});
    Stereo s{std::vector<float>((size_t)(fs * secs), 0.0f), {}};
    s.l[10] = 1.0f;
    s.r = s.l;
    runEffect(p, s.l, &s.r, 256);
    return s;
}
// Schroeder EDC, T20 fit (-5..-25 dB)
double rt60(const std::vector<float>& y, double fs) {
    std::vector<double> e(y.size());
    double acc = 0;
    for (size_t i = y.size(); i-- > 0;) { acc += (double)y[i] * y[i]; e[i] = acc; }
    size_t a = 0, b = 0;
    for (size_t i = 0; i < e.size(); ++i) { const double d = 10 * std::log10(e[i] / e[0] + 1e-30); if (!a && d < -5) a = i; if (d < -25) { b = i; break; } }
    if (b <= a) return NAN;
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = (double)(b - a);
    for (size_t i = a; i < b; ++i) { const double x = i / fs, d = 10 * std::log10(e[i] / e[0] + 1e-30); sx += x; sy += d; sxx += x * x; sxy += x * d; }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return -60.0 / slope;
}
std::vector<float> onePoleBand(const std::vector<float>& x, double fs, double fc, bool low) {
    std::vector<float> y(x.size());
    const double a = std::exp(-2 * 3.14159265358979 * fc / fs);
    double z1 = 0, z2 = 0;
    for (size_t i = 0; i < x.size(); ++i) { z1 = x[i] + a * (z1 - x[i]); z2 = z1 + a * (z2 - z1); y[i] = (float)(low ? z2 : x[i] - z2); }
    return y;
}
} // namespace

TEST(audio, premium_plate_rt60_tracks_decay_parameter) {
    for (float decay : {1.0f, 3.0f}) {
        PremiumPlate p;
        p.setParam("decay_s", decay); p.setParam("mix", 100.f); p.setParam("early", 0.f);
        p.setParam("predelay_ms", 0.f); p.setParam("bass_mult", 1.0f); p.setParam("damping_hz", 18000.f);
        auto s = impulse(p, 48000, decay * 3.0 + 1.0);
        const double rt = rt60(s.l, 48000);
        record("premium_plate_rt60_set" + std::to_string((int)decay), rt, "s");
        CHECK_MSG(std::fabs(rt - decay) < 0.2 * decay, "decay " << decay << " measured " << rt);
    }
}

TEST(audio, premium_plate_frequency_dependent_decay) {
    PremiumPlate p;
    p.setParam("decay_s", 3.f); p.setParam("mix", 100.f); p.setParam("early", 0.f); p.setParam("predelay_ms", 0.f);
    p.setParam("bass_mult", 2.0f); p.setParam("crossover_hz", 300.f); p.setParam("damping_hz", 2500.f); p.setParam("lowcut_hz", 20.f);
    auto s = impulse(p, 48000, 12.0);
    const double lo = rt60(onePoleBand(s.l, 48000, 150, true), 48000);
    const double hi = rt60(onePoleBand(s.l, 48000, 4000, false), 48000);
    const double mid = rt60(s.l, 48000);
    record("premium_plate_rt60_low", lo, "s");
    record("premium_plate_rt60_high", hi, "s");
    CHECK_MSG(lo > 1.3 * mid && hi < 0.8 * mid, "low " << lo << " mid " << mid << " high " << hi);
}

TEST(audio, premium_plate_density_decorrelation_and_stability) {
    PremiumPlate p;
    p.setParam("mix", 100.f); p.setParam("early", 0.f); p.setParam("predelay_ms", 0.f);
    auto s = impulse(p, 48000, 3.0);
    // normalised echo density (Abel & Huang) at 400 ms, 20 ms window
    const size_t c = 19200, n = 960;
    double m = 0, v = 0;
    for (size_t i = c - n / 2; i < c + n / 2; ++i) m += s.l[i];
    m /= n;
    for (size_t i = c - n / 2; i < c + n / 2; ++i) v += (s.l[i] - m) * (s.l[i] - m);
    const double sd = std::sqrt(v / n);
    double cnt = 0;
    for (size_t i = c - n / 2; i < c + n / 2; ++i) cnt += std::fabs(s.l[i] - m) > sd;
    const double ned = cnt / n / 0.3173;
    record("premium_plate_ned_400ms", ned, "");
    CHECK(ned > 0.8);
    // L/R correlation of the late tail
    double sl = 0, sr = 0, slr = 0;
    for (size_t i = 14400; i < 62400 && i < s.l.size(); ++i) { sl += s.l[i] * s.l[i]; sr += s.r[i] * s.r[i]; slr += s.l[i] * s.r[i]; }
    const double corr = slr / std::sqrt(sl * sr);
    record("premium_plate_lr_corr", corr, "");
    CHECK(std::fabs(corr) < 0.3);
    // tail stability at extreme settings: 1 s noise, then silence; energy must fall, stay finite
    PremiumPlate q;
    q.setParam("decay_s", 20.f); q.setParam("size", 150.f); q.setParam("mod_depth", 100.f); q.setParam("mod_rate_hz", 3.f);
    q.setParam("bass_mult", 2.f); q.setParam("damping_hz", 18000.f); q.setParam("mix", 100.f); q.setParam("diffusion", 100.f);
    q.prepare({48000, 256, 2});
    std::vector<float> l(48000 * 12, 0.0f);
    auto nz = makeNoise(48000, 0.5, 3);
    std::copy(nz.begin(), nz.end(), l.begin());
    std::vector<float> r = l;
    runEffect(q, l, &r, 256);
    auto e = [&](size_t a, size_t b) { double s2 = 0; for (size_t i = a; i < b; ++i) s2 += (double)l[i] * l[i]; return s2; };
    CHECK(allFinite(l) && allFinite(r));
    CHECK(e(48000 * 11, 48000 * 12) < e(48000 * 2, 48000 * 3));
}

TEST(audio, premium_plate_bypass_keeps_trails_and_memory_is_bounded) {
    Engine e;
    e.prepare(48000, 256);
    auto fx = createEffect("reverb_plate_premium");
    fx->setParam("mix", 100.f);
    e.addEffect(std::move(fx));
    const int slot = e.allSlotsInOrder()[0];
    std::vector<float> l(256), r(256);
    auto burst = makeNoise(256, 0.5, 4);
    double tail = 0;
    for (int b = 0; b < 200; ++b) {
        static const std::vector<float> zero(256, 0.0f);
        const float* in[1] = {b < 20 ? burst.data() : zero.data()};
        float* out[2] = {l.data(), r.data()};
        if (b == 20) e.setBypass(slot, true);
        e.process(in, 1, out, 2, 256);
        if (b > 30 && b < 60) for (float v : l) tail += (double)v * v;
    }
    CHECK(tail > 1e-6); // the tail continues after bypass (trails)
    PremiumPlate p;
    p.prepare({192000, 512, 2});
    record("premium_plate_memory_192k", (double)p.memoryBytes() / 1024.0, "KiB");
    CHECK(p.memoryBytes() < 4u * 1024u * 1024u);
}
