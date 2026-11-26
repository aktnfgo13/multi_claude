// Fractional delay line (cubic Hermite interpolation) and LFO.
#pragma once

#include <vector>

#include "core/Common.h"

namespace mfx {

class DelayLine {
public:
    void prepare(int maxDelaySamples) {
        size_ = 1;
        while (size_ < maxDelaySamples + 4) size_ <<= 1;
        mask_ = size_ - 1;
        for (auto& b : buf_) b.assign((size_t)size_, 0.0f);
        reset();
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        w_ = 0;
    }
    int maxDelay() const { return size_ - 4; }
    size_t bytes() const { size_t n = 0; for (const auto& b : buf_) n += b.size() * sizeof(float); return n; }
    /// Write one frame then advance (call once per sample after reads).
    inline void push(float x, int ch) { buf_[ch][(size_t)w_] = x; }
    inline void advance() { w_ = (w_ + 1) & mask_; }
    /// Read integer delay (d >= 1 when called before push of the current sample; d >= 0 after push).
    inline float readInt(int d, int ch) const { return buf_[ch][(size_t)((w_ - d) & mask_)]; }
    /// Hermite interpolated fractional read, d in [1, maxDelay].
    inline float read(float d, int ch) const {
        const int di = (int)d;
        const float f = d - (float)di;
        const float* b = buf_[ch].data();
        const float xm1 = b[(w_ - di + 1) & mask_];
        const float x0 = b[(w_ - di) & mask_];
        const float x1 = b[(w_ - di - 1) & mask_];
        const float x2 = b[(w_ - di - 2) & mask_];
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }

private:
    std::vector<float> buf_[kMaxChannels];
    int size_ = 0, mask_ = 0, w_ = 0;
};

class LFO {
public:
    enum class Shape { Sine, Triangle };
    void prepare(double fs) { fs_ = fs; }
    void setRate(double hz) { inc_ = hz / fs_; }
    void setShape(Shape s) { shape_ = s; }
    void reset(double phase = 0.0) { phase_ = phase; }
    /// Returns value in [-1, 1] for given phase offset (0..1).
    inline float valueAt(double offset) const {
        double p = phase_ + offset;
        p -= std::floor(p);
        if (shape_ == Shape::Sine) return (float)std::sin(kTwoPi * p);
        return (float)(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    }
    inline void advance(int n = 1) { phase_ += inc_ * n; phase_ -= std::floor(phase_); }
    double phase() const { return phase_; }

private:
    double fs_ = 48000.0, inc_ = 0.0, phase_ = 0.0;
    Shape shape_ = Shape::Sine;
};

/// Peak envelope follower with separate attack / release (seconds).
class EnvelopeFollower {
public:
    void prepare(double fs) { fs_ = fs; set(attack_, release_); reset(); }
    void set(double attackSec, double releaseSec) {
        attack_ = attackSec; release_ = releaseSec;
        aA_ = onePoleCoeff(attackSec, fs_);
        aR_ = onePoleCoeff(releaseSec, fs_);
    }
    void reset() { for (auto& e : env_) e = 0.0f; }
    inline float process(float x, int ch) {
        const float a = std::fabs(x);
        float& e = env_[ch];
        e = a > e ? a + aA_ * (e - a) : a + aR_ * (e - a);
        return e;
    }
private:
    double fs_ = 48000.0, attack_ = 0.001, release_ = 0.1;
    float aA_ = 0.0f, aR_ = 0.0f;
    float env_[kMaxChannels] = {0, 0};
};

} // namespace mfx
