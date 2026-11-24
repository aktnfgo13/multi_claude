// Linear-phase polyphase half-band FIR oversampler, cascaded 2x stages (1x/2x/4x/8x).
//
// Stage design (Kaiser-windowed sinc, cutoff fs_stage/4):
//   stage 1: 63 taps, beta 7.8  -> passband to ~0.42 fs, ~80 dB image rejection
//   stage 2: 33 taps, beta 7.0
//   stage 3: 17 taps, beta 6.0
// Tap counts are chosen so that the round-trip group delay is an INTEGER number of base-rate samples:
//   (N_s - 1) / 2^s : 62/2 = 31, 32/4 = 8, 16/8 = 2  -> latency 2x:31, 4x:39, 8x:41 samples.
// Memory for 8x is allocated in prepare(); switching factor at run time is allocation free.
#pragma once

#include <vector>

#include "core/Common.h"

namespace mfx {

std::vector<double> designHalfbandKaiser(int numTaps, double beta);

class HalfbandStage {
public:
    void init(int numTaps, double beta, int maxInputBlock);
    void reset();
    /// in: n samples -> out: 2n samples
    void upsample(const float* in, float* out, int n, int ch);
    /// in: 2n samples -> out: n samples (y[i] = sum_k h[k] v[2i-k], even phase)
    void downsample(const float* in, float* out, int n, int ch);
    int numTaps() const { return (int)h_.size(); }
    const std::vector<float>& taps() const { return h_; }

private:
    std::vector<float> h_;          // full impulse response
    std::vector<float> even_, odd_; // polyphase branches of h (h[2k], h[2k+1])
    // history buffers (double-length ring for contiguous reads)
    std::vector<float> upHist_[kMaxChannels];
    std::vector<float> dnHist_[kMaxChannels];
    int upPos_[kMaxChannels] = {0, 0};
    int dnPos_[kMaxChannels] = {0, 0};
    int upLen_ = 0, dnLen_ = 0;
};

class Oversampler {
public:
    static constexpr int kMaxStages = 3;
    void prepare(int maxBlockSize, int numChannels);
    void reset();
    /// factorIndex: 0=1x, 1=2x, 2=4x, 3=8x
    void setFactorIndex(int idx) { stages_ = clampv(idx, 0, kMaxStages); }
    int factorIndex() const { return stages_; }
    int factor() const { return 1 << stages_; }
    int latencySamples() const { return latencyFor(stages_); }
    static int latencyFor(int factorIndex);

    /// Upsample channel `ch` (n base samples). Returns pointer to factor()*n samples (internal buffer).
    float* upsample(const float* in, int n, int ch);
    /// Downsample the internal buffer previously returned (processed in place) into out (n samples).
    void downsample(float* out, int n, int ch);

private:
    HalfbandStage stage_[kMaxStages];
    std::vector<float> buf_[kMaxChannels][kMaxStages + 1];
    int stages_ = 1;
    int maxBlock_ = 0;
};

} // namespace mfx
