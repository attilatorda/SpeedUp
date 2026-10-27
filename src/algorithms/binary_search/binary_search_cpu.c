#include "speedup/algorithms/binary_search.h"
#include "bsearch_kernels.h"

/* Choices from benchmarks/outputs/bsearch_*.csv (docs/papers/binary-search):
 * - single key: plain branchless while the array fits the L2 cache hint, where
 *   the prefetches only add work; branchless with prefetching beyond it, where
 *   plain branchless is slower than even the branchy reference;
 * - many keys: 16 interleaved searches, without prefetching. */
#define SPEEDUP_BATCH_WIDTH 16

extern int64_t speedup_internal_l2_elements; /* src/core/dispatch.c */

static int64_t speedup_lower_bound_cpu(const int64_t* array, int64_t key, int64_t size) {
    if (size <= speedup_internal_l2_elements) {
        return speedup_lb_branchless(array, key, size);
    }
    return speedup_lb_branchless_prefetch(array, key, size);
}

int64_t speedup_binary_search_i64_cpu(const int64_t* array, int64_t key, int64_t size) {
    return speedup_lb_to_match(array, key, size, speedup_lower_bound_cpu(array, key, size));
}

int64_t speedup_lower_bound_i64(const int64_t* array, int64_t key, int64_t size) {
    return speedup_lower_bound_cpu(array, key, size);
}

void speedup_binary_search_batch_i64(const int64_t* array, int64_t size,
                                     const int64_t* keys, int64_t count, int64_t* results) {
    speedup_lb_batch(array, size, keys, count, results, SPEEDUP_BATCH_WIDTH, 0, 1);
}

void speedup_lower_bound_batch_i64(const int64_t* array, int64_t size,
                                   const int64_t* keys, int64_t count, int64_t* results) {
    speedup_lb_batch(array, size, keys, count, results, SPEEDUP_BATCH_WIDTH, 0, 0);
}
