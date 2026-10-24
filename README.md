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

## Bucketization: fast re-sorting by several keys

`Bucketization` is an interface for collections that can be sorted in several
ways (a file manager's name / size / modified / last-used columns). Every
element picks a bucket per sort key once, when it is added. Switching the sort
column then only regroups by bucket id, and only the bucket on screen gets
sorted.

- C++17, header-only: `include/speedup/bucketization.hpp` (CMake target `speedup::bucketization`)
- Java 17: `bindings/java/bucketization/` (Maven)
- Design and cross-language spec: `docs/bucketization/README.md`

```cpp
#include "speedup/bucketization.hpp"
namespace bz = speedup::bucketization;

bz::BucketIndex<File> files(bz::BucketPlan::defaults().with_page_size(40));
files.add_sort_key(bz::SortKey<File>::of("name", &File::name));
files.add_sort_key(bz::SortKey<File>::of("size", &File::size));
files.add_all(listing);
files.sort_by("size", bz::Direction::descending);   // O(n), no comparisons
auto rows = files.first_page();                      // sorts ~40 elements
```

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
- `include/speedup`: public C API, plus header-only C++ components (`bucketization.hpp`)
- `src`: core, algorithms, CPU/GPU backends
- `codegen`: typed specialization pipeline
- `bindings`: multi-language integration stubs, native Java modules (`bindings/java/bucketization`)
- `examples`: example programs (`examples/bucketization/file_browser.cpp`)
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
- Bucketization design and cross-language spec: `docs/bucketization/README.md`
- Bucketization benchmarks against real file manager stacks (Dolphin, GNOME family): `docs/bucketization/benchmarks.md`
- Bucketization in the sort code of 10 more file managers (GNOME family, Qt, terminal) and the Godot editor: `docs/bucketization/cross-project-benchmarks.md`
- How file managers sort today, and where to contribute: `docs/bucketization/file-manager-survey.md`
- Launch-day contribution plan and per-project kits: `docs/bucketization/launch/README.md`
- Plan for contributing to KDE Dolphin: `docs/bucketization/kde-contribution-plan.md`
- Benchmark analysis: `docs/papers/binary_search_results_analysis.md`
- Benchmark methodology baseline: `BENCHMARKS.md`
- API docs (Doxygen): `docs/api/README.md`

## Contributing
See `CONTRIBUTING.md` and issue/PR templates under `.github/`.
