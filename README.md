# SpeedUp

Cross-platform algorithm optimization library (C/C++ core, ASM and GPU optional) with fallback design.

## Current structure
- `include/speedup`: public C API
- `src`: core, algorithms, CPU/GPU backends
- `codegen`: typed specialization pipeline
- `bindings`: multi-language integration stubs
- `docs/tutorials/language-setup`: language onboarding guides
- `ci/github/workflows`: CI skeleton
- `legacy`: preserved original binary-search benchmark assets

## Active binary-search optimization assets
- ASM implementation (Windows x64): `src/backends/cpu/x86_64/binary_search_win64.asm`
- Benchmark suite source: `benchmarks/core/benchmark_win.c`
- Baseline benchmark output: `benchmarks/outputs/results_baseline_windows.csv`
- Results analysis: `docs/papers/binary_search_results_analysis.md`

## Build notes (Windows)
- `speedup_benchmark_win64` target is defined in `CMakeLists.txt` and wires NASM ASM + benchmark C source.
