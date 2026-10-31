---
title: Architecture
section: Overview
order: 3
---

## Layers

SpeedUp has two kinds of components.

- **Algorithms behind the C ABI.** Binary search and the sorts are plain C
  functions with fixed-width types. They dispatch to the best backend at run
  time and fall back to portable C. Any language with a C FFI can call them.
- **Header-only C++ and native ports.** Bucketization calls back into user
  code (key functions, comparators), so it is a C++17 header library with a
  native Java port instead of a C ABI.

## Dispatch

Dispatch order: CUDA, then OpenCL, then optimized CPU code, then the C
fallback. Callers can steer it:

```c
speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_CPU);
speedup_set_cache_hint(hint);       /* the binary search picks its kernel from the L2 size */
speedup_sort_set_simd(0);           /* bitonic sort without its AVX2 kernel */
```

The CUDA and OpenCL backends only check for a runtime today, so every
preference ends on the CPU. Bitonic sort is the sort meant to gain a GPU
path first. CPU features are detected once (`src/core/cpu_features.c`), and
the AVX2 kernels are built in their own translation unit with AVX2 enabled.

## Repository layout

| Path | Contents |
|------|----------|
| `include/speedup/` | Public headers: `api.h`, `algorithms/*.h`, `sort.hpp`, `bucketization.hpp` |
| `src/core/` | Initialization, dispatch hints, CPU feature detection |
| `src/algorithms/` | Binary search and the sorts |
| `src/backends/` | CPU assembly, CUDA and OpenCL stubs |
| `codegen/` | Typed specializations; the name-frequency table generator |
| `bindings/` | Language bindings; the native Java Bucketization |
| `benchmarks/` | Harnesses, datasets, scripts and raw results (`outputs/`) |
| `tests/unit/` | Differential tests against reference implementations |
| `docs/` | Design documents, papers, and this wiki |
