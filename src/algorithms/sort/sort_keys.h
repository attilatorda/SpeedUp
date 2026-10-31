#pragma once
/* Order-preserving unsigned encodings of signed and floating-point values,
 * as in include/speedup/bucketization/keys.hpp: the numeric kernels sort
 * unsigned integers only, and the typed entry points encode in place
 * before and decode after.
 *
 * One difference from keys.hpp: NaNs are not made canonical, because the
 * encoding here has to be undone. Negative NaNs sort first, positive NaNs
 * last (IEEE 754 totalOrder, Rust's f64::total_cmp). */
#include <stddef.h>
#include <stdint.h>

#define SU_SIGN64 0x8000000000000000ULL
#define SU_SIGN32 0x80000000U

enum { SU_KEY_UNSIGNED = 0, SU_KEY_SIGNED = 1, SU_KEY_FLOAT = 2 };

static inline uint64_t su_enc_f64(uint64_t bits) { return (bits & SU_SIGN64) ? ~bits : (bits | SU_SIGN64); }
static inline uint64_t su_dec_f64(uint64_t key) { return (key & SU_SIGN64) ? (key ^ SU_SIGN64) : ~key; }
static inline uint32_t su_enc_f32(uint32_t bits) { return (bits & SU_SIGN32) ? ~bits : (bits | SU_SIGN32); }
static inline uint32_t su_dec_f32(uint32_t key) { return (key & SU_SIGN32) ? (key ^ SU_SIGN32) : ~key; }

static inline void su_encode64(uint64_t* a, size_t n, int mode) {
    if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] ^= SU_SIGN64;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_enc_f64(a[i]);
    }
}

static inline void su_decode64(uint64_t* a, size_t n, int mode) {
    if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] ^= SU_SIGN64;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_dec_f64(a[i]);
    }
}

static inline void su_encode32(uint32_t* a, size_t n, int mode) {
    if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] ^= SU_SIGN32;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_enc_f32(a[i]);
    }
}

static inline void su_decode32(uint32_t* a, size_t n, int mode) {
    if (mode == SU_KEY_SIGNED) {
        for (size_t i = 0; i < n; i++) a[i] ^= SU_SIGN32;
    } else if (mode == SU_KEY_FLOAT) {
        for (size_t i = 0; i < n; i++) a[i] = su_dec_f32(a[i]);
    }
}
