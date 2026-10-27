# Legacy prototype (December 2025 – January 2026)

These files come from the first prototype of the binary-search work. They are
kept for history and to reproduce the first published numbers. The maintained
harness is in `benchmarks/binary_search/`.

| File | What it is |
|---|---|
| `bsearch.asm` | NASM, Win64 ABI: branching, "branchless" (cmov) and "optimized" searches, plus `rdtsc` helpers |
| `benchmark_win.c` | The original harness, with the key generator bug described below |
| `results.csv` | Numbers produced by that harness on a Ryzen 5 2600X. **Not valid for arrays of 100K elements and up** |
| `test_corrections.c` | Tests for three `*_fixed` functions whose source is lost (see below) |
| `build_windows.bat` | `nasm -f win64` + `gcc -O3`. Needs `-mavx2` added to build the SIMD variants |
| `recovered/corrected_search.disasm.txt` | Disassembly of the only surviving build of the `*_fixed` functions |

## The key generator bug

`benchmark_win.c` drew keys with `(rand() % size) * 2`. `RAND_MAX` is 32767
with both MSVC and MinGW, so only indices 0–32767 were ever targeted. For
arrays of 100K elements and up, every search stayed inside the first 256 KB.
That region fits in L2, and the first several comparisons always went left.
The numbers in `results.csv` (and the copy in
`benchmarks/outputs/results_baseline_windows.csv`) therefore measure
cache-hot searches, not memory-bound ones. The analysis built on them in
`docs/papers/bsearch_paper.html` and `docs/papers/drafts/` does not hold for
large arrays.

The copy in `benchmarks/core/benchmark_win.c` now uses a 64-bit generator.
Building it with `-DBENCH_LEGACY_RAND` restores the old generator, so the
effect of the bug can be measured.

## Other known issues

- The "optimized" search loads the next midpoint only after the comparison
  resolves. That is the same dependency chain as the branching search: it
  does not prefetch or pipeline anything.
- The "branchless" search still exits early on a match, so it keeps a
  data-dependent branch.
- `measure_cycles_win64` calls the function without the 32-byte shadow space
  and with a misaligned stack. It is safe only for the asm callees.

## The lost `*_fixed` functions

`test_corrections.c` calls `branching_cache_optimized_fixed`,
`branchless_cache_optimized_fixed` and `branchless_prefetch_optimized_fixed`.
Their source was never saved. The only build left was an ELF object
(System V ABI, assembled on Linux). Its disassembly is in
`recovered/corrected_search.disasm.txt` for reference, so the tests cannot be
built from this folder.
