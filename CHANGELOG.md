# Changelog

All notable changes to SpeedUp. Versions follow
[Semantic Versioning](https://semver.org/); while the major version is 0,
a new minor version may change APIs.

## [0.3.0] - unreleased

Binary search, measured properly. The earlier benchmark drew its keys with
`rand()`, which never exceeds 32,767, so it never searched beyond the first
256 KiB of an array; its large-array results did not hold. A new harness
measures 22 variants from 8 KiB to 512 MiB, and the library now uses what
won.

### Added

- `speedup_lower_bound_i64`, `speedup_binary_search_batch_i64` and
  `speedup_lower_bound_batch_i64`. The batch functions search 16 keys in
  lockstep: about 4× faster per key than single searches on arrays larger
  than the caches.
- Benchmark harness `benchmarks/binary_search/bench_bsearch.cpp` (throughput
  and latency modes, every variant verified before and during timing),
  runner `benchmarks/scripts/run_bsearch_benchmarks.py`, results
  `benchmarks/outputs/bsearch_ryzen2600x_*.csv`.
- Papers, with every number generated from the result files:
  `docs/papers/binary-search/` and `docs/papers/bucketization/`
  (`docs/papers/build_papers.py` builds them and packs them for arXiv).
- `legacy/README.md`: what the prototype files are and why their results do
  not hold; the disassembly of the prototype's lost `*_fixed` functions.

### Changed

- `speedup_binary_search_i64` used the textbook search on every path. It now
  uses a branchless search, with prefetching once the array exceeds the L2
  cache hint (`speedup_set_cache_hint`): about 2× faster in the caches, and
  never more than a few percent slower than the textbook search on large arrays.
  When the key occurs more than once, it now returns the first occurrence.
- `speedup_cpu_has_avx2` detects AVX2 (it returned 0).
- A forced CUDA or OpenCL preference no longer makes the search return -1
  (neither backend implements it yet).
- `benchmarks/core/benchmark_win.c` draws its keys with SplitMix64;
  `-DBENCH_LEGACY_RAND` restores the old generator.
- The Bucketization benchmarks build on Windows (nnn's entry struct used
  POSIX-only types); `run_bucketization_benchmarks.py --tag` keeps the results
  of other machines apart.

## [0.2.0] - 2026-10-27

Bucketization: fast re-sorting of collections that can be ordered in
several ways, such as a file manager's name, size and date columns. A
column click regroups the elements in O(n) and sorts only the rows on
screen. The binary-search library from 0.1.0 is unchanged.

### Added

- **Bucketization, C++17:** header-only, `include/speedup/bucketization.hpp`,
  CMake target `speedup::bucketization`. Interface `BucketedList<T>`,
  reference implementation `BucketIndex<T>`, with `SortKey`, `BucketPlan`
  and `BucketLayout`. Unit tests (`tests/unit/test_bucketization.cpp`) and an
  example (`examples/bucketization/file_browser.cpp`).
- **Bucketization, Java 17:** Maven module `bindings/java/bucketization`
  (`io.github.attilatorda.speedup:speedup-bucketization`), same interface and
  behaviour, no runtime dependencies. CI job `java-bucketization`.
- **Design and cross-language specification:** `docs/bucketization/README.md`,
  with golden values (S1–S8) that every port must reproduce.
- **Benchmarks against real file-manager sort code**, on real folder
  listings:
  - KDE: Dolphin (`KFileItemModel`), the file dialogs (KIO's
    `KDirSortFilterProxyModel`), Krusader;
  - GNOME family: GLib's `g_sequence_sort` (Thunar, Nemo, Caja) and
    Nautilus's GTK 4 sort models;
  - Qt: PCManFM-Qt (libfm-qt);
  - terminal: yazi (native Rust), lf (native Go), nnn;
  - Godot's editor FileSystem dock.

  Runner: `benchmarks/scripts/run_bucketization_benchmarks.py`; results in
  `benchmarks/outputs/`; write-ups in `docs/bucketization/benchmarks.md` and
  `docs/bucketization/cross-project-benchmarks.md`.
- **Comparator checks** that found three upstream issues, reported on
  2026-10-27: a non-transitive natural sort in Dolphin, a descending
  comparator in Krusader that is not a strict weak ordering, and per-comparison
  name normalization in lf (`docs/bucketization/launch/tickets/`).
- **Survey** of how 12 file managers sort (`docs/bucketization/file-manager-survey.md`),
  a KDE contribution plan and a launch kit with one post per project
  (`docs/bucketization/launch/`).
- **License exception** (`LICENSE-EXCEPTION-Bucketization.md`): Dolphin, KIO,
  Krusader, Thunar, Nemo, Caja, Nautilus, GLib, GTK, PCManFM-Qt, libfm-qt,
  yazi, lf, nnn and Godot may use the Bucketization code, and ports of it,
  under their own licenses. SpeedUp itself stays AGPL-3.0.
- This changelog.

### Changed

- Version 0.2.0 in CMake, `speedup/version.h`, the Doxygen configuration and
  the Java module.

## [0.1.0] - 2026-05-14

First release: an optimized binary search with a fallback design.

### Added

- **C API** (`include/speedup/api.h`) with a stable C ABI:
  `speedup_init()` and `speedup_binary_search_i64()`.
- **Backends, chosen at run time:**
  - portable C reference implementation, always available;
  - Windows x64 assembly (NASM), disabled automatically when NASM is missing;
  - GPU paths for CUDA and OpenCL, with runtime checks and CPU fallback.
- **CPU feature detection and dispatch** (`src/core/`).
- **Code generation** for type specializations (`codegen/`).
- **Tests:** unit tests for binary search, legacy correction tests, smoke test.
- **Benchmarks:** Windows x64 benchmark and baseline results; the original
  benchmark assets in `legacy/`.
- **Bindings:** shared-library build, a Python smoke test, and setup notes for
  C, C++, C#, Go, Java, JavaScript, Kotlin, Python, R, Rust, Scala, Swift,
  TypeScript and Zig.
- **Documentation:** architecture overview, benchmark methodology and
  analysis, Doxygen setup, contributing guide, issue and PR templates.
- **CI:** build and test workflow, backend compile checks, bindings smoke
  test.

[0.2.0]: https://github.com/attilatorda/SpeedUp/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/attilatorda/SpeedUp/releases/tag/v0.1.0
