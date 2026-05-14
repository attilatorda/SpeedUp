# Binary Search Optimization Results Analysis

This document summarizes the baseline benchmark from `benchmarks/outputs/results_baseline_windows.csv`.

## Baseline table (ns/search)

| Size | Ref C | Branching ASM | Branchless ASM | Optimized ASM | SIMD AVX2 | SIMD+Opt |
|---|---:|---:|---:|---:|---:|---:|
| 10K | 64.99 | 61.85 | **44.73** | 60.15 | 69.08 | 70.89 |
| 100K | 57.90 | 56.47 | 58.31 | **52.60** | 56.73 | 57.21 |
| 1M | 68.77 | 61.87 | 65.79 | **57.53** | 64.76 | 66.03 |
| 10M | 78.89 | **69.18** | 78.58 | 75.46 | 70.91 | 80.58 |

## Key observations

- Branchless ASM wins on small input.
- Software-pipelined optimized ASM is most stable mid-range.
- Branching ASM wins at largest scale in this dataset.
- SIMD variants are mixed and not universally best.

## Interpretation

Microarchitecture effects (branch prediction, cache locality, memory latency) dominate asymptotic complexity for this algorithm. The best implementation should be selected by size/hardware policy, not globally fixed.

## Next benchmarking steps

1. Repeat on Linux x86_64 and ARM64.
2. Add per-core/count and cache-hint experiments.
3. Add CUDA and OpenCL comparison lanes where available.
4. Track deltas in CI benchmark artifacts.
