/* AVX2 bitonic sort: the network of bitonic_net.h, a vector of 8 (32-bit)
 * or 4 (64-bit) keys at a time. Built with -mavx2 or /arch:AVX2 and only
 * called after speedup_cpu_has_avx2().
 *
 * - Half-cleaners with a distance of at least one vector compare two
 *   contiguous ranges: two loads, min and max, two stores.
 * - Flip stages with a half block of at least one vector compare a range
 *   with a mirrored range: the upper load is reversed in the register.
 * - Distances below one vector stay inside an aligned vector, so all of
 *   them (j, j/2, ..., 1) run in the register between one load and one
 *   store.
 * Comparators past the end of the array (bitonic_net.h) are skipped by
 * finishing partial vectors with the scalar code. */
#include <immintrin.h>
#include "bitonic_net.h"

/* ---- 32-bit keys, 8 per vector ---------------------------------------- */

static inline __m256i su_rev_u32(__m256i v) {
    return _mm256_permutevar8x32_epi32(v, _mm256_setr_epi32(7, 6, 5, 4, 3, 2, 1, 0));
}

static inline __m256i su_inreg_u32(__m256i v, size_t j) {
    __m256i p, lo, hi;
    switch (j) {
        case 4:
            p = _mm256_permute2x128_si256(v, v, 0x01);
            lo = _mm256_min_epu32(v, p); hi = _mm256_max_epu32(v, p);
            v = _mm256_blend_epi32(lo, hi, 0xF0);
            /* fall through */
        case 2:
            p = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
            lo = _mm256_min_epu32(v, p); hi = _mm256_max_epu32(v, p);
            v = _mm256_blend_epi32(lo, hi, 0xCC);
            /* fall through */
        case 1:
            p = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1));
            lo = _mm256_min_epu32(v, p); hi = _mm256_max_epu32(v, p);
            v = _mm256_blend_epi32(lo, hi, 0xAA);
            break;
        default: break;
    }
    return v;
}

void speedup_internal_bitonic_avx2_u32(uint32_t* a, size_t n) {
    if (n < 16) {
        speedup_internal_bitonic_scalar_u32(a, n);
        return;
    }
    const size_t top = su_pow2_ceil(n);
    for (size_t k = 2; k <= top; k <<= 1) {
        if (k / 2 >= 8) {
            for (size_t b = 0; b < n; b += k) {
                size_t i = (b + k > n) ? b + k - n : 0;
                for (; i + 8 <= k / 2; i += 8) {
                    uint32_t* lo_p = a + b + i;
                    uint32_t* hi_p = a + b + k - 8 - i;
                    __m256i x = _mm256_loadu_si256((const __m256i*)lo_p);
                    __m256i y = su_rev_u32(_mm256_loadu_si256((const __m256i*)hi_p));
                    _mm256_storeu_si256((__m256i*)lo_p, _mm256_min_epu32(x, y));
                    _mm256_storeu_si256((__m256i*)hi_p, su_rev_u32(_mm256_max_epu32(x, y)));
                }
                for (; i < k / 2; i++) su_cmpswap_u32(a, b + i, b + k - 1 - i);
            }
        } else {
            su_flip_u32(a, n, k);
        }
        size_t j = k >> 2;
        for (; j >= 8; j >>= 1) {
            for (size_t b = 0; b < n; b += 2 * j) {
                if (b + j >= n) break;
                size_t count = (b + 2 * j > n) ? n - b - j : j;
                size_t i = 0;
                for (; i + 8 <= count; i += 8) {
                    __m256i x = _mm256_loadu_si256((const __m256i*)(a + b + i));
                    __m256i y = _mm256_loadu_si256((const __m256i*)(a + b + i + j));
                    _mm256_storeu_si256((__m256i*)(a + b + i), _mm256_min_epu32(x, y));
                    _mm256_storeu_si256((__m256i*)(a + b + i + j), _mm256_max_epu32(x, y));
                }
                for (; i < count; i++) su_cmpswap_u32(a, b + i, b + i + j);
            }
        }
        if (j > 0) {
            size_t b = 0;
            for (; b + 8 <= n; b += 8) {
                __m256i v = _mm256_loadu_si256((const __m256i*)(a + b));
                _mm256_storeu_si256((__m256i*)(a + b), su_inreg_u32(v, j));
            }
            for (size_t jj = j; jj > 0; jj >>= 1) {
                for (size_t bb = b; bb < n; bb += 2 * jj) su_half_block_u32(a, n, bb, jj);
            }
        }
    }
}

