/* Bitonic bucket sort: the bucket sort of bucket_sort.c with power-of-two
 * target bucket sizes, every bucket finished by the bitonic network.
 *
 * Below SU_BBS_MIN keys it is bitonic merge sort: measured (tune031),
 * buckets pay only from about 64 Ki keys, and only for 32-bit keys.
 *
 * The keys are distributed into buckets of about SU_BBS_TARGET expected
 * keys (sampled equal-depth segments, equal-width buckets inside them;
 * sort_bucket_map.h). Each bucket is then sorted by the bitonic network
 * (the AVX2 kernel when available), which has no data-dependent branches
 * and suits these small, in-cache sizes. A bucket that the sample misjudged
 * beyond SU_BBS_NETWORK_MAX keys is sorted by introsort instead. */
#include <stdlib.h>
#include <string.h>
#include "speedup/algorithms/sort.h"
#include "bitonic_net.h"
#include "sort_bucket_map.h"
#include "sort_common.h"
#include "sort_keys.h"

#ifndef SU_BBS_TARGET
#define SU_BBS_TARGET 256         /* expected keys per bucket; tune031 tried 32..1024 */
#endif
#ifndef SU_BBS_NETWORK_MAX
#define SU_BBS_NETWORK_MAX 2048   /* larger buckets go to introsort */
#endif
#define SU_BBS_MIN 16384          /* below this, bitonic merge sort */
#define SU_BBS_SAMPLE 1024
#define SU_BBS_MAX_BUCKETS ((size_t)1 << 20)

static void su_network_u32(uint32_t* a, size_t n, int simd) {
#ifdef SPEEDUP_SORT_HAVE_AVX2
    if (simd) { speedup_internal_bitonic_avx2_u32(a, n); return; }
#endif
    (void)simd;
    speedup_internal_bitonic_scalar_u32(a, n);
}

static void su_network_u64(uint64_t* a, size_t n, int simd) {
#ifdef SPEEDUP_SORT_HAVE_AVX2
    if (simd) { speedup_internal_bitonic_avx2_u64(a, n); return; }
#endif
    (void)simd;
    speedup_internal_bitonic_scalar_u64(a, n);
}

#define SU_DEFINE_BBS(T, SFX, ENC, DEC)                                                                 \
    static void su_bbs_##SFX(T* a, size_t n, T* scratch, int mode) {                                    \
        const int simd = speedup_internal_sort_use_avx2();                                              \
        ENC(a, n, mode);                                                                                \
        if (n < SU_BBS_MIN) {                                                                           \
            /* the keys are encoded already: sort them as unsigned */                                   \
            speedup_bitonic_merge_sort_##SFX(a, n, scratch);                                            \
            DEC(a, n, mode);                                                                            \
            return;                                                                                     \
        }                                                                                               \
        T lo = a[0], hi = a[0];                                                                         \
        for (size_t i = 1; i < n; i++) {                                                                \
            lo = a[i] < lo ? a[i] : lo;                                                                 \
            hi = a[i] > hi ? a[i] : hi;                                                                 \
        }                                                                                               \
        if (lo == hi) {                                                                                 \
            DEC(a, n, mode);                                                                            \
            return;                                                                                     \
        }                                                                                               \
        size_t buckets = n / SU_BBS_TARGET;                                                             \
        if (buckets > SU_BBS_MAX_BUCKETS) buckets = SU_BBS_MAX_BUCKETS;                                 \
        const size_t per_segment = buckets / SU_SEGMENTS ? buckets / SU_SEGMENTS : 1;                   \
        buckets = per_segment * SU_SEGMENTS;                                                            \
        T* buf = scratch ? scratch : (T*)malloc(n * sizeof(T));                                         \
        size_t* start = (size_t*)calloc(buckets + 1, sizeof(size_t));                                   \
        size_t* next = (size_t*)malloc(buckets * sizeof(size_t));                                       \
        if (!buf || !start || !next) {                                                                  \
            if (!scratch) free(buf);                                                                    \
            free(start);                                                                                \
            free(next);                                                                                 \
            su_introsort_##SFX(a, n);                                                                   \
            DEC(a, n, mode);                                                                            \
            return;                                                                                     \
        }                                                                                               \
        uint64_t sample[SU_BBS_SAMPLE];                                                                 \
        const size_t stride = n / SU_BBS_SAMPLE;                                                        \
        for (size_t i = 0; i < SU_BBS_SAMPLE; i++) sample[i] = a[i * stride + stride / 2];             \
        su_introsort_u64(sample, SU_BBS_SAMPLE);                                                        \
        su_bucket_map map;                                                                              \
        su_bucket_map_init(&map, sample, SU_BBS_SAMPLE, lo, hi, per_segment);                           \
        for (size_t i = 0; i < n; i++) start[su_bucket_of(&map, a[i]) + 1]++;                          \
        for (size_t b = 0; b < buckets; b++) start[b + 1] += start[b];                                  \
        memcpy(next, start, buckets * sizeof(size_t));                                                  \
        for (size_t i = 0; i < n; i++) {                                                                \
            T k = a[i];                                                                                 \
            buf[next[su_bucket_of(&map, k)]++] = k;                                                     \
        }                                                                                               \
        for (size_t b = 0; b < buckets; b++) {                                                          \
            size_t len = start[b + 1] - start[b];                                                       \
            if (len < 2) continue;                                                                      \
            if (len <= SU_BBS_NETWORK_MAX) {                                                            \
                su_network_##SFX(buf + start[b], len, simd);                                            \
            } else {                                                                                    \
                su_introsort_##SFX(buf + start[b], len);                                                \
            }                                                                                           \
        }                                                                                               \
        memcpy(a, buf, n * sizeof(T));                                                                  \
        DEC(a, n, mode);                                                                                \
        free(start);                                                                                    \
        free(next);                                                                                     \
        if (!scratch) free(buf);                                                                        \
    }

SU_DEFINE_BBS(uint32_t, u32, su_encode32, su_decode32)
SU_DEFINE_BBS(uint64_t, u64, su_encode64, su_decode64)

void speedup_bitonic_bucket_sort_u32(uint32_t* data, size_t n, void* scratch) {
    su_bbs_u32(data, n, (uint32_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_bitonic_bucket_sort_i32(int32_t* data, size_t n, void* scratch) {
    su_bbs_u32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_SIGNED);
}
void speedup_bitonic_bucket_sort_f32(float* data, size_t n, void* scratch) {
    su_bbs_u32((uint32_t*)data, n, (uint32_t*)scratch, SU_KEY_FLOAT);
}
void speedup_bitonic_bucket_sort_u64(uint64_t* data, size_t n, void* scratch) {
    su_bbs_u64(data, n, (uint64_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_bitonic_bucket_sort_i64(int64_t* data, size_t n, void* scratch) {
    su_bbs_u64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_SIGNED);
}
void speedup_bitonic_bucket_sort_f64(double* data, size_t n, void* scratch) {
    su_bbs_u64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_FLOAT);
}
