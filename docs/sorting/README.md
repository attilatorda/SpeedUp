# Sorting

Sorts that standard libraries do not ship, behind the C ABI
(`include/speedup/algorithms/sort.h`) with a header-only C++17 layer
(`include/speedup/sort.hpp`). The first step of the sorting epic has four,
and 0.3.1 adds two bitonic hybrids and a GPU path:

| Sort | Types | What it is for |
|------|-------|----------------|
| **Radix sort** (LSD) | u32, i32, f32, u64, i64, f64 | large numeric arrays; linear time, no comparisons |
| **Bucket sort** | u64, i64, f64 | numeric arrays whose distribution is smooth; buckets from a sample |
| **Bitonic sort** | u32, i32, f32, u64, i64, f64 | a sorting network: no data-dependent branches; the GPU sort, and a SIMD building block on the CPU |
| **Name sort** | byte strings (`speedup_str_t`) | person names: a bucket sort whose buckets come from name-frequency tables ([name-sort.md](name-sort.md)) |
| **Bitonic merge sort** (0.3.1) | u32, i32, f32, u64, i64, f64 | large numeric arrays: bitonic blocks, then merges with an AVX2 merge network |
| **Bitonic bucket sort** (0.3.1) | u32, i32, f32, u64, i64, f64 | sampled buckets finished by the bitonic network; kept for study, rarely the fastest |
| **Bitonic sort on the GPU** (0.3.1) | the bitonic sort types | very large arrays on a machine with an OpenCL GPU |

Measured results: [results.md](results.md) (and `benchmarks/outputs/sort_benchmarks.html`).

## What the standard libraries ship

Every mainstream standard library sorts by comparison only:

| Language | Library sort | Radix | Bucket | Bitonic |
|----------|-------------|:-----:|:------:|:-------:|
| C | `qsort` (implementation-defined, usually quicksort/introsort or merge sort) | – | – | – |
| C++ | `std::sort` (introsort), `std::stable_sort` (merge sort) | – | – | – |
| Java | `Arrays.sort`: dual-pivot quicksort for primitives (counting sort for `byte`/`short`/`char`), TimSort for objects | –¹ | – | – |
| Python | `list.sort` (TimSort; Powersort merge policy since 3.11) | – | – | – |
| Rust | `sort` (driftsort), `sort_unstable` (ipnsort) | – | – | – |
| Go | `slices.Sort` (pdqsort) | – | – | – |
| C# | `Array.Sort` (introsort) | – | – | – |
| JavaScript | `Array.prototype.sort` (TimSort in V8) | – | – | – |

