/* LSD radix sort on unsigned keys, 8-bit digits.
 *
 * One read pass encodes the keys in place and builds the histogram of every
 * digit at once. A digit whose histogram has a single full bin (all keys
 * agree on it) is skipped, so 64-bit keys that only use their low bytes, or
 * share their high bytes, take fewer passes. The data then moves between the
 * input and the scratch buffer once per remaining digit; the last move back
 * into the input decodes as it copies. */
#include <stdlib.h>
#include <string.h>
#include "speedup/algorithms/sort.h"
#include "sort_common.h"
#include "sort_keys.h"

/* Below this, building and summing 256-bin histograms costs more than a
 * comparison sort. */
#define SU_RADIX_MIN 256

static void su_fallback64(uint64_t* a, size_t n, int mode) {
    su_encode64(a, n, mode);
    su_introsort_u64(a, n);
    su_decode64(a, n, mode);
}

static void su_fallback32(uint32_t* a, size_t n, int mode) {
    su_encode32(a, n, mode);
    su_introsort_u32(a, n);
    su_decode32(a, n, mode);
}

static void su_radix64(uint64_t* a, size_t n, uint64_t* scratch, int mode) {
    if (n < SU_RADIX_MIN) {
        su_fallback64(a, n, mode);
        return;
    }
    uint64_t* buf = scratch ? scratch : (uint64_t*)malloc(n * sizeof(uint64_t));
    if (!buf) {
        su_fallback64(a, n, mode);
        return;
    }
    size_t hist[8][256];
    memset(hist, 0, sizeof hist);
#define SU_HIST64(ENC)                                                        \
    for (size_t i = 0; i < n; i++) {                                          \
        uint64_t k = ENC;                                                     \
        a[i] = k;                                                             \
        for (int d = 0; d < 8; d++) hist[d][(k >> (8 * d)) & 0xFF]++;         \
    }
    if (mode == SU_KEY_SIGNED) {
        SU_HIST64(a[i] ^ SU_SIGN64)
    } else if (mode == SU_KEY_FLOAT) {
        SU_HIST64(su_enc_f64(a[i]))
    } else {
        SU_HIST64(a[i])
    }
#undef SU_HIST64
    uint64_t* src = a;
    uint64_t* dst = buf;
    for (int d = 0; d < 8; d++) {
        const unsigned shift = 8 * (unsigned)d;
        if (hist[d][(a[0] >> shift) & 0xFF] == n) continue;
        size_t offset = 0;
        for (int b = 0; b < 256; b++) {
            size_t c = hist[d][b];
            hist[d][b] = offset;
            offset += c;
        }
        for (size_t i = 0; i < n; i++) {
            uint64_t k = src[i];
            dst[hist[d][(k >> shift) & 0xFF]++] = k;
        }
        uint64_t* t = src; src = dst; dst = t;
    }
    if (src == a) {
        su_decode64(a, n, mode);
    } else if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] = src[i] ^ SU_SIGN64;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_dec_f64(src[i]);
    } else {
        memcpy(a, src, n * sizeof(uint64_t));
    }
    if (!scratch) free(buf);
}

static void su_radix32(uint32_t* a, size_t n, uint32_t* scratch, int mode) {
    if (n < SU_RADIX_MIN) {
        su_fallback32(a, n, mode);
        return;
    }
    uint32_t* buf = scratch ? scratch : (uint32_t*)malloc(n * sizeof(uint32_t));
    if (!buf) {
        su_fallback32(a, n, mode);
        return;
    }
    size_t hist[4][256];
    memset(hist, 0, sizeof hist);
#define SU_HIST32(ENC)                                                        \
    for (size_t i = 0; i < n; i++) {                                          \
        uint32_t k = ENC;                                                     \
        a[i] = k;                                                             \
        hist[0][k & 0xFF]++;                                                  \
        hist[1][(k >> 8) & 0xFF]++;                                           \
        hist[2][(k >> 16) & 0xFF]++;                                          \
        hist[3][k >> 24]++;                                                   \
    }
    if (mode == SU_KEY_SIGNED) {
        SU_HIST32(a[i] ^ SU_SIGN32)
    } else if (mode == SU_KEY_FLOAT) {
        SU_HIST32(su_enc_f32(a[i]))
    } else {
        SU_HIST32(a[i])
    }
#undef SU_HIST32
    uint32_t* src = a;
    uint32_t* dst = buf;
    for (int d = 0; d < 4; d++) {
        const unsigned shift = 8 * (unsigned)d;
        if (hist[d][(a[0] >> shift) & 0xFF] == n) continue;
        size_t offset = 0;
        for (int b = 0; b < 256; b++) {
            size_t c = hist[d][b];
            hist[d][b] = offset;
            offset += c;
        }
        for (size_t i = 0; i < n; i++) {
            uint32_t k = src[i];
            dst[hist[d][(k >> shift) & 0xFF]++] = k;
        }
        uint32_t* t = src; src = dst; dst = t;
    }
    if (src == a) {
        su_decode32(a, n, mode);
    } else if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] = src[i] ^ SU_SIGN32;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_dec_f32(src[i]);
    } else {
        memcpy(a, src, n * sizeof(uint32_t));
    }
    if (!scratch) free(buf);
}

/* Floats and signed integers are sorted through their bit patterns, which
 * the encodings above map onto unsigned order. */
void speedup_radix_sort_u64(uint64_t* data, size_t n, void* scratch) {
    su_radix64(data, n, (uint64_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_radix_sort_i64(int64_t* data, size_t n, void* scratch) {
    su_radix64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_SIGNED);
}
void speedup_radix_sort_f64(double* data, size_t n, void* scratch) {
    su_radix64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_FLOAT);
}
void speedup_radix_sort_u32(uint32_t* data, size_t n, void* scratch) {
    su_radix32(data, n, (uint32_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_radix_sort_i32(int32_t* data, size_t n, void* scratch) {
    su_radix32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_SIGNED);
}
void speedup_radix_sort_f32(float* data, size_t n, void* scratch) {
    su_radix32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_FLOAT);
}
