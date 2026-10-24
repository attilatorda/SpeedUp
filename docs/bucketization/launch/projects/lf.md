# lf

| | |
|---|---|
| Where | <https://github.com/gokcehan/lf/issues> (search first for "sort", "slow" and "natural") |
| Account | GitHub |
| License | MIT. Bucketization under MIT fits; a Go port would be MIT, under your copyright |
| AI policy (2026-10-24) | none found in the repository (`CONTRIBUTING.md`). Disclose anyway |
| Fit | **high for a first contribution**: lf's default name sort spends most of its time normalizing names on every comparison. A small, self-contained change makes it 4–8× faster with identical order. Bucketization is the step after that |
| First post | issue about the normalization finding, filed before launch without mentioning Bucketization ([../tickets/lf-normalize-per-comparison.md](../tickets/lf-normalize-per-comparison.md)); a separate Bucketization issue later, if wanted |
| Code needed | first a few lines in `nav.go`; for Bucketization, a Go port |

## Numbers

lf's `dir.sort()` from `664d7c8`, compiled natively with Go 1.24. Defaults:
natural sort, ignore case and diacritics, folders first; hidden files shown.
Bucketization times come from the C++ port of the same comparisons, whose
orders are identical to lf's (same hash).

<!-- numbers:full lf -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.24–17 | 0.004–0.32 | 50–364× | 0.085–3.3 |
| usr-bin | 1,172 | 0.19–12 | 0.004–0.064 | 29–1,239× | 0.096–0.97 |
| node-modules-tree | 36,069 | 23–651 | 0.057–0.27 | 252–7,501× | 1.3–15 |
| usr-tree | 97,974 | 34–2,003 | 0.18–0.43 | 159–11,009× | 3.3–38 |
| linux-tree | 97,005 | 8.0–2,345 | 0.14–1.0 | 8–6,384× | 2.3–33 |
| photos-100k | 100,000 | 15–3,050 | 0.091–0.31 | 162–12,108× | 2.7–184 |
<!-- /numbers -->

Name sort with each name normalized once per sort instead of on every
comparison, same order (from `bucketization_lf_native.csv`):

<!-- numbers:lf-normalize full -->
| folder | entries | lf today (ms) | names normalized once (ms) | faster by |
|---|---:|---:|---:|---:|
| usr-lib | 1,533 | 16–17 | 2.5–3.7 | 4–7× |
| usr-bin | 1,172 | 7.6–12 | 1.5–1.7 | 5–8× |
| node-modules-tree | 36,069 | 599–651 | 110–114 | 5–6× |
| usr-tree | 97,974 | 1,862–2,003 | 391–404 | 5× |
| linux-tree | 97,005 | 2,098–2,345 | 404–452 | 5× |
| photos-100k | 100,000 | 2,969–3,050 | 449–481 | 6–7× |
<!-- /numbers -->

## Day-0 post

Superseded: the ticket in [../tickets/](../tickets/) is the version to file
now, without the Bucketization paragraph. Keep the paragraph below for a
later follow-up issue.

**Title:** Natural sort normalizes both names on every comparison (1.9–3.0 s for 100k files; 4–8× faster if done once per sort)

> Hi! I've been benchmarking file managers' sort code for a sorting design
> I published today (<RELEASE_URL>), and lf's default sort turned up
> something simple.
>
> **What happens:** with `sortby natural`, `sortignorecase` and
> `sortignoredia` (the defaults), the comparison in `dir.sort()` calls
> `normalize()` on both names. That is `strings.ToLower` plus
> `removeDiacritics`, so every comparison allocates new strings. A sort does
> about n·log₂n comparisons, so a 100k-file folder normalizes about 3.3
> million names per sort.
>
> **Measured** with lf's `dir.sort()` (commit 664d7c8, Go 1.24, real folder
> listings, hidden files shown, medians of 5):
>
> <!-- numbers:lf-normalize post -->
> | folder | files | name sort today | names normalized once |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 16–17 ms | 2.5–3.7 ms |
> | node_modules, flattened | 36,069 | 599–651 ms | 110–114 ms |
> | /usr, flattened | 97,974 | 1.9–2.0 s | 391–404 ms |
> | 100,000-photo camera roll | 100,000 | 3.0 s | 449–481 ms |
> <!-- /numbers -->
>
> "Normalized once" means computing `normalize(f.Name())` for each file
> before `applySort`, for example into a map or a slice next to the files,
> and comparing those. The order is identical in every case (checked).
> I'd be happy to send a PR.
>
> **Further:** Bucketization, the design I published, makes a sort change
> regroup the files in O(n) and sort only the visible rows. With a C++ port
> of lf's comparison that is 0.09–1.0 ms on 100k files, and about 1 µs when
> going back to an earlier sort. It would need a Go port. If that interests
> you, I can open a separate issue later. Benchmarks: <CROSS_PROJECT_URL>
>
> (Disclosure: the benchmark harness and the C++/Java reference
> implementation were written with an AI coding assistant, under my
> direction and review. A PR for lf I'd write and test myself.)

## Follow-ups

1. **The PR:** normalize once per `dir.sort()` call.
   - Keep `naturalCmp` as it is; only the strings change.
   - Add a test that the order is unchanged (the benchmark's order hash is
     a good cross-check).
   - Re-measure with `benchmarks/terminal_stack/lf_bench`.
2. With lf's default of hiding dotfiles, `dir.sort()` also runs a stable
   partition that glob-matches `hiddenfiles` patterns on every comparison
   (the `fm_resort_default_ms` column). Evaluating `isHidden` once per file
   is a second small PR.
3. Later, if wanted: a Go port of Bucketization.
