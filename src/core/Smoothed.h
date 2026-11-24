// Click-free parameter smoothing.
#pragma once

#include "core/Common.h"

namespace mfx {

/// Linear ramp to the target over a fixed time. Allocation free, deterministic.
class LinearSmoothed {
public:
    void reset(double sampleRate, double rampSeconds) {
        rampSamples_ = std::max(1, (int)std::lround(sampleRate * rampSeconds));
        setImmediate(target_);
    }
    void setImmediate(float v) { current_ = target_ = v; remaining_ = 0; step_ = 0.0f; }
    void setTarget(float v) {
        if (v == target_) return;
        target_ = v;
        remaining_ = rampSamples_;
        step_ = (target_ - current_) / (float)remaining_;
    }
    float next() {
        if (remaining_ > 0) {
            current_ += step_;
            if (--remaining_ == 0) current_ = target_;
        }
        return current_;
    }
    /// Advance n samples at once (control-rate use).
    float skip(int n) {
        if (remaining_ <= 0) return current_;
        if (n >= remaining_) { current_ = target_; remaining_ = 0; }
        else { current_ += step_ * (float)n; remaining_ -= n; }
        return current_;
    }
    bool isSmoothing() const { return remaining_ > 0; }
    float current() const { return current_; }
    float target() const { return target_; }

private:
    float current_ = 0.0f, target_ = 0.0f, step_ = 0.0f;
    int remaining_ = 0, rampSamples_ = 1;
};

} // namespace mfx
