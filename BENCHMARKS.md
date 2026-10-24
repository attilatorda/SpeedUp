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
| 10,000 | name | 2.1 ms | 0.026 ms | 5.9 ms | 0.21 ms |
| 100,000 | name | 38.5 ms | 0.15 ms | 45.4 ms | 0.44 ms |
| 1,000,000 | name | 952 ms | 2.0 ms | 847 ms | 3.3 ms |
| 1,000,000 | size | 400 ms | 2.3 ms | 717 ms | 2.9 ms |
| 1,000,000 | modified | 395 ms | 1.9 ms | 768 ms | 2.4 ms |

Pre-sorting 1,000,000 elements for all four keys: 137 ms (C++), 184 ms (Java).

## Add a new benchmark checklist
1. Add benchmark source under `benchmarks/core/`.
2. Add target wiring in `CMakeLists.txt` behind platform guards.
3. Save outputs under `benchmarks/outputs/`.
4. Add interpretation notes to `docs/papers/`.