¹ A radix-sort path for `int[]`, `long[]`, `float[]` and `double[]` in
`DualPivotQuicksort` has been proposed since 2021
([JDK-8266431](https://bugs.openjdk.org/browse/JDK-8266431)); it was still
under review at the last check, not in a released JDK as far as could be
verified.

Radix and bitonic sorts exist in add-on libraries (Boost.Sort's spreadsort,
CUB/Thrust radix sort on GPUs, the `radsort` crate), not in the languages'
own libraries.

## Order

- Integers: numeric order.
- Floating point: IEEE 754 totalOrder: `-NaN < -inf < … < -0.0 < +0.0 < … < +inf < +NaN`.
  The bits are moved, never changed, so NaN payloads survive. (Bucketization's
  `keys::from_double` maps every NaN to one key instead, which only works for
  keys that are not decoded again; `radix_sort_by_key` uses that encoding.)
- Names: byte order (`memcmp`, shorter first on a tie), or with
  `SPEEDUP_NAME_SORT_ASCII_CASELESS` A–Z compared as a–z, ties broken by bytes.
  UTF-8 in byte order is code point order.

## API

```c
#include "speedup/api.h"

double v[N];
speedup_radix_sort_f64(v, N, NULL);          /* NULL: the library allocates scratch */
speedup_bucket_sort_f64(v, N, scratch);      /* or pass N elements of scratch */
speedup_bitonic_sort_f64(v, N);              /* in place, no scratch */

speedup_str_t names[M];                      /* {data, len}, need not be NUL-terminated */
speedup_name_sort(names, M, SPEEDUP_NAME_SORT_BYTES);
```

```cpp
#include "speedup/sort.hpp"
namespace ss = speedup::sort;

ss::radix_sort(values);                                   // std::vector<int|long|size_t|float|double|...>
ss::radix_sort_by_key(files, [](const File& f) { return f.size; });  // stable, any movable type
ss::name_sort(names);                                     // std::vector<std::string> or <std::string_view>
```

Every function sorts. If an allocation fails, it falls back to an in-place
comparison sort.

## Implementation notes

- **Radix sort** (`src/algorithms/sort/radix_sort.c`): 8-bit digits. One
  read pass encodes the keys and builds every digit's histogram. Digits on
  which all keys agree are skipped, so sorted, few-unique or small-valued
  data takes fewer passes. Signed and floating-point keys are encoded to
  unsigned in place and decoded during the last copy.
- **Bucket sort** (`bucket_sort.c`): 64 equal-depth segments from a sorted
  sample of 1,024 keys, each cut into equal-width buckets of about 32
  expected keys, then an introsort per bucket. The key-to-bucket map is
  monotone, so a bad estimate only makes a bucket larger. It never makes
  the result wrong.
- **Bitonic sort** (`bitonic_net.h`, `bitonic_sort.c`, `bitonic_sort_avx2.c`):
  this formulation of the network only has comparators that put the smaller
  value at the lower index. That makes it exact for any n: a comparator that
  reaches past the end would compare against +infinity padding, and is
  skipped. The AVX2 kernel (chosen at run time) handles comparator
  distances of one vector or more with vector min/max. It reverses the
  mirrored loads of the flip stages in a register, and runs every distance
  below one vector in registers between one load and one store.
  `speedup_sort_set_simd(0)` turns it off.
- **Bitonic merge sort** (`bitonic_merge_sort.c`, 0.3.1): blocks of 128 keys
  are sorted by the bitonic network.
  - Runs are then merged pairwise, first inside chunks of 32 Ki keys that stay
    in L2, then across the array.
  - The AVX2 merge (after Chhugani et al., PVLDB 2008) keeps the largest vector
    of keys seen so far in a register and takes the next vector from the run
    with the smaller head. It merges the two with a bitonic merge (reverse,
    min/max, in-register stages) and writes out the lower half.
  - Lengths must be multiples of the vector width, so the last `n mod 8` keys
    are merged in at the end.
  - Tuning (`run_sort_benchmarks.py tune031`): block sizes from 16 to 256 are
    within a few percent of each other; 8 Ki chunks are worse.
- **Bitonic bucket sort** (`bitonic_bucket_sort.c`, 0.3.1): the bucket map of
  bucket sort (`sort_bucket_map.h`) with buckets of about 256 keys, each
  finished by the bitonic network.
  - Below 16 Ki keys it is bitonic merge sort.
  - Tuning found no bucket size from 32 to 1,024 at which it beats bitonic merge
    sort. The scatter into thousands of buckets costs more than the networks
    save. It is kept for study.
- **GPU bitonic sort** (`src/backends/gpu/opencl/`, 0.3.1): the OpenCL ICD
  loader is opened at run time, so building needs no OpenCL SDK and machines
  without a GPU stay on the CPU.
  - The keys are padded to a power of two with the maximum key. Work-groups of
    512 threads sort blocks of 1,024 keys in local memory, then global flip and
    half-cleaner stages and local merges finish the network.
  - The keys are copied to the GPU and back on every call, and the timings
    include those copies.
- **Dispatch** (`sort_dispatch.c`): bitonic sort runs on the GPU under
  `SPEEDUP_BACKEND_FORCE_OPENCL`, and under `SPEEDUP_BACKEND_AUTO` from 65,536
  keys on. Every other sort, and every other preference, stays on the CPU. A
  GPU failure falls back to the CPU and disables OpenCL for the process.

## Next steps

- GPU: keep data on the device across calls, a GPU radix sort, CUDA.
- Name sort: built-in priors for other alphabets (Hungarian, German, …), and
  locale order through collation keys ([name-sort.md](name-sort.md#unicode)).
- 11-bit digits for 32-bit radix sort (3 passes instead of 4), key/value
  radix sort, and a parallel radix sort.
