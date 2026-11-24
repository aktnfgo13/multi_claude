#include "dsp/analysis/Analysis.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mfx {
namespace analysis {

namespace {
const double PI = 3.14159265358979323846;

void fftInPlace(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / (double)len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

size_t nextPow2(size_t n) { size_t p = 1; while (p < n) p <<= 1; return p; }

double rms(const float* x, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return n ? std::sqrt(s / (double)n) : 0.0;
}
double peak(const float* x, size_t n) {
    double p = 0.0;
    for (size_t i = 0; i < n; ++i) p = std::max(p, (double)std::fabs(x[i]));
    return p;
}
double mean(const float* x, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += x[i];
    return n ? s / (double)n : 0.0;
}
double crestFactorDb(const float* x, size_t n) {
    const double r = rms(x, n);
    return r > 0 ? 20.0 * std::log10(peak(x, n) / r) : 0.0;
}

std::vector<double> spectrum(const float* x, size_t n, size_t* fftSize) {
    const size_t N = nextPow2(n);
    std::vector<std::complex<double>> a(N);
    for (size_t i = 0; i < n; ++i) {
        const double t = (double)i / (double)(n - 1);
        const double w = 0.35875 - 0.48829 * std::cos(2 * PI * t) + 0.14128 * std::cos(4 * PI * t) - 0.01168 * std::cos(6 * PI * t);
        a[i] = x[i] * w;
    }
    fftInPlace(a);
    std::vector<double> m(N / 2 + 1);
    for (size_t k = 0; k <= N / 2; ++k) m[k] = std::abs(a[k]);
    if (fftSize) *fftSize = N;
    return m;
}

HarmonicReport analyzeHarmonics(const float* x, size_t n, double fs, double f0, int maxH) {
    HarmonicReport r;
    size_t N = 0;
    const auto mag = spectrum(x, n, &N);
    const double binHz = fs / (double)N;
    const int half = 6; // Blackman-Harris main lobe +/- 4 bins (+margin)
    auto bandEnergy = [&](double f) {
        const long c = (long)std::lround(f / binHz);
        double e = 0.0;
        for (long k = c - half; k <= c + half; ++k)
            if (k >= 0 && k < (long)mag.size()) e += mag[(size_t)k] * mag[(size_t)k];
        return e;
    };
    double total = 0.0;
    for (size_t k = 0; k < mag.size(); ++k) total += mag[k] * mag[k];
    const double eDC = bandEnergy(0.0);
    const double e1 = bandEnergy(f0);
    // window coherent gain for amplitude calibration: sum(w)/2
    const double cg = 0.35875 * (double)n;
    r.fundamentalDb = 20.0 * std::log10(std::sqrt(e1) / (cg / 2.0) + 1e-30);
    double eh = 0.0, eEven = 0.0, eOdd = 0.0, eHarmAll = 0.0;
    for (int k = 2; k <= std::min(maxH, 15); ++k) {
        if (k * f0 >= fs / 2.0 - half * binHz) break;
        const double e = bandEnergy(k * f0);
        eh += e;
        (k % 2 == 0 ? eEven : eOdd) += e;
        r.harmonicsDb[k] = 10.0 * std::log10(e / (e1 + 1e-300) + 1e-30);
    }
    eHarmAll = eh;
    r.harmonicsDb[1] = 0.0;
    r.thd = std::sqrt(eh / (e1 + 1e-300));
    r.thdN = std::sqrt(std::max(0.0, total - e1 - eDC) / (e1 + 1e-300));
    r.evenOddRatioDb = 10.0 * std::log10((eEven + 1e-300) / (eOdd + 1e-300));
    // non-harmonic: everything except DC, fundamental and all harmonics up to Nyquist
    double eAllHarm = e1 + eDC;
    for (int k = 2; k * f0 < fs / 2.0 - half * binHz; ++k) eAllHarm += bandEnergy(k * f0);
    r.nonHarmonicDb = 10.0 * std::log10(std::max(1e-300, total - eAllHarm) / (e1 + 1e-300));
    (void)eHarmAll;
    return r;
}

std::vector<double> magnitudeResponseDb(const std::vector<float>& ir, double fs, const std::vector<double>& freqs) {
    std::vector<double> out;
    for (double f : freqs) {
        std::complex<double> s(0.0);
        const double w = -2.0 * PI * f / fs;
        for (size_t i = 0; i < ir.size(); ++i) s += (double)ir[i] * std::polar(1.0, w * (double)i);
        out.push_back(20.0 * std::log10(std::abs(s) + 1e-30));
    }
    return out;
}

double intermodulation(const float* x, size_t n, double fs, double f1, double f2) {
    size_t N = 0;
    const auto mag = spectrum(x, n, &N);
    const double binHz = fs / (double)N;
    auto e = [&](double f) {
        if (f <= 0 || f >= fs / 2) return 0.0;
        const long c = (long)std::lround(f / binHz);
        double s = 0.0;
        for (long k = c - 5; k <= c + 5; ++k) if (k >= 0 && k < (long)mag.size()) s += mag[(size_t)k] * mag[(size_t)k];
        return s;
    };
    const double ref = e(f2);
    double side = 0.0;
    for (int k = 1; k <= 4; ++k) side += e(f2 + k * f1) + e(f2 - k * f1);
    return std::sqrt(side / (ref + 1e-300));
}

double esr(const float* ref, const float* est, size_t n) {
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double d = (double)ref[i] - est[i];
        num += d * d;
        den += (double)ref[i] * ref[i];
    }
    return den > 0 ? num / den : (num > 0 ? 1e30 : 0.0);
}

} // namespace analysis
} // namespace mfx
