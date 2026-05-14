# SpeedUp Benchmarks

## Baseline
- Baseline CSV: `benchmarks/outputs/results_baseline_windows.csv`
- Analysis: `docs/papers/binary_search_results_analysis.md`

## How to run (Windows ASM lane)
```powershell
./benchmarks/scripts/run_windows_benchmark.ps1
```

## Method notes
- Measure ns/search across multiple dataset sizes.
- Keep build type `Release`.
- Record CPU model, core count, and frequency behavior.
- Track backend mode used (CPU/ASM/CUDA/OpenCL).

## Add a new benchmark checklist
1. Add benchmark source under `benchmarks/core/`.
2. Add target wiring in `CMakeLists.txt` behind platform guards.
3. Save outputs under `benchmarks/outputs/`.
4. Add interpretation notes to `docs/papers/`.
