#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Sorts that the standard libraries of C, C++, Java, Python, Rust, Go, C#
 * and JavaScript do not ship (docs/sorting/README.md). Every function sorts
 * in ascending order, in place.
 *
 * Floating-point order is IEEE 754 totalOrder: -NaN < -inf < ... < -0.0 <
 * +0.0 < ... < +inf < +NaN. Values are moved, never changed, so NaN payloads
 * survive.
 *
 * `scratch` may be NULL; the function then allocates what it needs. Passing
 * a buffer of n elements of the same type avoids the allocation. If an
 * allocation fails, the function falls back to an in-place comparison sort,
 * so it always sorts. */

/* LSD radix sort, 8-bit digits; digits that are equal in every element are
 * skipped. Stable for the keys (equal keys are indistinguishable anyway). */
void speedup_radix_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_radix_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_radix_sort_f32(float* data, size_t n, void* scratch);
void speedup_radix_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_radix_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_radix_sort_f64(double* data, size_t n, void* scratch);

/* Bucket sort: equal-depth splitters from a sample, linear interpolation
 * between them, then a small comparison sort per bucket. */
void speedup_bucket_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bucket_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bucket_sort_f64(double* data, size_t n, void* scratch);

/* Bitonic sort: a sorting network of O(n log^2 n) compare-exchanges for any
 * n, with no data-dependent branches. Needs no scratch. On the CPU it pays
 * off for small arrays and as a building block. It is the sort that suits
 * GPUs: with an OpenCL GPU (found at run time, no SDK needed) it runs there
 * under SPEEDUP_BACKEND_FORCE_OPENCL, and under SPEEDUP_BACKEND_AUTO from
 * 65,536 keys on; FORCE_CPU keeps it on the CPU. Any GPU failure falls back
 * to the CPU. */
void speedup_bitonic_sort_u32(uint32_t* data, size_t n);
void speedup_bitonic_sort_i32(int32_t* data, size_t n);
void speedup_bitonic_sort_f32(float* data, size_t n);
void speedup_bitonic_sort_u64(uint64_t* data, size_t n);
void speedup_bitonic_sort_i64(int64_t* data, size_t n);
void speedup_bitonic_sort_f64(double* data, size_t n);

/* Bitonic merge sort: blocks of 128 keys sorted by the bitonic network, then
 * merged, first inside the L2 cache, then across the array; with AVX2 each
 * merge step merges a vector of keys in registers (Chhugani et al. 2008). */
void speedup_bitonic_merge_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_f32(float* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_f64(double* data, size_t n, void* scratch);

/* Bitonic bucket sort: the sampled bucket sort above with buckets of about
 * 256 keys, each sorted by the bitonic network instead of introsort; below
 * 16,384 keys, bitonic merge sort. */
void speedup_bitonic_bucket_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_f32(float* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_f64(double* data, size_t n, void* scratch);

/* For tests and benchmarks: 0 turns the AVX2 kernels off, 1 (the default)
 * uses them when the CPU has AVX2. */
void speedup_sort_set_simd(int enabled);

/* ---- Name sort ---------------------------------------------------------
 * A bucket sort for person names (docs/sorting/name-sort.md). Bucket
 * boundaries come from how often each byte occurs at positions 1, 2 and 3
 * of names: built-in priors from US Census data, refined on a sample of the
 * input. From 131072 names on; smaller inputs go straight to the MSD radix
 * sort that also finishes the buckets. Works on any byte strings; UTF-8
 * sorts in code point order. */

typedef struct speedup_str_t {
    const char* data; /* need not be NUL-terminated; may be NULL when len is 0 */
    size_t len;
} speedup_str_t;

/* Default: byte order, like memcmp with the shorter string first on a tie. */
#define SPEEDUP_NAME_SORT_BYTES 0u
/* ASCII A-Z compare as a-z; names equal up to case fall back to byte order. */
#define SPEEDUP_NAME_SORT_ASCII_CASELESS 1u

typedef struct speedup_name_sort_options_t {
    size_t target_bucket; /* names per bucket the plan aims for; 0 = default (32) */
    size_t sample_size;   /* names sampled to refine the priors; 0 = default (n/128, 256..16384) */
} speedup_name_sort_options_t;

typedef struct speedup_name_sort_stats_t {
    size_t buckets;          /* buckets the plan cut; 0 below 131072 names (no plan: MSD radix sort) */
    size_t trie_blocks;      /* 257-entry lookup blocks (1 + nodes split on position 2 or 3) */
    size_t max_bucket;       /* largest bucket */
    double mean_bucket;      /* n / buckets */
    double stddev_bucket;    /* standard deviation of the bucket sizes */
    size_t oversized;        /* buckets of more than 32 names, finished by MSD radix sort */
} speedup_name_sort_stats_t;

void speedup_name_sort(speedup_str_t* names, size_t n, uint32_t flags);

/* options and stats may be NULL. */
void speedup_name_sort_ex(speedup_str_t* names, size_t n, uint32_t flags,
                          const speedup_name_sort_options_t* options, speedup_name_sort_stats_t* stats);

/* The order name sort produces: <0, 0 or >0, like memcmp. */
int speedup_name_compare(speedup_str_t a, speedup_str_t b, uint32_t flags);

#ifdef __cplusplus
}
#endif
