---
title: C API reference
nav: C API
section: Reference
order: 20
---

Include `speedup/api.h` for everything below, and call `speedup_init()` once
before the first call. The ABI is stable: functions take fixed-width types,
and nothing calls back into user code.

## Core

Headers: `speedup/api.h`, `speedup/backend/dispatch.h`.

```api
void speedup_init(void);
Initializes the library. Call it once before any other function.
```

```api
void speedup_set_backend_preference(speedup_backend_pref_t pref);
Chooses the backend: SPEEDUP_BACKEND_AUTO (default), SPEEDUP_BACKEND_FORCE_CPU, SPEEDUP_BACKEND_FORCE_CUDA or SPEEDUP_BACKEND_FORCE_OPENCL. Since 0.3.1 bitonic sort has an OpenCL path; everything else, and CUDA, runs on the CPU.
```

```api
void speedup_set_cache_hint(speedup_cache_hint_t hint);
Sets the cache sizes the algorithms plan for (l1_bytes, l2_bytes, l3_bytes, cacheline_bytes). The binary search switches to its prefetching kernel beyond the L2 hint. Defaults: 32 KiB, 256 KiB, 8 MiB, 64 B.
```

```api
void speedup_set_threads_hint(uint32_t n);
Thread count hint; 0 means auto-detect. Reserved for parallel algorithms.
```

```api
int speedup_opencl_available(void);
const char* speedup_opencl_device_name(void);
Whether an OpenCL GPU was found, and its name (0.3.1). The OpenCL loader is opened at run time on the first call; nothing is linked at build time.
```

```api
speedup_backend_pref_t speedup_get_backend_preference(void);
speedup_cache_hint_t speedup_get_cache_hint(void);
uint32_t speedup_get_threads_hint(void);
Read the current hints.
```

## Binary search

Header: `speedup/algorithms/binary_search.h`. All functions take an array sorted in ascending order.

```api
int64_t speedup_binary_search_i64(const int64_t* array, int64_t key, int64_t size);
Index of an element equal to key, or -1. When key occurs more than once, the first occurrence.
```

```api
int64_t speedup_lower_bound_i64(const int64_t* array, int64_t key, int64_t size);
First index i with array[i] >= key, or size when every element is smaller.
```

```api
void speedup_binary_search_batch_i64(const int64_t* array, int64_t size, const int64_t* keys, int64_t count, int64_t* results);
void speedup_lower_bound_batch_i64(const int64_t* array, int64_t size, const int64_t* keys, int64_t count, int64_t* results);
results[i] is what the single-key function returns for keys[i]. On large arrays several times faster than a loop of single searches, because the searches overlap their cache misses.
```

```api
int64_t speedup_binary_search_i64_ref(const int64_t* array, int64_t key, int64_t size);
Textbook branching search with early exit; the baseline the others are tested against. Returns any matching index, or -1.
```

## Sorting

Header: `speedup/algorithms/sort.h`. Every sort is ascending and in place. Floating point sorts in IEEE 754
totalOrder. `scratch` may be `NULL` (the library allocates) or a buffer of
`n` elements of the same type. If an allocation fails, the function falls back
to an in-place comparison sort.

```api
void speedup_radix_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_radix_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_radix_sort_f32(float* data, size_t n, void* scratch);
void speedup_radix_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_radix_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_radix_sort_f64(double* data, size_t n, void* scratch);
LSD radix sort with 8-bit digits; digits on which all keys agree are skipped. Below 256 elements, introsort.
```

```api
void speedup_bucket_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bucket_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bucket_sort_f64(double* data, size_t n, void* scratch);
Bucket sort: equal-depth splitters from a sample, linear interpolation between them, then a comparison sort per bucket.
```

```api
void speedup_bitonic_sort_u32(uint32_t* data, size_t n);
void speedup_bitonic_sort_i32(int32_t* data, size_t n);
void speedup_bitonic_sort_f32(float* data, size_t n);
void speedup_bitonic_sort_u64(uint64_t* data, size_t n);
void speedup_bitonic_sort_i64(int64_t* data, size_t n);
void speedup_bitonic_sort_f64(double* data, size_t n);
Bitonic sorting network for any n, O(n log² n) compare-exchanges without data-dependent branches. Runs on an OpenCL GPU under SPEEDUP_BACKEND_FORCE_OPENCL, and under SPEEDUP_BACKEND_AUTO for large arrays (0.3.1); otherwise on the CPU, with the AVX2 kernel when the CPU has AVX2. Any GPU failure falls back to the CPU.
```

```api
void speedup_bitonic_merge_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_f32(float* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bitonic_merge_sort_f64(double* data, size_t n, void* scratch);
Bitonic merge sort (0.3.1): blocks sorted by the bitonic network, then merged, first inside the L2 cache, then across the array. With AVX2 every merge step merges a vector of keys in registers.
```

```api
void speedup_bitonic_bucket_sort_u32(uint32_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_i32(int32_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_f32(float* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_u64(uint64_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_i64(int64_t* data, size_t n, void* scratch);
void speedup_bitonic_bucket_sort_f64(double* data, size_t n, void* scratch);
Bitonic bucket sort (0.3.1): sampled buckets, each sorted by the bitonic network.
```

```api
void speedup_sort_set_simd(int enabled);
0 turns the AVX2 kernels off, 1 (the default) uses them when the CPU has AVX2. For tests and benchmarks.
```

## Name sort

Header: `speedup/algorithms/sort.h`.

```api
typedef struct speedup_str_t { const char* data; size_t len; } speedup_str_t;
A name: bytes and length. data need not be NUL-terminated and may be NULL when len is 0.
```

```api
void speedup_name_sort(speedup_str_t* names, size_t n, uint32_t flags);
Sorts names in place. flags: SPEEDUP_NAME_SORT_BYTES (byte order, the default) or SPEEDUP_NAME_SORT_ASCII_CASELESS (A–Z compare as a–z; names equal up to case fall back to byte order).
```

```api
void speedup_name_sort_ex(speedup_str_t* names, size_t n, uint32_t flags, const speedup_name_sort_options_t* options, speedup_name_sort_stats_t* stats);
As speedup_name_sort, with options (target_bucket: names per bucket, default 32; sample_size: default n/128 within 256..16384) and statistics of the plan (buckets, trie_blocks, max_bucket, mean_bucket, stddev_bucket, and oversized: buckets finished by MSD radix sort). Both pointers may be NULL.
```

```api
int speedup_name_compare(speedup_str_t a, speedup_str_t b, uint32_t flags);
The order name sort produces: negative, zero or positive, like memcmp.
```
