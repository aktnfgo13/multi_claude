#include "dsp/utility/Resample.h"

#include <cmath>

namespace mfx {

static double i0(double x) {
    double s = 1.0, t = 1.0;
    for (int k = 1; k < 50; ++k) { t *= (x * x / 4.0) / (k * k); s += t; if (t < 1e-16 * s) break; }
    return s;
}

std::vector<float> resampleOffline(const std::vector<float>& in, double srIn, double srOut, int halfTaps) {
    if (in.empty() || srIn <= 0 || srOut <= 0) return {};
    if (std::fabs(srIn - srOut) < 1e-9) return in;
    const double ratio = srOut / srIn;
    const double cutoff = std::min(1.0, ratio) * 0.97; // relative to input Nyquist
    const double beta = 8.0, i0b = i0(beta);
    const size_t outLen = (size_t)std::ceil(in.size() * ratio);
    std::vector<float> out(outLen, 0.0f);
    const double scale = std::max(1.0, 1.0 / ratio);
    const int half = (int)std::ceil(halfTaps * scale);
    for (size_t n = 0; n < outLen; ++n) {
        const double t = n / ratio; // position in input samples
        const long c = (long)std::floor(t);
        double acc = 0.0;
        for (long k = c - half + 1; k <= c + half; ++k) {
            if (k < 0 || k >= (long)in.size()) continue;
            const double x = t - (double)k;
            const double arg = 3.14159265358979323846 * x * cutoff;
            const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(arg) / arg;
            const double r = x / (double)half;
            if (std::fabs(r) > 1.0) continue;
            const double w = i0(beta * std::sqrt(1.0 - r * r)) / i0b;
            acc += in[(size_t)k] * cutoff * sinc * w;
        }
        out[n] = (float)acc;
    }
    return out;
}

} // namespace mfx
