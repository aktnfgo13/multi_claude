// Common types and helpers shared by every layer. No platform / framework dependencies.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mfx {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kMaxChannels = 2;

struct ProcessSpec {
    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    int numChannels = 2;
};

/// Non-owning view over up to kMaxChannels planar float buffers.
struct AudioBlock {
    float* ch[kMaxChannels] = {nullptr, nullptr};
    int numChannels = 0;
    int numSamples = 0;

    float* channel(int c) const { return ch[c]; }
    AudioBlock subBlock(int start, int len) const {
        AudioBlock b;
        b.numChannels = numChannels;
        b.numSamples = len;
        for (int c = 0; c < numChannels; ++c) b.ch[c] = ch[c] + start;
        return b;
    }
};

inline float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }
inline double dbToGainD(double db) { return std::pow(10.0, db * 0.05); }
inline float gainToDb(float g, float floorDb = -150.0f) {
    return g > 0.0f ? std::max(floorDb, 20.0f * std::log10(g)) : floorDb;
}

template <typename T>
inline T clampv(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Map 0..1 to [lo, hi] logarithmically (lo, hi > 0).
inline double mapLog(double n, double lo, double hi) { return lo * std::pow(hi / lo, n); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

/// Coefficient of a one-pole smoother reaching ~63% in `seconds`.
inline float onePoleCoeff(double seconds, double sampleRate) {
    if (seconds <= 0.0) return 0.0f;
    return (float)std::exp(-1.0 / (seconds * sampleRate));
}

inline float flushDenorm(float x) { return std::fabs(x) < 1e-15f ? 0.0f : x; }

inline bool isFiniteSample(float x) { return std::isfinite(x); }

/// Scoped flush-to-zero / denormals-are-zero on x86 (no-op elsewhere; ARM64 handles via FPCR in platform layer).
class ScopedNoDenormals {
public:
    ScopedNoDenormals();
    ~ScopedNoDenormals();
private:
    unsigned int saved_ = 0;
};

} // namespace mfx
