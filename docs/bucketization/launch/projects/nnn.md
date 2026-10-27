# nnn

| | |
|---|---|
| Where | <https://github.com/jarun/nnn/discussions> (ideas). nnn's maintainer keeps the program small and is selective about features |
| Account | GitHub |
| License | BSD-2-Clause. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license; it covers a C port too |
| AI policy (2026-10-24) | none found. Disclose anyway |
| Fit | **low**. The gains are real for big folders, but nnn values a tiny codebase, and typical folders already sort in under 1 ms. Post only if you have time; it is not needed for priority |
| First post | optional discussion with numbers |

## Numbers

nnn's `qsort()` with `entrycmp()` / `reventrycmp()` from `d1ee163` (glibc
2.39). Default `xstricmp` names: numbers by value first, then `strcoll()`.
Folders first; the size and time orders are nnn's (largest and newest
first). The pre-sort is high for names because the name key needs
`strxfrm()` per file.

<!-- numbers:full nnn -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.34–2.2 | 0.024–0.095 | 14–54× | 0.40–2.9 |
| usr-bin | 1,172 | 0.19–1.0 | 0.007–0.024 | 14–139× | 0.23–1.0 |
| node-modules-tree | 36,069 | 12–54 | 0.072–0.10 | 141–677× | 1.9–38 |
| usr-tree | 97,974 | 42–181 | 0.13–0.28 | 205–991× | 6.1–66 |
| linux-tree | 97,005 | 30–290 | 0.14–0.26 | 176–1,379× | 4.1–64 |
| photos-100k | 100,000 | 7.7–282 | 0.11–0.26 | 72–1,246× | 2.6–106 |
<!-- /numbers -->

## Day-0 post (optional)

**Title:** Idea: faster sort-order switching for very large directories

> Hi! I've published a sorting design for file views, Bucketization
> (<RELEASE_URL>), and measured it against nnn's `ENTSORT` path
> (`qsort()` with `entrycmp()`, from d1ee163) on real directory listings:
>
> <!-- numbers:post nnn -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 0.34–2.2 ms | 0.024–0.095 ms |
> | node_modules, flattened | 36,069 | 12–54 ms | 0.072–0.10 ms |
> | /usr, flattened | 97,974 | 42–181 ms | 0.13–0.28 ms |
> | 100,000-photo camera roll | 100,000 | 7.7–282 ms | 0.11–0.26 ms |
> <!-- /numbers -->
>
> Under a few thousand entries there's nothing to gain, so I understand if
> this doesn't fit nnn's goals. For huge directories, a sort change would
> regroup the entries in O(n) and sort only the visible rows. It would need
> a small C port, which nnn could take under BSD-2-Clause. Is this something you'd consider at all?
> Benchmarks: <CROSS_PROJECT_URL>
>
> (Disclosure: the benchmark harness and the C++/Java reference
> implementation were written with an AI coding assistant under my
> direction.)