/* ---- 64-bit keys, 4 per vector ---------------------------------------- */

/* AVX2 has no unsigned 64-bit compare: flip the sign bits and compare signed. */
static inline void su_minmax_u64(__m256i x, __m256i y, __m256i* mn, __m256i* mx) {
    const __m256i sign = _mm256_set1_epi64x((long long)0x8000000000000000ULL);
    __m256i gt = _mm256_cmpgt_epi64(_mm256_xor_si256(x, sign), _mm256_xor_si256(y, sign));
    *mn = _mm256_blendv_epi8(x, y, gt);
    *mx = _mm256_blendv_epi8(y, x, gt);
}

static inline __m256i su_rev_u64(__m256i v) { return _mm256_permute4x64_epi64(v, _MM_SHUFFLE(0, 1, 2, 3)); }

static inline __m256i su_inreg_u64(__m256i v, size_t j) {
    __m256i p, lo, hi;
    switch (j) {
        case 2:
            p = _mm256_permute4x64_epi64(v, _MM_SHUFFLE(1, 0, 3, 2));
            su_minmax_u64(v, p, &lo, &hi);
            v = _mm256_blend_epi32(lo, hi, 0xF0);
            /* fall through */
        case 1:
            p = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2));
            su_minmax_u64(v, p, &lo, &hi);
            v = _mm256_blend_epi32(lo, hi, 0xCC);
            break;
        default: break;
    }
    return v;
}

void speedup_internal_bitonic_avx2_u64(uint64_t* a, size_t n) {
    if (n < 8) {
        speedup_internal_bitonic_scalar_u64(a, n);
        return;
    }
    const size_t top = su_pow2_ceil(n);
    for (size_t k = 2; k <= top; k <<= 1) {
        if (k / 2 >= 4) {
            for (size_t b = 0; b < n; b += k) {
                size_t i = (b + k > n) ? b + k - n : 0;
                for (; i + 4 <= k / 2; i += 4) {
                    uint64_t* lo_p = a + b + i;
                    uint64_t* hi_p = a + b + k - 4 - i;
                    __m256i x = _mm256_loadu_si256((const __m256i*)lo_p);
                    __m256i y = su_rev_u64(_mm256_loadu_si256((const __m256i*)hi_p));
                    __m256i mn, mx;
                    su_minmax_u64(x, y, &mn, &mx);
                    _mm256_storeu_si256((__m256i*)lo_p, mn);
                    _mm256_storeu_si256((__m256i*)hi_p, su_rev_u64(mx));
                }
                for (; i < k / 2; i++) su_cmpswap_u64(a, b + i, b + k - 1 - i);
            }
        } else {
            su_flip_u64(a, n, k);
        }
        size_t j = k >> 2;
        for (; j >= 4; j >>= 1) {
            for (size_t b = 0; b < n; b += 2 * j) {
                if (b + j >= n) break;
                size_t count = (b + 2 * j > n) ? n - b - j : j;
                size_t i = 0;
                for (; i + 4 <= count; i += 4) {
                    __m256i x = _mm256_loadu_si256((const __m256i*)(a + b + i));
                    __m256i y = _mm256_loadu_si256((const __m256i*)(a + b + i + j));
                    __m256i mn, mx;
                    su_minmax_u64(x, y, &mn, &mx);
                    _mm256_storeu_si256((__m256i*)(a + b + i), mn);
                    _mm256_storeu_si256((__m256i*)(a + b + i + j), mx);
                }
                for (; i < count; i++) su_cmpswap_u64(a, b + i, b + i + j);
            }
        }
        if (j > 0) {
            size_t b = 0;
            for (; b + 4 <= n; b += 4) {
                __m256i v = _mm256_loadu_si256((const __m256i*)(a + b));
                _mm256_storeu_si256((__m256i*)(a + b), su_inreg_u64(v, j));
            }
            for (size_t jj = j; jj > 0; jj >>= 1) {
                for (size_t bb = b; bb < n; bb += 2 * jj) su_half_block_u64(a, n, bb, jj);
            }
        }
    }
}

