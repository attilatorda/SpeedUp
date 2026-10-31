/* Portable bitonic sort (bitonic_net.h). The half-cleaner loops run over
 * contiguous ranges, which compilers can vectorize where the target has
 * unsigned min/max instructions. */
#include "bitonic_net.h"

void speedup_internal_bitonic_scalar_u32(uint32_t* a, size_t n) {
    const size_t top = su_pow2_ceil(n);
    for (size_t k = 2; k <= top; k <<= 1) {
        su_flip_u32(a, n, k);
        for (size_t j = k >> 2; j > 0; j >>= 1) su_half_u32(a, n, j);
    }
}

void speedup_internal_bitonic_scalar_u64(uint64_t* a, size_t n) {
    const size_t top = su_pow2_ceil(n);
    for (size_t k = 2; k <= top; k <<= 1) {
        su_flip_u64(a, n, k);
        for (size_t j = k >> 2; j > 0; j >>= 1) su_half_u64(a, n, j);
    }
}
