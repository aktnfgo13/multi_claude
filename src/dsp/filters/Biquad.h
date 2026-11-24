// RBJ "Audio EQ Cookbook" biquads + bilinear first-order sections.
// Coefficients are designed in double precision from *physical* parameters (f0, Q, gain dB)
// and processed in float, Transposed Direct Form II.
// The same formulas are mirrored in research/common/mfx_dsp.py for Python/C++ parity.
#pragma once

#include "core/Common.h"

namespace mfx {

enum class FilterType {
    LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf, AllPass,
    LowPass1, HighPass1, AllPass1, LowShelf1, HighShelf1
};

struct BiquadCoeffs {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

/// Design. f0 is clamped to (1 Hz, 0.499 fs). Q > 0. gainDb used by Peak / shelves.
BiquadCoeffs designBiquad(FilterType type, double fs, double f0, double Q = 0.7071, double gainDb = 0.0);

/// Magnitude (linear) of the designed filter at frequency f (double precision evaluation).
double biquadMagnitude(const BiquadCoeffs& c, double fs, double f);

/// Stability check (poles inside unit circle).
bool biquadIsStable(const BiquadCoeffs& c);

class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) { c_ = c; }
    const BiquadCoeffs& coeffs() const { return c_; }
    void design(FilterType t, double fs, double f0, double Q = 0.7071, double gainDb = 0.0) {
        c_ = designBiquad(t, fs, f0, Q, gainDb);
    }
    void reset() {
        for (int c = 0; c < kMaxChannels; ++c) z1_[c] = z2_[c] = 0.0f;
    }
    inline float process(float x, int ch) {
        const float y = c_.b0 * x + z1_[ch];
        z1_[ch] = c_.b1 * x - c_.a1 * y + z2_[ch];
        z2_[ch] = c_.b2 * x - c_.a2 * y;
        return y;
    }
    void process(float* data, int n, int ch) {
        for (int i = 0; i < n; ++i) data[i] = process(data[i], ch);
    }
    void processBlock(AudioBlock& b) {
        for (int c = 0; c < b.numChannels; ++c) process(b.ch[c], b.numSamples, c);
    }

private:
    BiquadCoeffs c_;
    float z1_[kMaxChannels] = {0, 0};
    float z2_[kMaxChannels] = {0, 0};
};

/// DC blocker: y = x - x1 + R*y1 (R from cutoff).
class DCBlocker {
public:
    void prepare(double fs, double cutoffHz = 8.0) { R_ = (float)std::exp(-kTwoPi * cutoffHz / fs); reset(); }
    void reset() { for (int c = 0; c < kMaxChannels; ++c) x1_[c] = y1_[c] = 0.0f; }
    inline float process(float x, int ch) {
        const float y = x - x1_[ch] + R_ * y1_[ch];
        x1_[ch] = x;
        y1_[ch] = y;
        return y;
    }
private:
    float R_ = 0.999f;
    float x1_[kMaxChannels] = {0, 0}, y1_[kMaxChannels] = {0, 0};
};

/// One-pole lowpass (impulse-invariant), used for envelopes and in-loop damping.
class OnePoleLP {
public:
    void setCutoff(double fs, double fc) { a_ = (float)std::exp(-kTwoPi * fc / fs); }
    void setCoeff(float a) { a_ = a; }
    void reset(float v = 0.0f) { for (int c = 0; c < kMaxChannels; ++c) z_[c] = v; }
    inline float process(float x, int ch) { z_[ch] = x + a_ * (z_[ch] - x); return z_[ch]; }
private:
    float a_ = 0.0f;
    float z_[kMaxChannels] = {0, 0};
};

} // namespace mfx
