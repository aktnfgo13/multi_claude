#include "dsp/oversampling/Oversampler.h"

#include <cstring>

namespace mfx {

static double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 64; ++k) {
        term *= q / ((double)k * (double)k);
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

std::vector<double> designHalfbandKaiser(int N, double beta) {
    std::vector<double> h((size_t)N);
    const double M = (N - 1) / 2.0;
    const double i0b = besselI0(beta);
    for (int n = 0; n < N; ++n) {
        const double t = n - M;
        const double sinc = (t == 0.0) ? 0.5 : std::sin(kPi * 0.5 * t) / (kPi * t); // cutoff fs/4
        const double r = (n - M) / M;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        h[(size_t)n] = sinc * w;
    }
    // normalise DC gain to exactly 1
    double s = 0.0;
    for (double v : h) s += v;
    for (double& v : h) v /= s;
    return h;
}

void HalfbandStage::init(int numTaps, double beta, int maxInputBlock) {
    const auto hd = designHalfbandKaiser(numTaps, beta);
    h_.assign(hd.begin(), hd.end());
    even_.clear(); odd_.clear();
    for (size_t i = 0; i < h_.size(); ++i) (i % 2 == 0 ? even_ : odd_).push_back(h_[i]);
    upLen_ = (int)even_.size() + 1;
    dnLen_ = (int)h_.size();
    for (int c = 0; c < kMaxChannels; ++c) {
        upHist_[c].assign((size_t)(2 * upLen_), 0.0f);
        dnHist_[c].assign((size_t)(2 * dnLen_), 0.0f);
    }
    (void)maxInputBlock;
    reset();
}

void HalfbandStage::reset() {
    for (int c = 0; c < kMaxChannels; ++c) {
        std::fill(upHist_[c].begin(), upHist_[c].end(), 0.0f);
        std::fill(dnHist_[c].begin(), dnHist_[c].end(), 0.0f);
        upPos_[c] = dnPos_[c] = 0;
    }
}

// Zero-stuffed upsampling: u[2n]=x[n], u[2n+1]=0, y = 2 * (h * u)
// y[2n]   = 2 * sum_k h[2k]   x[n-k]
// y[2n+1] = 2 * sum_k h[2k+1] x[n-k]
void HalfbandStage::upsample(const float* in, float* out, int n, int ch) {
    float* hist = upHist_[ch].data();
    int pos = upPos_[ch];
    const int ne = (int)even_.size(), no = (int)odd_.size();
    for (int i = 0; i < n; ++i) {
        pos = (pos == 0) ? upLen_ - 1 : pos - 1;
        hist[pos] = hist[pos + upLen_] = in[i];
        const float* x = hist + pos; // x[0] = newest, x[k] = x[n-k]
        float se = 0.0f, so = 0.0f;
        for (int k = 0; k < ne; ++k) se += even_[(size_t)k] * x[k];
        for (int k = 0; k < no; ++k) so += odd_[(size_t)k] * x[k];
        out[2 * i] = 2.0f * se;
        out[2 * i + 1] = 2.0f * so;
    }
    upPos_[ch] = pos;
}

// y[n] = sum_k h[k] v[2n+1-k]  (filter then keep every second sample)
void HalfbandStage::downsample(const float* in, float* out, int n, int ch) {
    float* hist = dnHist_[ch].data();
    int pos = dnPos_[ch];
    const int N = (int)h_.size();
    const float* h = h_.data();
    // Output is taken at the EVEN high-rate phase (after pushing in[2i]) so that the
    // round-trip delay (N-1 high-rate samples, even) maps to an integer base-rate latency.
    for (int i = 0; i < n; ++i) {
        pos = (pos == 0) ? dnLen_ - 1 : pos - 1;
        hist[pos] = hist[pos + dnLen_] = in[2 * i];
        const float* v = hist + pos;
        float s = 0.0f;
        for (int k = 0; k < N; ++k) s += h[k] * v[k];
        out[i] = s;
        pos = (pos == 0) ? dnLen_ - 1 : pos - 1;
        hist[pos] = hist[pos + dnLen_] = in[2 * i + 1];
    }
    dnPos_[ch] = pos;
}

int Oversampler::latencyFor(int idx) {
    static const int table[] = {0, 31, 39, 41};
    return table[clampv(idx, 0, 3)];
}

void Oversampler::prepare(int maxBlockSize, int numChannels) {
    (void)numChannels;
    maxBlock_ = maxBlockSize;
    static const int taps[kMaxStages] = {63, 33, 17};
    static const double betas[kMaxStages] = {7.8, 7.0, 6.0};
    for (int s = 0; s < kMaxStages; ++s) stage_[s].init(taps[s], betas[s], maxBlockSize << s);
    for (int c = 0; c < kMaxChannels; ++c)
        for (int s = 0; s <= kMaxStages; ++s) buf_[c][s].assign((size_t)(maxBlockSize << s), 0.0f);
    reset();
}

void Oversampler::reset() {
    for (auto& s : stage_) s.reset();
}

float* Oversampler::upsample(const float* in, int n, int ch) {
    std::memcpy(buf_[ch][0].data(), in, sizeof(float) * (size_t)n);
    for (int s = 0; s < stages_; ++s) stage_[s].upsample(buf_[ch][s].data(), buf_[ch][s + 1].data(), n << s, ch);
    return buf_[ch][stages_].data();
}

void Oversampler::downsample(float* out, int n, int ch) {
    for (int s = stages_ - 1; s >= 0; --s) stage_[s].downsample(buf_[ch][s + 1].data(), buf_[ch][s].data(), n << s, ch);
    std::memcpy(out, buf_[ch][0].data(), sizeof(float) * (size_t)n);
}

} // namespace mfx
