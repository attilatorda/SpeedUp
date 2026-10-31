---
title: SpeedUp
nav: Home
section: Overview
order: 1
---

SpeedUp is a cross-platform library of fast algorithms and data structures,
written in C and C++ with optional assembly and GPU paths. Every accelerated
path has a portable fallback, so the library always works, and its C ABI is
stable so other languages can bind to it.

## What is in it

| Component | What it does | Interface |
|-----------|--------------|-----------|
| [Binary search](#binary-search) | Branchless and batched lower-bound search, measured from L1 to DRAM | C |
| [Bucketization](#bucketization) | Switches a list between sort orders in `O(n)`, sorting only the rows on screen | C++17, Java 17 |
| [Sorting](#sorting) | Radix, bucket and bitonic sort, which no standard library ships | C, C++17 |
| [Name sort](#name-sort) | A bucket sort for person names, planned from name-frequency tables | C, C++17 |

## Start here

- [Getting started](#getting-started): build, test and link the library.
- [C API reference](#c-api) and [C++ API reference](#cpp-api).
- [Benchmarks](#benchmarks): what was measured, and where the numbers live.
- [Papers](#papers): the three studies behind the components.

## Platforms

- Windows and Unix, x86-64 and ARM64 (64-bit).
- Compilers: GCC, Clang and MSVC, C11 and C++17.
- Dispatch order: CUDA, then OpenCL, then optimized CPU code, then the C
  fallback. The GPU backends are stubs today, so every call ends on the CPU.

## License

AGPL-3.0. Bucketization carries an additional permission that lets the file
managers it was measured in use it under their own licenses
(`LICENSE-EXCEPTION-Bucketization.md`).
