#include "dsp/cabinet/Convolver.h"

#include <cstring>

namespace mfx {

std::unique_ptr<ConvolutionKernel> ConvolutionKernel::build(const float* ir, int len) {
    auto k = std::make_unique<ConvolutionKernel>();
    k->length = len;
    k->head.assign(kHead, 0.0f);
    for (int i = 0; i < std::min(len, kHead); ++i) k->head[(size_t)i] = ir[i];
    const int numParts = len > kHead ? (len - kHead + kHead - 1) / kHead : 0;
    FFT fft(kFFT);
    std::vector<std::complex<float>> scratch(kFFT);
    std::vector<float> buf(kFFT);
    for (int j = 1; j <= numParts; ++j) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < kHead; ++i) {
            const int idx = j * kHead + i;
            if (idx < len) buf[(size_t)i] = ir[idx];
        }
        std::vector<std::complex<float>> bins(kFFT / 2 + 1);
        fft.forwardReal(buf.data(), bins.data(), scratch.data());
        k->parts.push_back(std::move(bins));
    }
    return k;
}

void Convolver::prepare(int maxIrLength) {
    constexpr int B = ConvolutionKernel::kHead;
    maxParts_ = std::max(1, (maxIrLength + B - 1) / B);
    fft_.prepare(ConvolutionKernel::kFFT);
    inHist_.assign(2 * B, 0.0f);
    inBlock_.assign(B, 0.0f);
    prevBlock_.assign(B, 0.0f);
    fdl_.assign((size_t)maxParts_, std::vector<std::complex<float>>(ConvolutionKernel::kFFT / 2 + 1));
    outAcc_.assign(B, 0.0f);
    acc_.assign(ConvolutionKernel::kFFT / 2 + 1, {});
    scratch_.assign(ConvolutionKernel::kFFT, {});
    timeBuf_.assign(ConvolutionKernel::kFFT, 0.0f);
    reset();
}

void Convolver::reset() {
    std::fill(inHist_.begin(), inHist_.end(), 0.0f);
    std::fill(inBlock_.begin(), inBlock_.end(), 0.0f);
    std::fill(prevBlock_.begin(), prevBlock_.end(), 0.0f);
    for (auto& f : fdl_) std::fill(f.begin(), f.end(), std::complex<float>{});
    std::fill(outAcc_.begin(), outAcc_.end(), 0.0f);
    histPos_ = inCount_ = fdlPos_ = outPos_ = 0;
}

float Convolver::process(float x) {
    constexpr int B = ConvolutionKernel::kHead;
    if (!kernel_) return 0.0f;
    // head (direct form)
    histPos_ = (histPos_ == 0) ? B - 1 : histPos_ - 1;
    inHist_[(size_t)histPos_] = inHist_[(size_t)(histPos_ + B)] = x;
    const float* hx = inHist_.data() + histPos_;
    const float* h = kernel_->head.data();
    float y = 0.0f;
    for (int m = 0; m < B; ++m) y += h[m] * hx[m];
    // tail (computed at the previous block boundary)
    y += outAcc_[(size_t)inCount_];
    inBlock_[(size_t)inCount_] = x;
    if (++inCount_ == B) { processBlock(); inCount_ = 0; }
    return y;
}

void Convolver::processBlock() {
    constexpr int B = ConvolutionKernel::kHead;
    const int P = std::min((int)kernel_->parts.size(), maxParts_);
    if (P == 0) { std::fill(outAcc_.begin(), outAcc_.end(), 0.0f); return; }
    // spectrum of newest input block
    fdlPos_ = (fdlPos_ + 1) % maxParts_;
    std::fill(timeBuf_.begin(), timeBuf_.end(), 0.0f);
    std::memcpy(timeBuf_.data(), inBlock_.data(), sizeof(float) * B);
    fft_.forwardReal(timeBuf_.data(), fdl_[(size_t)fdlPos_].data(), scratch_.data());
    // S_k = sum_{j=1..P} X_{k+1-j} H_j
    std::fill(acc_.begin(), acc_.end(), std::complex<float>{});
    const int nb = ConvolutionKernel::kFFT / 2 + 1;
    for (int j = 1; j <= P; ++j) {
        const int idx = ((fdlPos_ - (j - 1)) % maxParts_ + maxParts_) % maxParts_;
        const auto* X = fdl_[(size_t)idx].data();
        const auto* H = kernel_->parts[(size_t)(j - 1)].data();
        for (int b = 0; b < nb; ++b) acc_[(size_t)b] += X[b] * H[b];
    }
    fft_.inverseReal(acc_.data(), timeBuf_.data(), scratch_.data());
    for (int i = 0; i < B; ++i) {
        outAcc_[(size_t)i] = timeBuf_[(size_t)i] + prevBlock_[(size_t)i];
        prevBlock_[(size_t)i] = timeBuf_[(size_t)(i + B)];
    }
}

} // namespace mfx
