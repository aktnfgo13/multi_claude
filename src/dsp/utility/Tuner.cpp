#include "dsp/utility/Tuner.h"

#include <algorithm>

namespace mfx {

const char* PitchResult::noteName() const {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return valid ? names[((midiNote % 12) + 12) % 12] : "-";
}

PitchResult detectPitchYin(const float* x, int n, double fs, double minHz, double maxHz, double thr) {
    PitchResult r;
    const int tauMin = std::max(2, (int)(fs / maxHz));
    const int tauMax = std::min(n / 2, (int)(fs / minHz));
    if (tauMax <= tauMin + 2) return r;
    const int W = n - tauMax;
    double energy = 0.0;
    for (int i = 0; i < n; ++i) energy += (double)x[i] * x[i];
    if (energy / n < 1e-8) return r; // silence
    std::vector<double> d((size_t)tauMax + 1, 0.0), cmnd((size_t)tauMax + 1, 1.0);
    for (int tau = 1; tau <= tauMax; ++tau) {
        double s = 0.0;
        for (int i = 0; i < W; ++i) { const double v = (double)x[i] - x[i + tau]; s += v * v; }
        d[(size_t)tau] = s;
    }
    double run = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau) {
        run += d[(size_t)tau];
        cmnd[(size_t)tau] = run > 0 ? d[(size_t)tau] * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau) {
        if (cmnd[(size_t)tau] < thr) {
            while (tau + 1 < tauMax && cmnd[(size_t)tau + 1] < cmnd[(size_t)tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0) {
        best = (int)(std::min_element(cmnd.begin() + tauMin, cmnd.begin() + tauMax) - cmnd.begin());
        if (cmnd[(size_t)best] > 0.35) return r;
    }
    double t = best;
    if (best > 1 && best < tauMax) {
        const double a = cmnd[(size_t)best - 1], b = cmnd[(size_t)best], c = cmnd[(size_t)best + 1];
        const double den = a - 2 * b + c;
        if (std::fabs(den) > 1e-12) t = best + 0.5 * (a - c) / den;
    }
    r.valid = true;
    r.frequency = fs / t;
    r.clarity = 1.0 - cmnd[(size_t)best];
    const double midi = 69.0 + 12.0 * std::log2(r.frequency / 440.0);
    r.midiNote = (int)std::lround(midi);
    r.cents = (midi - r.midiNote) * 100.0;
    return r;
}

Tuner::Tuner() {
    for (auto& r : ring_) r.store(0.0f, std::memory_order_relaxed);
    params_.add({"mute", "Mute Output", 0, 1, 1, ParamUnit::Toggle});
    params_.add({"reference_hz", "Reference A4", 430, 450, 440, ParamUnit::Hertz});
}

void Tuner::onProcess(AudioBlock& b) {
    uint32_t w = w_.load(std::memory_order_relaxed);
    for (int i = 0; i < b.numSamples; ++i) {
        float v = b.ch[0][i];
        if (b.numChannels > 1) v = 0.5f * (v + b.ch[1][i]);
        acc_ += v;
        if (++accN_ == kDecim) {
            ring_[w % kRing].store(acc_ / kDecim, std::memory_order_relaxed);
            ++w;
            acc_ = 0.0f; accN_ = 0;
        }
    }
    w_.store(w, std::memory_order_release);
    if (params_.getInt(kMute))
        for (int c = 0; c < b.numChannels; ++c) std::fill(b.ch[c], b.ch[c] + b.numSamples, 0.0f);
}

PitchResult Tuner::analyze() const {
    const int N = 4096;
    std::vector<float> win((size_t)N);
    const uint32_t w = w_.load(std::memory_order_acquire);
    if (w < (uint32_t)N) return {};
    for (int i = 0; i < N; ++i) win[(size_t)i] = ring_[(w - N + i) % kRing].load(std::memory_order_relaxed);
    PitchResult r = detectPitchYin(win.data(), N, spec_.sampleRate / kDecim);
    if (r.valid) {
        const double ref = params_.get(kReference);
        const double midi = 69.0 + 12.0 * std::log2(r.frequency / ref);
        r.midiNote = (int)std::lround(midi);
        r.cents = (midi - r.midiNote) * 100.0;
    }
    return r;
}

double TapTempo::tap(double t) {
    if (n_ > 0 && t - times_[(n_ - 1) % 8] > 2.0) n_ = 0; // long gap restarts
    times_[n_ % 8] = t;
    ++n_;
    const int k = std::min(n_, 5);
    if (k < 2) return bpm_;
    double iv[4];
    int m = 0;
    for (int i = n_ - k + 1; i < n_; ++i) iv[m++] = times_[i % 8] - times_[(i - 1) % 8];
    for (int a = 1; a < m; ++a) // insertion sort (m <= 4)
        for (int b = a; b > 0 && iv[b] < iv[b - 1]; --b) std::swap(iv[b], iv[b - 1]);
    const double med = (m % 2) ? iv[m / 2] : 0.5 * (iv[m / 2 - 1] + iv[m / 2]);
    if (med > 0.0) bpm_ = 60.0 / med;
    return bpm_;
}

} // namespace mfx
