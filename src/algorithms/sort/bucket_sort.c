/* Bucket sort on unsigned keys.
 *
 * Plain bucket sort cuts [min, max] into equal-width buckets, which only
 * works for uniform data. Here the range is first cut into SU_SEGMENTS
 * segments of equal depth (splitters taken from a sorted sample), and
 * each segment into equal-width buckets. Skewed data then spreads like
 * uniform data, as long as it is smooth at the scale of a segment. The
 * key-to-bucket map is monotone, so after one counting pass, one scatter
 * pass and a comparison sort per bucket the data is sorted. A bucket that
 * the sample misjudges just costs a larger comparison sort. */
#include <stdlib.h>
#include <string.h>
#include "speedup/algorithms/sort.h"
#include "sort_bucket_map.h"
#include "sort_common.h"
#include "sort_keys.h"

#define SU_BUCKET_MIN 512
#define SU_BUCKET_TARGET 32   /* elements per bucket the plan aims for */
#define SU_BUCKET_SAMPLE 1024

static void su_bucket64(uint64_t* a, size_t n, uint64_t* scratch, int mode) {
    su_encode64(a, n, mode);
    if (n < SU_BUCKET_MIN) {
        su_introsort_u64(a, n);
        su_decode64(a, n, mode);
        return;
    }
    uint64_t lo = a[0], hi = a[0];
    for (size_t i = 1; i < n; i++) {
        lo = a[i] < lo ? a[i] : lo;
        hi = a[i] > hi ? a[i] : hi;
    }
    if (lo == hi) {
        su_decode64(a, n, mode);
        return;
    }

    size_t buckets = n / SU_BUCKET_TARGET;
    if (buckets > 65536) buckets = 65536;
    const size_t per_segment = buckets / SU_SEGMENTS ? buckets / SU_SEGMENTS : 1;
    buckets = per_segment * SU_SEGMENTS;

    uint64_t* buf = scratch ? scratch : (uint64_t*)malloc(n * sizeof(uint64_t));
    size_t* start = (size_t*)calloc(buckets + 1, sizeof(size_t));
    if (!buf || !start) {
        if (!scratch) free(buf);
        free(start);
        su_introsort_u64(a, n);
        su_decode64(a, n, mode);
        return;
    }

    /* Equal-depth splitters from an evenly strided sample. */
    uint64_t sample[SU_BUCKET_SAMPLE];
    const size_t s = SU_BUCKET_SAMPLE, stride = n / SU_BUCKET_SAMPLE;
    for (size_t i = 0; i < s; i++) sample[i] = a[i * stride + stride / 2];
    su_introsort_u64(sample, s);
    su_bucket_map map;
    su_bucket_map_init(&map, sample, s, lo, hi, per_segment);

    for (size_t i = 0; i < n; i++) start[su_bucket_of(&map, a[i]) + 1]++;
    for (size_t b = 0; b < buckets; b++) start[b + 1] += start[b];
    /* start[b] is now where bucket b begins; scatter advances a copy of it */
    size_t* next = (size_t*)malloc(buckets * sizeof(size_t));
    if (next) {
        memcpy(next, start, buckets * sizeof(size_t));
        for (size_t i = 0; i < n; i++) {
            uint64_t k = a[i];
            buf[next[su_bucket_of(&map, k)]++] = k;
        }
        free(next);
        for (size_t b = 0; b < buckets; b++) su_introsort_u64(buf + start[b], start[b + 1] - start[b]);
        if (mode == SU_KEY_SIGNED) {
            for (size_t i = 0; i < n; i++) a[i] = buf[i] ^ SU_SIGN64;
        } else if (mode == SU_KEY_FLOAT) {
            for (size_t i = 0; i < n; i++) a[i] = su_dec_f64(buf[i]);
        } else {
            memcpy(a, buf, n * sizeof(uint64_t));
        }
    } else {
        su_introsort_u64(a, n);
        su_decode64(a, n, mode);
    }
    free(start);
    if (!scratch) free(buf);
}

void speedup_bucket_sort_u64(uint64_t* data, size_t n, void* scratch) {
    su_bucket64(data, n, (uint64_t*)scratch, SU_KEY_UNSIGNED);
}
void speedup_bucket_sort_i64(int64_t* data, size_t n, void* scratch) {
    su_bucket64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_SIGNED);
}
void speedup_bucket_sort_f64(double* data, size_t n, void* scratch) {
    su_bucket64((uint64_t*)data, n, (uint64_t*)scratch, SU_KEY_FLOAT);
}
