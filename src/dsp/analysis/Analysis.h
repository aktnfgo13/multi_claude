// Offline audio analysis used by tests, the renderer and benchmarks (non real-time).
#pragma once

#include <vector>

namespace mfx {
namespace analysis {

double rms(const float* x, size_t n);
double peak(const float* x, size_t n);
double mean(const float* x, size_t n);
double crestFactorDb(const float* x, size_t n);

struct HarmonicReport {
    double fundamentalDb = -200.0;      // dBFS of fundamental (sine peak = 0 dB)
    double thd = 0.0;                   // ratio, harmonics 2..maxHarmonic (below Nyquist)
    double thdN = 0.0;                  // ratio, everything except fundamental & DC
    double harmonicsDb[16] = {};        // level of harmonic k (k=1..15) re fundamental, dB
    double evenOddRatioDb = 0.0;        // energy(even)/energy(odd) of harmonics 2..15
    double nonHarmonicDb = -200.0;      // energy not at DC / harmonic bins, re fundamental (aliasing + noise)
};

/// Steady-state analysis of a periodic response to a sine at f0 (Blackman-Harris 4-term window).
HarmonicReport analyzeHarmonics(const float* x, size_t n, double fs, double f0, int maxHarmonic = 15);

/// Magnitude (dB) of the spectrum of x at given frequencies (zero padded FFT, rectangular window).
std::vector<double> magnitudeResponseDb(const std::vector<float>& impulseResponse, double fs, const std::vector<double>& freqs);

/// Twin-tone IMD (SMPTE-like ratio: sum of f2 +/- k f1 sidebands vs f2), ratio.
double intermodulation(const float* x, size_t n, double fs, double f1, double f2);

/// Error-to-signal ratio sum((y - yhat)^2) / sum(y^2).
double esr(const float* ref, const float* est, size_t n);

/// Next power of two >= n.
size_t nextPow2(size_t n);

/// Real spectrum magnitude (linear), windowed (Blackman-Harris), size = next pow2 of n. Returns bins 0..N/2.
std::vector<double> spectrum(const float* x, size_t n, size_t* fftSize = nullptr);

} // namespace analysis
} // namespace mfx
