#include "dsp/fft/FFT.h"

#include <cmath>

namespace mfx {

void FFT::prepare(int size) {
    n_ = size;
    logn_ = 0;
    while ((1 << logn_) < n_) ++logn_;
    rev_.resize((size_t)n_);
    for (int i = 0; i < n_; ++i) {
        int r = 0;
        for (int b = 0; b < logn_; ++b) r |= ((i >> b) & 1) << (logn_ - 1 - b);
        rev_[(size_t)i] = r;
    }
    tw_.resize((size_t)(n_ / 2));
    for (int k = 0; k < n_ / 2; ++k) {
        const double a = -2.0 * 3.14159265358979323846 * k / n_;
        tw_[(size_t)k] = cf((float)std::cos(a), (float)std::sin(a));
    }
}

void FFT::transform(cf* d, bool inv) const {
    for (int i = 0; i < n_; ++i) {
        const int r = rev_[(size_t)i];
        if (r > i) std::swap(d[i], d[r]);
    }
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len >> 1;
        const int step = n_ / len;
        for (int i = 0; i < n_; i += len) {
            for (int k = 0; k < half; ++k) {
                cf w = tw_[(size_t)(k * step)];
                if (inv) w = std::conj(w);
                const cf u = d[i + k];
                const cf v = d[i + k + half] * w;
                d[i + k] = u + v;
                d[i + k + half] = u - v;
            }
        }
    }
}

void FFT::inverse(cf* data) const {
    transform(data, true);
    const float s = 1.0f / (float)n_;
    for (int i = 0; i < n_; ++i) data[i] *= s;
}

void FFT::forwardReal(const float* in, cf* outBins, cf* scratch) const {
    for (int i = 0; i < n_; ++i) scratch[i] = cf(in[i], 0.0f);
    transform(scratch, false);
    for (int k = 0; k <= n_ / 2; ++k) outBins[k] = scratch[k];
}

void FFT::inverseReal(const cf* bins, float* out, cf* scratch) const {
    for (int k = 0; k <= n_ / 2; ++k) scratch[k] = bins[k];
    for (int k = n_ / 2 + 1; k < n_; ++k) scratch[k] = std::conj(bins[n_ - k]);
    transform(scratch, true);
    const float s = 1.0f / (float)n_;
    for (int i = 0; i < n_; ++i) out[i] = scratch[i].real() * s;
}

} // namespace mfx
