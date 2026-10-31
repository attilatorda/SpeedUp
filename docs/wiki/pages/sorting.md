---
title: Sorting
section: Algorithms
order: 12
---

Sorts that the standard libraries of C, C++, Java, Python, Rust, Go, C# and
JavaScript do not ship. All of them sort in ascending order and in place.
Header: `include/speedup/algorithms/sort.h`. C++ layer: `include/speedup/sort.hpp`.

| Sort | Types | Use it for |
|------|-------|------------|
| Radix sort (LSD) | u32, i32, f32, u64, i64, f64 | Large numeric arrays: linear time, no comparisons |
| Bucket sort | u64, i64, f64 | Numeric data with a smooth distribution |
| Bitonic sort | u32, i32, f32, u64, i64, f64 | Small arrays and fixed-pattern sorting: no data-dependent branches |
| [Name sort](#name-sort) | byte strings | Person names and other strings |
| Bitonic merge sort (0.3.1) | u32, i32, f32, u64, i64, f64 | Large numeric arrays: bitonic blocks, AVX2 merges |
| Bitonic bucket sort (0.3.1) | u32, i32, f32, u64, i64, f64 | Sampled buckets finished by the network; kept for study |
| Bitonic sort, GPU (0.3.1) | as bitonic sort | Very large arrays, on a machine with an OpenCL GPU |

## Order

- **Integers:** numeric order.
- **Floating point:** IEEE 754 totalOrder, `-NaN < -inf < … < -0.0 < +0.0 < … < +inf < +NaN`.
  Values are moved, never changed, so NaN payloads survive.
- **Names:** byte order (UTF-8 code point order), or ASCII-caseless with
  `SPEEDUP_NAME_SORT_ASCII_CASELESS`.

## Examples

```c
double v[N];
speedup_radix_sort_f64(v, N, NULL);         /* NULL: the library allocates scratch */
speedup_bucket_sort_f64(v, N, scratch);     /* or pass N elements of scratch */
speedup_bitonic_sort_f64(v, N);             /* in place, no scratch */
```

```cpp
#include "speedup/sort.hpp"
namespace ss = speedup::sort;

ss::radix_sort(values);                      // std::vector<int|long|size_t|float|double|...>
ss::radix_sort_by_key(files, [](const File& f) { return f.size; });  // stable, any movable type
ss::name_sort(names);                        // std::vector<std::string> or std::string_view
```

Every function sorts. If an allocation fails, it falls back to an in-place
comparison sort.

## How they work

- **Radix sort:** 8-bit digits. One read pass builds every digit's histogram,
  and a digit on which all keys agree is skipped. Signed and floating-point
  keys are encoded to unsigned in place and decoded on the last copy.
- **Bucket sort:** 64 equal-depth segments from a sample of 1,024 keys, each
  cut into equal-width buckets of about 32 keys, then an introsort per
  bucket. A bad estimate only makes a bucket larger. It never makes the
  result wrong.
- **Bitonic sort:** every comparator puts the smaller key first. That makes
  the network exact for any `n` without padding, because comparators that
  reach past the end are skipped. An AVX2 kernel is chosen at run time.

## The GPU path (0.3.1)

Bitonic sort can run on an OpenCL GPU. SpeedUp opens the OpenCL loader at run
time, so building needs no OpenCL SDK, and machines without a GPU stay on the
CPU.

```c
speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_OPENCL);  /* always on the GPU, if there is one */
speedup_bitonic_sort_u32(keys, n);
speedup_set_backend_preference(SPEEDUP_BACKEND_AUTO);          /* default: the GPU from 65,536 keys */
```

The keys are copied to the GPU and back on every call. Any GPU error falls
back to the CPU.

## When to use which

- Radix sort beats `std::sort` on large integer and floating-point arrays. It
  is slower on input that is already sorted, which `std::sort` handles very
  cheaply.
- Bitonic sort with AVX2 beats `std::sort` on 32-bit keys while they fit in
  the caches. Beyond that, its `O(n log² n)` work loses.
- Bucket sort is about level with `std::sort` on this machine. It is kept for
  data whose distribution a sample describes well.

Measured numbers: [Benchmarks](#benchmarks).
