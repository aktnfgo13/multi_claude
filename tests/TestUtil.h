// Helpers shared by the tests: signal generators and an offline "host" for single effects.
#pragma once

#include <memory>
#include <vector>

#include "core/Effect.h"
#include "dsp/analysis/Analysis.h"

namespace mfxtest {

inline std::vector<float> makeSine(double fs, double f, double amp, size_t n, double phase = 0.0) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = (float)(amp * std::sin(2.0 * mfx::kPi * f * (double)i / fs + phase));
    return x;
}

inline std::vector<float> makeNoise(size_t n, double amp, uint32_t seed = 1234) {
    std::vector<float> x(n);
    uint32_t s = seed;
    for (auto& v : x) { s = s * 1664525u + 1013904223u; v = (float)(amp * ((double)(s >> 8) / 8388608.0 - 1.0)); }
    return x;
}

inline std::vector<float> makeImpulse(size_t n, float amp = 1.0f) { std::vector<float> x(n, 0.0f); x[0] = amp; return x; }

/// Synthetic plucked-string "guitar DI" (Karplus-Strong). SYNTHETIC — not a recording.
inline std::vector<float> makePluck(double fs, double f, double secs, double amp = 0.3, uint32_t seed = 7) {
    const size_t n = (size_t)(fs * secs);
    const int L = std::max(2, (int)(fs / f));
    std::vector<float> buf((size_t)L), out(n);
    auto noise = makeNoise((size_t)L, 1.0, seed);
    for (int i = 0; i < L; ++i) buf[(size_t)i] = noise[(size_t)i];
    int p = 0;
    for (size_t i = 0; i < n; ++i) {
        const int q = (p + 1) % L;
        const float y = 0.498f * (buf[(size_t)p] + buf[(size_t)q]);
        out[i] = (float)(amp * buf[(size_t)p]);
        buf[(size_t)p] = y;
        p = q;
    }
    return out;
}

/// Process a mono or stereo signal through one effect with a given host block size.
inline void runEffect(mfx::Effect& fx, std::vector<float>& l, std::vector<float>* r, int block) {
    const size_t n = l.size();
    for (size_t pos = 0; pos < n; pos += (size_t)block) {
        mfx::AudioBlock b;
        b.numSamples = (int)std::min<size_t>((size_t)block, n - pos);
        b.numChannels = r ? 2 : 1;
        b.ch[0] = l.data() + pos;
        b.ch[1] = r ? r->data() + pos : nullptr;
        fx.process(b);
    }
}

inline std::vector<float> processMono(mfx::Effect& fx, std::vector<float> x, int block = 128) {
    runEffect(fx, x, nullptr, block);
    return x;
}

inline bool allFinite(const std::vector<float>& x) {
    for (float v : x) if (!std::isfinite(v)) return false;
    return true;
}

inline double db(double lin) { return 20.0 * std::log10(std::max(lin, 1e-30)); }

} // namespace mfxtest
