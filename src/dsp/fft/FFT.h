// Iterative radix-2 complex FFT (power-of-two sizes). Tables are built in prepare(), so
// transforms are allocation free. Used by partitioned convolution and analysis.
#pragma once

#include <complex>
#include <vector>

namespace mfx {

class FFT {
public:
    using cf = std::complex<float>;
    explicit FFT(int size = 0) { if (size > 0) prepare(size); }
    void prepare(int size);
    int size() const { return n_; }
    /// In-place forward transform (no scaling).
    void forward(cf* data) const { transform(data, false); }
    /// In-place inverse transform (scaled by 1/N).
    void inverse(cf* data) const;
    /// Real input (n samples) -> n/2+1 bins. scratch must hold n complex values.
    void forwardReal(const float* in, cf* outBins, cf* scratch) const;
    /// n/2+1 bins (Hermitian) -> real output (n samples, scaled 1/N). scratch: n complex.
    void inverseReal(const cf* bins, float* out, cf* scratch) const;

private:
    void transform(cf* data, bool inv) const;
    int n_ = 0, logn_ = 0;
    std::vector<int> rev_;
    std::vector<cf> tw_;
};

} // namespace mfx
