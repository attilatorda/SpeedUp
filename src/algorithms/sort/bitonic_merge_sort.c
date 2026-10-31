/* Bitonic merge sort: the bitonic network for small blocks, merging above.
 *
 * 1. Blocks of SU_BMS_BLOCK (128) keys are sorted by the bitonic network
 *    (bitonic_net.h; the AVX2 kernel when available).
 * 2. Sorted runs are merged pairwise, first inside chunks of SU_BMS_CHUNK
 *    keys, which stay in the L2 cache, then across the whole array. With
 *    AVX2 a merge step takes one vector of keys at a time and merges it
 *    with the largest keys so far in registers (speedup_internal_merge_avx2_*);
 *    without it, a branch-free scalar merge.
 * The vector path needs runs whose lengths are multiples of the vector
 * width, so the last n mod 8 keys are sorted apart and merged in at the end.
 * Not stable (equal keys are indistinguishable for these key types). */
#include <stdlib.h>
#include <string.h>
#include "speedup/algorithms/sort.h"
#include "bitonic_net.h"
#include "sort_common.h"
#include "sort_keys.h"

#ifndef SU_BMS_BLOCK
#define SU_BMS_BLOCK 128          /* keys per bitonic block (power of two, >= 8); tune031: 16..256 within 5 % */
#endif
#ifndef SU_BMS_CHUNK
#define SU_BMS_CHUNK 32768        /* keys merged inside the cache before the global passes */
#endif
#define SU_BMS_MIN 256            /* below this, introsort */

#define SU_DEFINE_BMS(T, SFX, W)                                                                        \
    static void su_merge_scalar_##SFX(const T* a, size_t na, const T* b, size_t nb, T* out) {           \
        while (na && nb) {                                                                              \
            T x = *a, y = *b;                                                                           \
            int take_b = y < x;                                                                         \
            *out++ = take_b ? y : x;                                                                    \
            a += !take_b;                                                                               \
            b += take_b;                                                                                \
            na -= (size_t)!take_b;                                                                      \
            nb -= (size_t)take_b;                                                                       \
        }                                                                                               \
        memcpy(out, a, na * sizeof(T));                                                                 \
        memcpy(out + na, b, nb * sizeof(T));                                                            \
    }                                                                                                   \
    static void su_merge_##SFX(const T* a, size_t na, const T* b, size_t nb, T* out, int simd) {        \
        if (simd && na % (W) == 0 && nb % (W) == 0 && na && nb) {                                       \
            su_merge_vec_##SFX(a, na, b, nb, out);                                                      \
        } else {                                                                                        \
            su_merge_scalar_##SFX(a, na, b, nb, out);                                                   \
        }                                                                                               \
    }                                                                                                   \
    /* One pass: merges runs of `width` from src into runs of 2 x width in dst. */                     \
    static void su_merge_pass_##SFX(const T* src, T* dst, size_t n, size_t width, int simd) {           \
        for (size_t i = 0; i < n; i += 2 * width) {                                                     \
            size_t mid = i + width < n ? i + width : n;                                                 \
            size_t end = i + 2 * width < n ? i + 2 * width : n;                                         \
            if (mid >= end) {                                                                           \
                memcpy(dst + i, src + i, (end - i) * sizeof(T));                                        \
            } else {                                                                                    \
                su_merge_##SFX(src + i, mid - i, src + mid, end - mid, dst + i, simd);                  \
            }                                                                                           \
        }                                                                                               \
    }                                                                                                   \
    /* Merges runs of `width` in a[0, n) until one run is left; the result ends in a. */               \
    static void su_merge_all_##SFX(T* a, T* tmp, size_t n, size_t width, int simd) {                    \
        T* src = a;                                                                                     \
        T* dst = tmp;                                                                                   \
        for (; width < n; width *= 2) {                                                                 \
            su_merge_pass_##SFX(src, dst, n, width, simd);                                              \
            T* t = src; src = dst; dst = t;                                                             \
        }                                                                                               \
        if (src != a) memcpy(a, src, n * sizeof(T));                                                    \
    }                                                                                                   \
    static void su_bms_core_##SFX(T* a, size_t n, T* tmp, int simd) {                                   \
        for (size_t c = 0; c < n; c += SU_BMS_CHUNK) {                                                  \
            size_t len = n - c < SU_BMS_CHUNK ? n - c : SU_BMS_CHUNK;                                   \
            for (size_t i = 0; i < len; i += SU_BMS_BLOCK) {                                            \
                size_t m = len - i < SU_BMS_BLOCK ? len - i : SU_BMS_BLOCK;                             \
                su_block_sort_##SFX(a + c + i, m, simd);                                                \
            }                                                                                           \
            su_merge_all_##SFX(a + c, tmp + c, len, SU_BMS_BLOCK, simd);                                \
        }                                                                                               \
        su_merge_all_##SFX(a, tmp, n, SU_BMS_CHUNK, simd);                                              \
    }                                                                                                   \
    static void su_bms_##SFX(T* a, size_t n, T* scratch, int mode) {                                   \
        su_encode_##SFX(a, n, mode);                                                                    \
        T* tmp = scratch ? scratch : (T*)malloc(n * sizeof(T));                                         \
        if (n < SU_BMS_MIN || !tmp) {                                                                   \
            su_introsort_##SFX(a, n);                                                                   \
        } else {                                                                                        \
            const int simd = speedup_internal_sort_use_avx2();                                          \
            const size_t body = simd ? n - n % (W) : n;                                                 \
            su_bms_core_##SFX(a, body, tmp, simd);                                                      \
            if (body < n) {                                                                             \
                /* the last n mod W keys: sort them and merge them in */                                \
                su_insertion_##SFX(a + body, n - body);                                                 \
                su_merge_scalar_##SFX(a, body, a + body, n - body, tmp);                                \
                memcpy(a, tmp, n * sizeof(T));                                                          \
            }                                                                                           \
        }                                                                                               \
        if (!scratch) free(tmp);                                                                        \
        su_decode_##SFX(a, n, mode);                                                                    \
    }

