# SpeedUp Architecture

Dispatch order: CUDA -> OpenCL -> CPU optimized -> C fallback.

Tuning surfaces:
- compile-time defaults via CMake
- runtime hints via C API (threads/cache/backend preference)

## Phase 2 implemented status

- `speedup_binary_search_i64` now uses runtime backend preference policy:
  - `AUTO`
  - `FORCE_CPU`
  - `FORCE_CUDA`
  - `FORCE_OPENCL`
- CUDA/OpenCL runtime checks are wired into dispatch (currently stub availability).
- CPU fallback is active through `speedup_binary_search_i64_ref`.
- Windows benchmark runner script added:
  - `benchmarks/scripts/run_windows_benchmark.ps1`