/* ---- merging two sorted runs ------------------------------------------- */

/* The merge of Chhugani et al. (PVLDB 2008): keep the largest vector of
 * keys seen so far in a register; take the next vector from the run whose
 * head is smaller; merge the two vectors with a bitonic merge (reverse one,
 * min/max, then the in-register stages); write out the lower half. */

static inline void su_merge16_u32(__m256i* lo, __m256i* hi) {
    __m256i x = *lo, y = su_rev_u32(*hi);
    *lo = su_inreg_u32(_mm256_min_epu32(x, y), 4);
    *hi = su_inreg_u32(_mm256_max_epu32(x, y), 4);
}

void speedup_internal_merge_avx2_u32(const uint32_t* a, size_t na, const uint32_t* b, size_t nb, uint32_t* out) {
    const uint32_t* ae = a + na;
    const uint32_t* be = b + nb;
    __m256i v = _mm256_loadu_si256((const __m256i*)a);
    __m256i top = _mm256_loadu_si256((const __m256i*)b);
    a += 8;
    b += 8;
    su_merge16_u32(&v, &top);
    _mm256_storeu_si256((__m256i*)out, v);
    out += 8;
    while (a < ae && b < be) {
        if (*a < *b) {
            v = _mm256_loadu_si256((const __m256i*)a);
            a += 8;
        } else {
            v = _mm256_loadu_si256((const __m256i*)b);
            b += 8;
        }
        su_merge16_u32(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
        out += 8;
    }
    for (; a < ae; a += 8, out += 8) {
        v = _mm256_loadu_si256((const __m256i*)a);
        su_merge16_u32(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
    }
    for (; b < be; b += 8, out += 8) {
        v = _mm256_loadu_si256((const __m256i*)b);
        su_merge16_u32(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
    }
    _mm256_storeu_si256((__m256i*)out, top);
}

static inline void su_merge8_u64(__m256i* lo, __m256i* hi) {
    __m256i mn, mx;
    su_minmax_u64(*lo, su_rev_u64(*hi), &mn, &mx);
    *lo = su_inreg_u64(mn, 2);
    *hi = su_inreg_u64(mx, 2);
}

void speedup_internal_merge_avx2_u64(const uint64_t* a, size_t na, const uint64_t* b, size_t nb, uint64_t* out) {
    const uint64_t* ae = a + na;
    const uint64_t* be = b + nb;
    __m256i v = _mm256_loadu_si256((const __m256i*)a);
    __m256i top = _mm256_loadu_si256((const __m256i*)b);
    a += 4;
    b += 4;
    su_merge8_u64(&v, &top);
    _mm256_storeu_si256((__m256i*)out, v);
    out += 4;
    while (a < ae && b < be) {
        if (*a < *b) {
            v = _mm256_loadu_si256((const __m256i*)a);
            a += 4;
        } else {
            v = _mm256_loadu_si256((const __m256i*)b);
            b += 4;
        }
        su_merge8_u64(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
        out += 4;
    }
    for (; a < ae; a += 4, out += 4) {
        v = _mm256_loadu_si256((const __m256i*)a);
        su_merge8_u64(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
    }
    for (; b < be; b += 4, out += 4) {
        v = _mm256_loadu_si256((const __m256i*)b);
        su_merge8_u64(&v, &top);
        _mm256_storeu_si256((__m256i*)out, v);
    }
    _mm256_storeu_si256((__m256i*)out, top);
}
