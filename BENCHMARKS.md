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

## Bucketization (switching sort keys)

Measures the time from "user clicks a sort column" until the first page of
rows is sorted, against a full sort, on synthetic directories (camera-style
names with long shared prefixes, numbered documents, random words; log-normal
sizes; random timestamps).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target speedup_benchmark_bucketization
./build/speedup_benchmark_bucketization benchmarks/outputs/results_bucketization_linux.csv 1000000

cd bindings/java/bucketization && mvn -q test-compile
java -Xmx4g -cp target/classes:target/test-classes \
    io.github.attilatorda.speedup.bucketization.examples.BucketizationBenchmark 1000000
```

Columns: full sort of the column; pre-sort (registering 4 sort keys and
assigning every element to its buckets, paid once per directory load);
switch + first page (`sort_by` + `first_page`, 64 rows); switching back to a
cached key; switch + sorting everything.

Results (medians), 4-vCPU Intel Xeon @ 2.1 GHz container, Linux.
C++: GCC 13 `-O3` (Release). Java: OpenJDK 21. CSV: `benchmarks/outputs/results_bucketization_linux.csv`.

| n | key | C++ std::sort | C++ switch + page | Java List.sort | Java switch + page |
|---|-----|---------------|-------------------|----------------|--------------------|
| 10,000 | name | 1.7 ms | 0.025 ms | 3.5 ms | 0.10 ms |
| 100,000 | name | 41 ms | 0.18 ms | 55 ms | 0.49 ms |
| 1,000,000 | name | 1,052 ms | 2.1 ms | 1,034 ms | 3.7 ms |
| 1,000,000 | size | 416 ms | 1.8 ms | 695 ms | 2.8 ms |
| 1,000,000 | modified | 448 ms | 2.1 ms | 927 ms | 3.1 ms |

Pre-sorting 1,000,000 elements for all four keys: 135 ms (C++), 226 ms (Java).
Java numbers vary by about ±20% between runs (JIT).

## Bucketization vs. real file manager stacks

Compares Bucketization with the sorting code file managers actually run, on
real directory listings (`benchmarks/datasets/`):

- `speedup_benchmark_dolphin_stack`: KDE Dolphin's comparison, parallel merge
  sort and `resortAllItems()` bookkeeping, extracted from Dolphin, on Qt 6
  `QCollator`/ICU (needs Qt 6 and ICU development packages)
- `speedup_benchmark_glib_stack`: GLib collation keys with `g_sequence_sort`
  (Thunar, Nemo, Caja) and a stable sort standing in for GTK 4's
  `GtkSortListModel` (Nautilus) (needs GLib development packages and an
  `en_US.UTF-8` locale)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build   # add --quick to skip 1M entries
```

Results: `benchmarks/outputs/bucketization_dolphin_stack.csv`,
`benchmarks/outputs/bucketization_glib_stack.csv` and the generated tables in
`benchmarks/outputs/bucketization_file_manager_stacks.md`. Method and
analysis: `docs/bucketization/benchmarks.md`.

The same runner also runs the per-project benchmarks (Thunar, Nemo, Caja,
Nautilus on GTK 4, PCManFM-Qt, the KDE file dialogs, Krusader, nnn, yazi and
lf natively in Rust and Go, and the Godot editor's FileSystem dock;
`--only-projects` runs just these). They need
`libgtk-4-dev` for Nautilus, plus `cargo` and `go` for the native programs,
and write `benchmarks/outputs/bucketization_{gnome,qt,terminal}_family.csv`,
`bucketization_godot.csv` and `bucketization_{yazi,lf}_native.csv`. Analysis:
`docs/bucketization/cross-project-benchmarks.md`.

## Add a new benchmark checklist
1. Add benchmark source under `benchmarks/core/`.
2. Add target wiring in `CMakeLists.txt` behind platform guards.
3. Save outputs under `benchmarks/outputs/`.
4. Add interpretation notes to `docs/papers/`.
