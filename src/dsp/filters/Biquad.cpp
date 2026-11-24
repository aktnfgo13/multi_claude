#include "dsp/filters/Biquad.h"

#include <complex>

namespace mfx {

BiquadCoeffs designBiquad(FilterType type, double fs, double f0, double Q, double gainDb) {
    f0 = clampv(f0, 1.0, 0.499 * fs);
    Q = std::max(Q, 0.025);
    const double w0 = kTwoPi * f0 / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * Q);
    const double A = std::pow(10.0, gainDb / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type) {
        case FilterType::LowPass:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::HighPass:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::BandPass: // constant 0 dB peak gain
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::AllPass:
            b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case FilterType::Peak:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case FilterType::LowShelf: {
            const double sa = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) - (A - 1) * cw + sa);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sa;
            break;
        }
        case FilterType::HighShelf: {
            const double sa = 2 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1) + (A - 1) * cw + sa);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sa;
            break;
        }
        case FilterType::LowPass1: {
            const double K = std::tan(kPi * f0 / fs);
            b0 = K; b1 = K; b2 = 0; a0 = 1 + K; a1 = K - 1; a2 = 0; break;
        }
        case FilterType::HighPass1: {
            const double K = std::tan(kPi * f0 / fs);
            b0 = 1; b1 = -1; b2 = 0; a0 = 1 + K; a1 = K - 1; a2 = 0; break;
        }
        case FilterType::AllPass1: {
            const double K = std::tan(kPi * f0 / fs);
            b0 = K - 1; b1 = 1 + K; b2 = 0; a0 = 1 + K; a1 = K - 1; a2 = 0; break;
        }
        case FilterType::LowShelf1: { // H(s) = (s + wA) / (s + w) style, gain A^2 at DC
            const double G = std::pow(10.0, gainDb / 20.0);
            const double K = std::tan(kPi * f0 / fs);
            // analog: H(s) = (s + G*w)/(s + w), w normalised by bilinear prewarp K
            b0 = 1 + G * K; b1 = G * K - 1; b2 = 0; a0 = 1 + K; a1 = K - 1; a2 = 0; break;
        }
        case FilterType::HighShelf1: { // H(s) = (G*s + w)/(s + w)
            const double G = std::pow(10.0, gainDb / 20.0);
            const double K = std::tan(kPi * f0 / fs);
            b0 = G + K; b1 = K - G; b2 = 0; a0 = 1 + K; a1 = K - 1; a2 = 0; break;
        }
    }
    BiquadCoeffs c;
    c.b0 = (float)(b0 / a0); c.b1 = (float)(b1 / a0); c.b2 = (float)(b2 / a0);
    c.a1 = (float)(a1 / a0); c.a2 = (float)(a2 / a0);
    return c;
}

double biquadMagnitude(const BiquadCoeffs& c, double fs, double f) {
    const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / fs);
    const std::complex<double> z2 = z1 * z1;
    const auto num = (double)c.b0 + (double)c.b1 * z1 + (double)c.b2 * z2;
    const auto den = 1.0 + (double)c.a1 * z1 + (double)c.a2 * z2;
    return std::abs(num / den);
}

bool biquadIsStable(const BiquadCoeffs& c) {
    // Jury conditions for z^2 + a1 z + a2
    const double a1 = c.a1, a2 = c.a2;
    return std::fabs(a2) < 1.0 && std::fabs(a1) < 1.0 + a2;
}

} // namespace mfx
