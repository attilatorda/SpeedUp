#pragma once
/* Comparison sort for unsigned keys: the fallback of every numeric sort
 * (small inputs, failed allocations) and the per-bucket sort of bucket sort.
 * Introsort: median-of-three quicksort, insertion sort below
 * SU_INSERTION_MAX elements, heapsort past a depth of 2 log2 n. */
#include <stddef.h>
#include <stdint.h>

#define SU_INSERTION_MAX 24

static inline unsigned su_log2(size_t n) {
    unsigned r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

#define SU_DEFINE_INTROSORT(T, SFX)                                                   \
    static inline void su_insertion_##SFX(T* a, size_t n) {                           \
        for (size_t i = 1; i < n; i++) {                                              \
            T v = a[i];                                                               \
            size_t j = i;                                                             \
            while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; }                   \
            a[j] = v;                                                                 \
        }                                                                             \
    }                                                                                 \
    static inline void su_sift_##SFX(T* a, size_t root, size_t n) {                   \
        T v = a[root];                                                                \
        for (;;) {                                                                    \
            size_t child = 2 * root + 1;                                              \
            if (child >= n) break;                                                    \
            if (child + 1 < n && a[child + 1] > a[child]) child++;                    \
            if (!(a[child] > v)) break;                                               \
            a[root] = a[child];                                                       \
            root = child;                                                             \
        }                                                                             \
        a[root] = v;                                                                  \
    }                                                                                 \
    static inline void su_heapsort_##SFX(T* a, size_t n) {                            \
        for (size_t i = n / 2; i-- > 0;) su_sift_##SFX(a, i, n);                      \
        for (size_t end = n; end-- > 1;) {                                            \
            T t = a[0]; a[0] = a[end]; a[end] = t;                                    \
            su_sift_##SFX(a, 0, end);                                                 \
        }                                                                             \
    }                                                                                 \
    static void su_introsort_loop_##SFX(T* a, size_t n, unsigned depth) {            \
        while (n > SU_INSERTION_MAX) {                                                \
            if (depth == 0) { su_heapsort_##SFX(a, n); return; }                      \
            depth--;                                                                  \
            T x = a[0], y = a[n / 2], z = a[n - 1], p;                                \
            if (x < y) p = (y < z) ? y : ((x < z) ? z : x);                           \
            else p = (x < z) ? x : ((y < z) ? z : y);                                 \
            /* Hoare partition: equal keys split evenly between the sides */          \
            size_t i = 0, j = n - 1;                                                  \
            for (;;) {                                                                \
                while (a[i] < p) i++;                                                 \
                while (p < a[j]) j--;                                                 \
                if (i >= j) break;                                                    \
                T t = a[i]; a[i] = a[j]; a[j] = t;                                    \
                i++; j--;                                                             \
            }                                                                         \
            size_t left = j + 1;                                                      \
            /* recurse into the smaller side, loop on the larger */                   \
            if (left < n - left) {                                                    \
                su_introsort_loop_##SFX(a, left, depth);                              \
                a += left; n -= left;                                                 \
            } else {                                                                  \
                su_introsort_loop_##SFX(a + left, n - left, depth);                   \
                n = left;                                                             \
            }                                                                         \
        }                                                                             \
        su_insertion_##SFX(a, n);                                                     \
    }                                                                                 \
    static inline void su_introsort_##SFX(T* a, size_t n) {                           \
        if (n > 1) su_introsort_loop_##SFX(a, n, 2 * su_log2(n));                     \
    }

SU_DEFINE_INTROSORT(uint32_t, u32)
SU_DEFINE_INTROSORT(uint64_t, u64)
