---
title: Benchmarks
section: Project
order: 30
---

Every benchmark checks its results against a reference before it times
anything, and raw results are kept as CSV files in `benchmarks/outputs/`.
The reference machine is an AMD Ryzen 5 2600X under Windows 10, with GCC 13.2
and MSVC 19.41.

## Where the numbers are

| Component | Harness | Results | Summary |
|-----------|---------|---------|---------|
| Binary search | `benchmarks/binary_search/bench_bsearch.cpp` | `bsearch_ryzen2600x_*.csv` | `docs/papers/binary-search/` |
| Bucketization | `benchmarks/*_stack/` (Dolphin, GNOME, Qt, terminal, Godot) | `bucketization_*.csv` | `docs/bucketization/benchmarks.md` |
| Sorting | `benchmarks/sort/bench_sort.cpp` | `sort_ryzen2600x_*.csv`, `namesort_*.csv` | `docs/sorting/results.md` |

The sorting results also come as an interactive page with charts and tables:
`benchmarks/outputs/sort_benchmarks.html`.

## Headline results

- **Binary search:** 16 interleaved searches are about 4× faster per key than
  single searches once the array outgrows the caches.
- **Bucketization:** in the sort code of real file managers, switching the
  sort column drops from hundreds of milliseconds to a few milliseconds on
  folders of about 100,000 entries.
- **Sorting:** radix sort is up to about 4× faster than `std::sort` on large
  32-bit arrays. Name sort is about 3× faster on large name lists.
- **0.3.1:** bitonic merge sort is about 3.4× faster than `std::sort` on
  32-bit keys. Bitonic sort on a GPU (GeForce GTX 1660 SUPER, copies included)
  is about 14× faster on 16 Mi 32-bit keys when it runs back to back, and
  about 5× when the GPU has been idle and has to clock up first.

## Running them

```bash
python benchmarks/scripts/run_bsearch_benchmarks.py run --build build --label gcc
python benchmarks/scripts/run_bucketization_benchmarks.py --build build
python benchmarks/scripts/run_sort_benchmarks.py datasets data
python benchmarks/scripts/run_sort_benchmarks.py run --build build --label gcc --data data
```

## Method

- Pin the thread to one core and raise its priority.
- Interleave the timed runs of the algorithms round-robin, so slow periods of
  the machine hit them all alike.
- Report the median and the interquartile range.

Compare algorithms within one run. Separate runs on the reference machine
differ by up to about 15 %.
