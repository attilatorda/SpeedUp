---
title: Binary search
section: Algorithms
order: 10
---

Header: `include/speedup/algorithms/binary_search.h`. Kernels:
`src/algorithms/binary_search/bsearch_kernels.h`.

## What the library does

- **Single keys** use a branchless search while the array fits the L2 cache
  hint, and a branchless search that prefetches both candidates of the next
  step beyond it. Branchy code loses to mispredictions in cache, and plain
  branchless code loses to memory latency outside it.
- **Many keys** (`*_batch_*`) search 16 keys in lockstep, so their cache
  misses overlap. On arrays larger than the caches this is about 4× faster
  per key than calling the single-key function in a loop.

```c
int64_t pos = speedup_lower_bound_i64(sorted, key, n);      /* first index with sorted[i] >= key */
speedup_lower_bound_batch_i64(sorted, n, keys, count, out); /* many keys at once */
```

All functions take an array sorted in ascending order. For keys that occur
more than once, `speedup_binary_search_i64` returns the first occurrence.

## How it was chosen

The study measured 22 variants from 8 KiB to 512 MiB in throughput and
latency mode, with two compilers. It also found the error in the original
benchmark: its keys came from `rand()`, which never exceeds 32,767, so the
large-array results never left the first 256 KiB. The hand-written assembly
and AVX2 searches of the prototype are kept in `legacy/` for the comparison
only. See [Papers](#papers) and `docs/papers/binary-search/`.

## Functions

See the [C API reference](#c-api).
