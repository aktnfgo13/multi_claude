#include "engine/AnalysisWorker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mfx {

AnalysisWorker::AnalysisWorker(double historySeconds, double maxSampleRate) {
    cap_ = std::max(8192, (int)std::ceil(historySeconds * maxSampleRate));
    for (auto& src : ring_)
        for (auto& ch : src) ch.assign((size_t)cap_, 0.0f);
}

void AnalysisWorker::clear() {
    for (auto& src : ring_)
        for (auto& ch : src) std::fill(ch.begin(), ch.end(), 0.0f);
    written_ = 0;
    info_ = Info{};
}

int AnalysisWorker::poll(AnalysisTap& tap) {
    int count = 0;
    const uint32_t gen = tap.generation();
    if (gen != generation_) { // engine re-prepared (sample rate / block size change): restart history
        generation_ = gen;
        clear();
    }
    while (const AnalysisTap::Packet* p = tap.peek()) {
        if (p->generation == generation_ && !paused_) {
            const int n = p->numFrames;
            for (int i = 0; i < n; ++i) {
                const size_t w = (size_t)((written_ + i) % cap_);
                const float il = p->in[0][i], ir = p->inChannels > 1 ? p->in[1][i] : il;
                const float ol = p->out[0][i], orr = p->outChannels > 1 ? p->out[1][i] : ol;
                ring_[0][0][w] = il; ring_[0][1][w] = ir;
                ring_[1][0][w] = ol; ring_[1][1][w] = orr;
            }
            written_ += n;
            latency_ = std::max(0, p->latency);
            info_.sampleRate = p->sampleRate;
            info_.inChannels = p->inChannels;
            info_.outChannels = p->outChannels;
            info_.latency = latency_;
            info_.lastFrame = p->frameIndex + (uint64_t)n;
            info_.framesCaptured += (uint64_t)n;
            info_.hasData = true;
        }
        tap.pop();
        ++count;
    }
    info_.dropped = tap.droppedPackets();
    return count;
}

int AnalysisWorker::availableFrames() const {
    const int64_t avail = std::min<int64_t>(written_, cap_) - latency_;
    return (int)std::max<int64_t>(0, avail);
}

float AnalysisWorker::sampleAt(Source s, Channel ch, int64_t age) const {
    const int si = s == Source::Input ? 0 : 1;
    const int64_t idx = written_ - 1 - age - (s == Source::Input ? latency_ : 0);
    const size_t w = (size_t)(((idx % cap_) + cap_) % cap_);
    switch (ch) {
        case Channel::Left: return ring_[si][0][w];
        case Channel::Right: return ring_[si][1][w];
        default: return 0.5f * (ring_[si][0][w] + ring_[si][1][w]);
    }
}

bool AnalysisWorker::latest(Source s, Channel ch, int numFrames, float* dst) const {
    if (numFrames <= 0 || numFrames > availableFrames()) return false;
    for (int i = 0; i < numFrames; ++i) dst[i] = sampleAt(s, ch, numFrames - 1 - i);
    return true;
}

bool AnalysisWorker::waveform(Source s, Channel ch, int numFrames, int columns, float* outMin, float* outMax) const {
    numFrames = std::min(numFrames, availableFrames());
    if (numFrames <= 0 || columns <= 0) return false;
    for (int c = 0; c < columns; ++c) {
        const int64_t a = (int64_t)c * numFrames / columns, b = std::max<int64_t>(a + 1, (int64_t)(c + 1) * numFrames / columns);
        float mn = 1e30f, mx = -1e30f;
        for (int64_t i = a; i < b; ++i) {
            const float v = sampleAt(s, ch, numFrames - 1 - i);
            mn = std::min(mn, v); mx = std::max(mx, v);
        }
        outMin[c] = mn; outMax[c] = mx;
    }
    return true;
}

void AnalysisWorker::ensureFft(int n) {
    if (n == fftN_) return;
    fftN_ = n;
    fft_.prepare(n);
    win_.resize((size_t)n); frame_.resize((size_t)n); mag_.resize((size_t)n / 2 + 1);
    bins_.resize((size_t)n / 2 + 1); scratch_.resize((size_t)n);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        win_[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / n)); // periodic Hann
        sum += win_[(size_t)i];
    }
    winSum_ = (float)sum; // coherent gain * n
}

float AnalysisWorker::pointFrequency(int i, int points, float fLo, float fHi) {
    if (points <= 1) return fLo;
    return fLo * std::pow(fHi / fLo, (float)i / (float)(points - 1));
}

bool AnalysisWorker::spectrum(Source s, Channel ch, int fftSize, int points, float fLo, float fHi, float* outDb) {
    if (fftSize != 2048 && fftSize != 4096) fftSize = fftSize > 3072 ? 4096 : 2048;
    if (!info_.hasData || availableFrames() < fftSize || points <= 0) return false;
    ensureFft(fftSize);
    latest(s, ch, fftSize, frame_.data());
    for (int i = 0; i < fftSize; ++i) frame_[(size_t)i] *= win_[(size_t)i];
    fft_.forwardReal(frame_.data(), bins_.data(), scratch_.data());
    // amplitude: |X| * 2 / sum(w) -> a full-scale sine reads 1.0 (0 dBFS)
    const float k = 2.0f / winSum_;
    for (size_t b = 0; b < mag_.size(); ++b) mag_[b] = std::abs(bins_[b]) * k;
    const double fs = info_.sampleRate > 0 ? info_.sampleRate : 48000.0;
    const double binHz = fs / fftSize;
    fHi = std::min<float>(fHi, (float)(0.5 * fs));
    for (int p = 0; p < points; ++p) {
        // each display point covers [f(p-1/2), f(p+1/2)] in log space; take the max bin (peak-preserving)
        const float f0 = pointFrequency(p, points, fLo, fHi);
        const float r = points > 1 ? std::pow(fHi / fLo, 0.5f / (float)(points - 1)) : 1.0f;
        int b0 = (int)std::floor(f0 / r / binHz), b1 = (int)std::ceil(f0 * r / binHz);
        b0 = std::clamp(b0, 1, (int)mag_.size() - 1); b1 = std::clamp(b1, b0, (int)mag_.size() - 1);
        float m = 0.0f;
        if (b1 - b0 <= 1) { // point narrower than a bin (low frequencies): interpolate between bins
            const double fb = std::clamp(f0 / binHz, 1.0, (double)mag_.size() - 2.0);
            const size_t i0 = (size_t)fb;
            const float t = (float)(fb - (double)i0);
            m = mag_[i0] + t * (mag_[i0 + 1] - mag_[i0]);
        } else {
            for (int b = b0; b <= b1; ++b) m = std::max(m, mag_[(size_t)b]); // peak-preserving
        }
        outDb[p] = m > 1e-7f ? 20.0f * std::log10(m) : -140.0f;
    }
    return true;
}

bool AnalysisWorker::smoothedSpectrum(Source s, Channel ch, int fftSize, int points, float fLo, float fHi,
                                      float* state, float attack, float release) {
    std::vector<float> tmp((size_t)points);
    if (!spectrum(s, ch, fftSize, points, fLo, fHi, tmp.data())) return false;
    for (int p = 0; p < points; ++p) {
        const float a = tmp[(size_t)p] > state[p] ? attack : release;
        state[p] += a * (tmp[(size_t)p] - state[p]);
    }
    return true;
}

} // namespace mfx
