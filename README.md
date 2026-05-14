# SpeedUp

Cross-platform algorithm optimization library (C/C++ core, ASM and GPU optional) with fallback design.

## Goals
- High-performance algorithms and data structures
- Cross-platform support: Windows + Unix, x86_64 + ARM64 (64-bit)
- Heterogeneous backend model: GPU when available, CPU fallback always
- Multi-language consumption through stable C ABI

## Build

### Core library + smoke test
```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

### Build with shared library (for bindings)
```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared --config Release
```

### Windows benchmark (ASM path)
```powershell
./benchmarks/scripts/run_windows_benchmark.ps1
```

If NASM is not installed, ASM paths are disabled automatically and C/C++ fallback remains available.

## Minimal C usage
```c
#include <stdint.h>
#include "speedup/api.h"

int main(void) {
    speedup_init();
    int64_t a[5] = {2,4,6,8,10};
    int64_t idx = speedup_binary_search_i64(a, 6, 5);
    return (idx == 2) ? 0 : 1;
}
```

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

## Documentation index
- Architecture: `docs/architecture/overview.md`
- Benchmark analysis: `docs/papers/binary_search_results_analysis.md`
- Benchmark methodology baseline: `BENCHMARKS.md`
- API docs (Doxygen): `docs/api/README.md`

## Contributing
See `CONTRIBUTING.md` and issue/PR templates under `.github/`.
