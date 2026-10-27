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

Bucketization is a design for collections that can be sorted in several
ways (a file manager's name / size / modified / last-used columns); its
interface is `BucketedList<T>`, implemented by `BucketIndex<T>`. Every
element picks a bucket per sort key once, when it is added. Switching the sort
column then only regroups by bucket id, and only the bucket on screen gets
sorted.

- C++17, header-only: `include/speedup/bucketization.hpp` (CMake target `speedup::bucketization`)
- Java 17: `bindings/java/bucketization/` (Maven)
- Design and cross-language spec: `docs/bucketization/README.md`
- License: AGPL-3.0, like the rest of SpeedUp, with an exception that lets the
  file managers it was benchmarked against (and their libraries) use it under
  their own licenses: [LICENSE-EXCEPTION-Bucketization.md](LICENSE-EXCEPTION-Bucketization.md)

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

## Binary search
- API: `include/speedup/algorithms/binary_search.h`. Single keys:
  `speedup_binary_search_i64`, `speedup_lower_bound_i64`. Many keys:
  `speedup_binary_search_batch_i64`, `speedup_lower_bound_batch_i64`, which
  search 16 keys in lockstep and are about 4× faster per key on large arrays.
- Kernels: `src/algorithms/binary_search/bsearch_kernels.h`.
- Study: `benchmarks/binary_search/bench_bsearch.cpp` (harness),
  `benchmarks/outputs/bsearch_ryzen2600x_*.csv` (results),
  `docs/papers/binary-search/` (paper).
- The prototype's assembly and AVX2 searches (`legacy/`,
  `src/backends/cpu/x86_64/binary_search_win64.asm`) are kept for the
  comparison only; see `legacy/README.md` for why their first results did
  not hold.

## Papers
- `docs/papers/binary-search/`: what makes binary search fast, from L1 to DRAM
- `docs/papers/bucketization/`: comparison-free re-sorting of multi-column views
- Build: `python docs/papers/build_papers.py` (needs a LaTeX distribution with latexmk)

## Documentation index
- Architecture: `docs/architecture/overview.md`
- Bucketization design and cross-language spec: `docs/bucketization/README.md`
- Bucketization benchmarks against real file manager stacks (Dolphin, GNOME family): `docs/bucketization/benchmarks.md`
- Bucketization in the sort code of 10 more file managers (GNOME family, Qt, terminal) and the Godot editor: `docs/bucketization/cross-project-benchmarks.md`
- How file managers sort today, and where to contribute: `docs/bucketization/file-manager-survey.md`
- Launch-day contribution plan and per-project kits: `docs/bucketization/launch/README.md`
- Plan for contributing to KDE Dolphin: `docs/bucketization/kde-contribution-plan.md`
- Binary search study: `docs/papers/binary-search/`
- Benchmark methodology baseline: `BENCHMARKS.md`
- API docs (Doxygen): `docs/api/README.md`

## Contributing
See `CONTRIBUTING.md` and issue/PR templates under `.github/`.
