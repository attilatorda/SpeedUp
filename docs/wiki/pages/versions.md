---
title: Versions
section: Project
order: 34
---

What changed in each version, in short. The full list is in the
[Changelog](#changelog).

```version
0.3.1 | not yet released | Bitonic hybrids and the GPU
- Bitonic sort runs on an OpenCL GPU, found at run time with no SDK needed: the first GPU path in SpeedUp.
- Bitonic merge sort: bitonic blocks merged with an AVX2 merge network.
- Bitonic bucket sort: sampled buckets finished by the bitonic network.
- A C++ fix: the dispatch header lacked extern "C".
```

```version
Unreleased | not yet released | Sorting
- Radix sort, bucket sort and bitonic sort (with an AVX2 kernel) for 32- and 64-bit integers and floating-point numbers. No standard library ships these.
- Name sort: a bucket sort for person names, planned from US Census name-frequency tables.
- A C++17 sorting layer, including a stable radix sort of any objects by a numeric key.
- A third paper, on name sort and the new sorts, and an interactive results page.
- This wiki.
```

```version
0.3.0 | not yet released | Binary search, measured properly
- The original benchmark drew its keys with rand(), which never exceeds 32,767, so its large-array results did not hold. A new harness measures 22 variants from 8 KiB to 512 MiB.
- The single-key search is now branchless, with prefetching beyond the L2 cache: about 2× faster in the caches. With duplicate keys it returns the first occurrence.
- New: lower bound, and batch searches of 16 keys in lockstep, about 4× faster per key on large arrays.
- AVX2 detection works; the Bucketization benchmarks run on Windows.
- Papers on binary search and on Bucketization.
```

```version
0.2.0 | 27 October 2026 | Bucketization
- Bucketization in C++17 (header-only) and Java 17: switching a list's sort order in O(n), sorting only the rows on screen.
- Measured in the real sort code of KDE Dolphin, the GNOME file managers, Qt and terminal file managers, and the Godot editor.
- Found three comparator bugs upstream (Dolphin, Krusader, lf) and reported them.
- A license exception lets those projects use Bucketization under their own licenses.
```

```version
0.1.0 | 14 May 2026 | First release
- An optimized binary search behind a stable C ABI.
- Backends chosen at run time: portable C, Windows x64 assembly, and CUDA and OpenCL stubs with CPU fallback.
- CPU feature detection, code generation for typed specializations, tests, benchmarks, CI, and setup notes for 14 languages.
```
