#pragma once
/* The key-to-bucket map of the bucket sorts (bucket_sort.c,
 * bitonic_bucket_sort.c).
 *
 * The key range is cut into SU_SEGMENTS segments of equal depth (splitters
 * from a sorted sample), and each segment into `per_segment` equal-width
 * buckets. The map is monotone in the key, so bucket order is key order. */
#include <stddef.h>
#include <stdint.h>

#define SU_SEGMENTS 64        /* power of two: the segment search is branchless */

typedef struct su_bucket_map {
    uint64_t split[SU_SEGMENTS - 1]; /* segment j holds split[j-1] <= x < split[j] */
    uint64_t lo[SU_SEGMENTS];
    double scale[SU_SEGMENTS];
    size_t per_segment;
} su_bucket_map;

static inline size_t su_bucket_of(const su_bucket_map* m, uint64_t x) {
    size_t j = 0;
    for (size_t step = SU_SEGMENTS / 2; step > 0; step >>= 1) {
        j += (m->split[j + step - 1] <= x) ? step : 0;
    }
    size_t sub = (size_t)((double)(x - m->lo[j]) * m->scale[j]);
    if (sub >= m->per_segment) sub = m->per_segment - 1;
    return j * m->per_segment + sub;
}

/* Builds the map for keys in [lo, hi] from a sorted sample of s keys. */
static inline void su_bucket_map_init(su_bucket_map* m, const uint64_t* sample, size_t s, uint64_t lo, uint64_t hi,
                                      size_t per_segment) {
    m->per_segment = per_segment;
    for (size_t j = 1; j < SU_SEGMENTS; j++) m->split[j - 1] = sample[j * s / SU_SEGMENTS];
    for (size_t j = 0; j < SU_SEGMENTS; j++) {
        uint64_t seg_lo = j ? m->split[j - 1] : lo;
        uint64_t seg_hi = (j + 1 < SU_SEGMENTS) ? m->split[j] : hi;
        if (seg_lo < lo) seg_lo = lo;
        if (seg_hi < seg_lo) seg_hi = seg_lo;
        m->lo[j] = seg_lo;
        m->scale[j] = (double)per_segment / ((double)(seg_hi - seg_lo) + 1.0);
    }
}
