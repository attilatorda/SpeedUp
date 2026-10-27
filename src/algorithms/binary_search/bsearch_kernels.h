/* Search kernels shared by the library and benchmarks/binary_search.
 * Plain C11 and valid C++; every function is static inline so callers can
 * specialise the batch width at compile time. */
#pragma once
#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#define SPEEDUP_PREFETCH(p) _mm_prefetch((const char*)(p), _MM_HINT_T0)
#elif defined(__GNUC__) || defined(__clang__)
#define SPEEDUP_PREFETCH(p) __builtin_prefetch((p), 0, 3)
#else
#define SPEEDUP_PREFETCH(p) ((void)(p))
#endif

#define SPEEDUP_BATCH_MAX 32

/* Turns a lower bound into an exact-match result: the first index holding
 * key, or -1. */
static inline int64_t speedup_lb_to_match(const int64_t* array, int64_t key, int64_t size, int64_t lb) {
    return (lb < size && array[lb] == key) ? lb : -1;
}

/* Branchless lower bound (Khuong and Morin 2017): the loop runs
 * ceil(log2 n) times whatever the key, and the only data-dependent step is a
 * select, which compilers turn into cmov. Returns the first index i with
 * array[i] >= key, or size when there is none. */
static inline int64_t speedup_lb_branchless(const int64_t* array, int64_t key, int64_t size) {
    if (size <= 0) return 0;
    const int64_t* base = array;
    int64_t n = size;
    while (n > 1) {
        int64_t half = n >> 1;
        base = (base[half] < key) ? base + half : base;
        n -= half;
    }
    return (int64_t)(base - array) + (*base < key);
}

/* As above, and before each comparison it prefetches both places the next
 * comparison can land, so the load for the next step is already on its way
 * when the select resolves. */
static inline int64_t speedup_lb_branchless_prefetch(const int64_t* array, int64_t key, int64_t size) {
    if (size <= 0) return 0;
    const int64_t* base = array;
    int64_t n = size;
    while (n > 1) {
        int64_t half = n >> 1;
        int64_t next_half = (n - half) >> 1;
        SPEEDUP_PREFETCH(base + next_half);
        SPEEDUP_PREFETCH(base + half + next_half);
        base = (base[half] < key) ? base + half : base;
        n -= half;
    }
    return (int64_t)(base - array) + (*base < key);
}

/* Interleaved search: k keys walk down the same array in lockstep. Every
 * key takes the same number of steps, so the loop needs no per-key
 * bookkeeping, and the k loads of one step are independent of each other.
 * With prefetch set, each key's real next probe is prefetched as soon as its
 * select resolves. With exact set, results are exact-match results (first
 * index or -1), computed while the group's last lines are still in cache.
 * Callers pass constants, so the flags cost nothing once inlined.
 * The step is mask arithmetic, not a select: when a compiler does not unroll
 * the lane loop, it tends to compile a select into a branch, and half of
 * those mispredict (GCC 13 does this at k = 32). */
static inline void speedup_lb_batch(const int64_t* array, int64_t size,
                                    const int64_t* keys, int64_t count,
                                    int64_t* results, int k, int prefetch, int exact) {
    int64_t i = 0;
    if (size <= 0) {
        for (; i < count; i++) results[i] = exact ? -1 : 0;
        return;
    }
    for (; i + k <= count; i += k) {
        int64_t pos[SPEEDUP_BATCH_MAX];
        for (int j = 0; j < k; j++) pos[j] = 0;
        int64_t n = size;
        while (n > 1) {
            int64_t half = n >> 1;
            int64_t next_half = (n - half) >> 1;
            for (int j = 0; j < k; j++) {
                pos[j] += half & -(int64_t)(array[pos[j] + half] < keys[i + j]);
                if (prefetch) SPEEDUP_PREFETCH(array + pos[j] + next_half);
            }
            n -= half;
        }
        for (int j = 0; j < k; j++) {
            int64_t lb = pos[j] + (array[pos[j]] < keys[i + j]);
            results[i + j] = exact ? speedup_lb_to_match(array, keys[i + j], size, lb) : lb;
        }
    }
    for (; i < count; i++) {
        int64_t lb = speedup_lb_branchless(array, keys[i], size);
        results[i] = exact ? speedup_lb_to_match(array, keys[i], size, lb) : lb;
    }
}
