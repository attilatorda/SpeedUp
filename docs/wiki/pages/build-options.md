---
title: Build options
section: Reference
order: 22
---

## CMake options

| Option | Default | Effect |
|--------|---------|--------|
| `SPEEDUP_ENABLE_ASM` | `ON` | NASM-assembled paths; switched off automatically when NASM is missing |
| `SPEEDUP_ENABLE_OPENCL` | `ON` | OpenCL backend (runtime check only today) |
| `SPEEDUP_ENABLE_CUDA` | `OFF` | CUDA backend (runtime check only today) |
| `SPEEDUP_BUILD_BENCHMARKS` | `ON` | Benchmark executables |
| `SPEEDUP_BUILD_EXAMPLES` | `ON` | Example programs (`examples/`) |
| `BUILD_SHARED_LIBS` | `OFF` | Shared library, for language bindings |

## Tuning defaults

Cached CMake variables that seed the run-time hints in the generated
`speedup/config.h`:

| Variable | Default | Meaning |
|----------|---------|---------|
| `SPEEDUP_DEFAULT_THREADS` | `0` | Thread hint; 0 detects automatically |
| `SPEEDUP_DEFAULT_L1_BYTES` | `32768` | L1 data cache hint |
| `SPEEDUP_DEFAULT_L2_BYTES` | `262144` | L2 cache hint |
| `SPEEDUP_DEFAULT_L3_BYTES` | `8388608` | L3 cache hint |

## Targets

| Target | Kind | Contents |
|--------|------|----------|
| `speedup` | library | C ABI: core, binary search, sorts |
| `speedup::bucketization` | interface | Header-only Bucketization (C++17) |
| `speedup_*_test` | executables | Unit tests, registered with CTest |
| `speedup_bench_*` | executables | Benchmark harnesses |

## SIMD

The AVX2 sort kernels are compiled in their own file with `-mavx2` or
`/arch:AVX2` on x86-64, and chosen at run time only when the CPU and the
operating system support AVX2. Other targets use the portable C kernels.
