#pragma once
/* Bitonic sorting network for any n, in the form where every comparator
 * puts the smaller value at the lower index.
 *
 * For each block size k = 2, 4, ..., N (N = n rounded up to a power of
 * two) a "flip" stage compares i with its mirror in the block, i ^ (k-1),
 * and half-cleaner stages j = k/4, ..., 1 compare i with i + j. Picture the
 * array padded to N with +infinity: a comparator whose upper index is >= n
 * would compare against +infinity and never move anything, so it is simply
 * skipped. That is what makes the network exact for any n without padding
 * memory. */
#include <stddef.h>
#include <stdint.h>

static inline size_t su_pow2_ceil(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

#define SU_DEFINE_BITONIC_SCALAR(T, SFX)                                               \
    static inline void su_cmpswap_##SFX(T* a, size_t i, size_t l) {                    \
        T x = a[i], y = a[l];                                                          \
        a[i] = x < y ? x : y;                                                          \
        a[l] = x < y ? y : x;                                                          \
    }                                                                                  \
    /* Flip stage of block size k on the pairs of block [b, b+k) only. */             \
    static inline void su_flip_block_##SFX(T* a, size_t n, size_t b, size_t k) {       \
        size_t first = (b + k > n) ? b + k - n : 0;                                    \
        for (size_t i = first; i < k / 2; i++) su_cmpswap_##SFX(a, b + i, b + k - 1 - i); \
    }                                                                                  \
    /* Half-cleaner of distance j on block [b, b+2j). */                              \
    static inline void su_half_block_##SFX(T* a, size_t n, size_t b, size_t j) {       \
        if (b + j >= n) return;                                                        \
        size_t count = (b + 2 * j > n) ? n - b - j : j;                                \
        for (size_t i = 0; i < count; i++) su_cmpswap_##SFX(a, b + i, b + i + j);      \
    }                                                                                  \
    static inline void su_flip_##SFX(T* a, size_t n, size_t k) {                       \
        for (size_t b = 0; b < n; b += k) su_flip_block_##SFX(a, n, b, k);             \
    }                                                                                  \
    static inline void su_half_##SFX(T* a, size_t n, size_t j) {                       \
        for (size_t b = 0; b < n; b += 2 * j) su_half_block_##SFX(a, n, b, j);         \
    }

SU_DEFINE_BITONIC_SCALAR(uint32_t, u32)
SU_DEFINE_BITONIC_SCALAR(uint64_t, u64)

void speedup_internal_bitonic_scalar_u32(uint32_t* a, size_t n);
void speedup_internal_bitonic_scalar_u64(uint64_t* a, size_t n);
#ifdef SPEEDUP_SORT_HAVE_AVX2
void speedup_internal_bitonic_avx2_u32(uint32_t* a, size_t n);
void speedup_internal_bitonic_avx2_u64(uint64_t* a, size_t n);
/* Merges sorted runs a and b into out. na and nb are multiples of the
 * vector width (8 keys for u32, 4 for u64) and at least one vector. */
void speedup_internal_merge_avx2_u32(const uint32_t* a, size_t na, const uint32_t* b, size_t nb, uint32_t* out);
void speedup_internal_merge_avx2_u64(const uint64_t* a, size_t na, const uint64_t* b, size_t nb, uint64_t* out);
#endif

/* 1 when the AVX2 kernels may be used (CPU support and speedup_sort_set_simd). */
int speedup_internal_sort_use_avx2(void);
