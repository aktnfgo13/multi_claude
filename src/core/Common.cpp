#include "core/Common.h"

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
#include <xmmintrin.h>
#define MFX_HAS_SSE 1
#endif

namespace mfx {

ScopedNoDenormals::ScopedNoDenormals() {
#if MFX_HAS_SSE
    saved_ = _mm_getcsr();
    _mm_setcsr(saved_ | 0x8040); // FTZ | DAZ
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    saved_ = (unsigned int)fpcr;
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr | (1ull << 24)));
#endif
}

ScopedNoDenormals::~ScopedNoDenormals() {
#if MFX_HAS_SSE
    _mm_setcsr(saved_);
#elif defined(__aarch64__)
    uint64_t fpcr = saved_;
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr));
#endif
}

} // namespace mfx