static inline void su_encode_u32(uint32_t* a, size_t n, int mode) { su_encode32(a, n, mode); }
static inline void su_decode_u32(uint32_t* a, size_t n, int mode) { su_decode32(a, n, mode); }
static inline void su_encode_u64(uint64_t* a, size_t n, int mode) { su_encode64(a, n, mode); }
static inline void su_decode_u64(uint64_t* a, size_t n, int mode) { su_decode64(a, n, mode); }

#ifdef SPEEDUP_SORT_HAVE_AVX2
static void su_merge_vec_u32(const uint32_t* a, size_t na, const uint32_t* b, size_t nb, uint32_t* out) {
    speedup_internal_merge_avx2_u32(a, na, b, nb, out);
}
static void su_merge_vec_u64(const uint64_t* a, size_t na, const uint64_t* b, size_t nb, uint64_t* out) {
    speedup_internal_merge_avx2_u64(a, na, b, nb, out);
}
static void su_block_sort_u32(uint32_t* a, size_t n, int simd) {
    if (simd) speedup_internal_bitonic_avx2_u32(a, n); else speedup_internal_bitonic_scalar_u32(a, n);
}
static void su_block_sort_u64(uint64_t* a, size_t n, int simd) {
    if (simd) speedup_internal_bitonic_avx2_u64(a, n); else speedup_internal_bitonic_scalar_u64(a, n);
}
#else
/* never called: simd is 0 without AVX2 kernels */
static void su_merge_vec_u32(const uint32_t* a, size_t na, const uint32_t* b, size_t nb, uint32_t* out) {
    (void)a; (void)na; (void)b; (void)nb; (void)out;
}
static void su_merge_vec_u64(const uint64_t* a, size_t na, const uint64_t* b, size_t nb, uint64_t* out) {
    (void)a; (void)na; (void)b; (void)nb; (void)out;
}
static void su_block_sort_u32(uint32_t* a, size_t n, int simd) { (void)simd; speedup_internal_bitonic_scalar_u32(a, n); }
static void su_block_sort_u64(uint64_t* a, size_t n, int simd) { (void)simd; speedup_internal_bitonic_scalar_u64(a, n); }
#endif

SU_DEFINE_BMS(uint32_t, u32, 8)
SU_DEFINE_BMS(uint64_t, u64, 4)

void speedup_bitonic_merge_sort_u32(uint32_t* data, size_t n, void* scratch) {
    su_bms_u32(data, n, (uint32_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_bitonic_merge_sort_i32(int32_t* data, size_t n, void* scratch) {
    su_bms_u32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_SIGNED);
}
void speedup_bitonic_merge_sort_f32(float* data, size_t n, void* scratch) {
    su_bms_u32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_FLOAT);
}
void speedup_bitonic_merge_sort_u64(uint64_t* data, size_t n, void* scratch) {
    su_bms_u64(data, n, (uint64_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_bitonic_merge_sort_i64(int64_t* data, size_t n, void* scratch) {
    su_bms_u64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_SIGNED);
}
void speedup_bitonic_merge_sort_f64(double* data, size_t n, void* scratch) {
    su_bms_u64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_FLOAT);
}
