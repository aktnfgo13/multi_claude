// Zero-latency partitioned convolution:
//   head  : first kHead taps computed directly in the time domain (per sample)
//   tail  : remaining taps, uniformly partitioned (kHead) FFT convolution, frequency-domain delay line.
// Because tail partition j>=1 contributes to outputs >= kHead*(k+j) when input block k completes,
// results are always ready in time -> no added latency.
//
// A ConvolutionKernel (pre-transformed IR) is built on a non-real-time thread and swapped in
// lock-free by the owner (see Cabinet).
#pragma once

#include <complex>
#include <memory>
#include <vector>

#include "core/Common.h"
#include "dsp/fft/FFT.h"

namespace mfx {

struct ConvolutionKernel {
    static constexpr int kHead = 64;
    static constexpr int kFFT = 2 * kHead;
    int length = 0;                                   // IR length in samples
    std::vector<float> head;                          // kHead taps
    std::vector<std::vector<std::complex<float>>> parts; // tail partitions (bins kFFT/2+1)
    /// Build from IR samples (already at engine sample rate).
    static std::unique_ptr<ConvolutionKernel> build(const float* ir, int len);
};

class Convolver {
public:
    /// Allocate for kernels up to maxPartitions (non-RT).
    void prepare(int maxIrLength);
    void reset();
    /// Must only be called when not processing concurrently, or from the audio thread itself.
    void setKernel(const ConvolutionKernel* k) { kernel_ = k; reset(); }
    const ConvolutionKernel* kernel() const { return kernel_; }
    float process(float x);

private:
    void processBlock();
    const ConvolutionKernel* kernel_ = nullptr;
    FFT fft_;
    int maxParts_ = 0;
    std::vector<float> inHist_;  // time-domain history for head (double length ring)
    int histPos_ = 0;
    std::vector<float> inBlock_; // accumulates kHead input samples
    int inCount_ = 0;
    std::vector<float> prevBlock_;
    std::vector<std::vector<std::complex<float>>> fdl_; // spectra of past input blocks
    int fdlPos_ = 0;
    std::vector<float> outAcc_;  // overlap/accumulation for tail output (ring, 2*kHead)
    int outPos_ = 0;
    std::vector<std::complex<float>> acc_, scratch_;
    std::vector<float> timeBuf_;
};

} // namespace mfx
