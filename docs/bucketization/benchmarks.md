# Bucketization benchmarks on real file manager stacks

How much faster is Bucketization than what file managers do today, measured
with their own sorting code on real directory listings, and where is it not
faster? Everything here can be rerun with one command (see
[Reproducing](#reproducing)).

## Summary

On real folders of 36,000–98,000 entries, against Dolphin's own sorting code
on Qt 6 and ICU:

| | Dolphin today | Bucketization |
|---|---:|---:|
| Click a column, until the visible rows are final | 59–671 ms | 0.08–0.72 ms |
| Click a column used before | 59–671 ms | < 0.001 ms |
| Open a folder (name order, sorting only) | 208–434 ms | 13–40 ms |
| Paid once per column (pre-sort, both directions) | – | 3–50 ms |

- Column switches are **178–3,842× faster** on real folders, and 800–2,700×
  at 1,000,000 entries (names: 8.9 s → 4.3 ms).
- **Ten more file managers**, measured in their own sort code, are in
  [cross-project-benchmarks.md](cross-project-benchmarks.md).
- **GNOME family** (cached GLib collation keys): a full re-sort takes 9–203 ms
  on the same folders; Bucketization takes 0.06–0.27 ms, after a pre-sort of
  1.5–7 ms.
- **Same order** as the file manager's own sort in every case where that
  order is well defined. Along the way the benchmarks found that Dolphin's
  natural comparison has cycles for version-numbered names
  (`kernel-6.10` < `kernel-6.9` < `kernel-6.9.1` < `kernel-6.10`).
- **Where it is not faster:** folders under ~2,000 entries, where both take
  at most a few milliseconds (a 60 Hz frame is 16.7 ms); pre-sorting all
  three columns of a 395-entry folder takes 1.4 ms against Dolphin's 1.0 ms.
  `std::partial_sort` of the visible page is the strongest simple
  alternative: on the real folders a median 10× (3–150×) slower than a
  Bucketization switch, 212 ms in the worst case, and it repeats its work on
  every click.

## What is measured

A file manager re-sorts in three situations; each has its own number.

| Situation | What the user does | Measured |
|---|---|---|
| **Switch** | clicks a column header (name / size / modified), or the same header again to flip the direction | time until the rows on screen (first 64) are in their final order |
| **Switch back** | returns to a column used before | same, with Bucketization's cached grouping |
| **Open** | enters a folder | time until the first page is sorted (sorting only, not listing the files) |

Bucketization also pays a **pre-sort** per column: computing every element's
key and bucket once. It is reported separately, because it can run while the
folder is listed or the first time a column is used, and it is paid once,
not per click.

Baselines:

- **Dolphin**: `KFileItemModel::resortAllItems()` as Dolphin runs it: its
  comparison, its parallel merge sort, and the bookkeeping after the sort
  (rebuilding the URL→row hash and computing the moved-items list for the
  view). "of which sort" is the sort call alone.
- **GNOME family**: `g_sequence_sort()` (Thunar, Nemo, Caja) and
  `std::stable_sort` standing in for GTK 4's `GtkSortListModel` (Nautilus),
  comparing cached `g_utf8_collate_key_for_filename()` keys.
- **`std::partial_sort`** of the first page with the same comparison: the
  obvious "only sort what is visible" alternative, without pre-sorting.

## The stacks

### KDE Dolphin

`benchmarks/kde_stack/dolphin_sort.h` is Dolphin's sorting code from
`src/kitemviews/kfileitemmodel.cpp` and `private/kfileitemmodelsortalgorithm.h`
at commit `c5849f9` (2026-10-24), under its original GPL-2.0-or-later
license, reduced to flat folder views:

- natural, case-insensitive comparison: `decimalAwareNaturalCompare()` on top
  of `QCollator` (Qt 6.4.2 on ICU 74), folders first, and Dolphin's fallbacks
  (text, then name, then URL);
- the rotation-based merge sort, run on 4 threads for the name column and on
  one thread for the others, as in Dolphin;
- `resortAllItems()` and the sorting part of `insertItems()`.

Bucketization runs in **keyed mode**: each name's key is the start of an ICU
primary-strength sort key of the name up to its first number, numbers by
value (`benchmarks/kde_stack/icu_natural_key.h`); sizes and times are exact
keys. Dolphin's own comparison is the tie-breaker, so the order is Dolphin's
by construction. Folders first is a **group**, so one pre-sort serves both
directions. **Comparator mode** (no key, Dolphin's comparison only) is
measured too.

### GNOME family

`benchmarks/gnome_stack/benchmark_glib_stack.cpp` caches one GLib collation
key per file, as Nautilus, Thunar, Nemo and Caja do, compares keys with
`strcmp`, and keeps folders first. It is not a copy of any one of these file
managers, but the expensive parts (collation keys, `g_sequence_sort`) are
GLib's own. Bucketization's key is the first 8 bytes of the same collation
key, so the order is identical by construction.

## Datasets

Real listings unless marked synthetic; details and capture commands in
[`benchmarks/datasets/README.md`](../../benchmarks/datasets/README.md).

| Dataset | Entries | What it is |
|---|---:|---|
| usr-bin | 1,172 | real folder `/usr/bin` |
| usr-lib | 1,533 | real folder `/usr/lib/x86_64-linux-gnu` |
| node-modules-top | 395 | top of a real `node_modules` (754-package web app) |
| node-modules-tree | 36,069 | that `node_modules` flattened (recursive search view) |
| usr-tree | 97,974 | all of `/usr` flattened |
| linux-tree | 97,005 | every file of the Linux kernel tree; no timestamps (all equal) |
| photos-100k | 100,000 | synthetic camera roll |
| photos-1m | 1,000,000 | synthetic camera roll |
| intl-20k | 20,000 | synthetic multilingual names, for correctness checks |

## Environment and method

- 4-vCPU Intel Xeon @ 2.1 GHz cloud container, Ubuntu 24.04, GCC 13.3,
  CMake Release build (`-O3`), Qt 6.4.2, ICU 74.2, GLib 2.80.
- Locale `en_US` (QCollator / ICU) and `en_US.UTF-8` (GLib); page size 64;
  Bucketization's default plan.
- Every number is the median of 5 runs (1 run for the 1,000,000-entry
  dataset). Small timings (< 0.1 ms) are at the edge of timer resolution;
  treat them as "well under a millisecond".
- Dolphin's name sort uses 4 threads; Bucketization uses one.
- **Correctness:** every Bucketization result is compared with the stack's
  own result, element by element: the first page for every measurement and
  the whole order once per column and direction.
- These tables were measured at commit `2afceb1`. A full re-run with the
  current code gave the same results within run-to-run noise: the median
  ratio per column was 0.96–1.06 for Dolphin and 0.96–0.99 for GLib, and
  every order matched. The only change since then is that buckets are
  checked for order before sorting.

## Results

Selected rows; every dataset, column and direction is in the generated
[`benchmarks/outputs/bucketization_file_manager_stacks.md`](../../benchmarks/outputs/bucketization_file_manager_stacks.md)
(CSV next to it). Times in milliseconds. Medians from separate runs, so "of
which sort" can be slightly above the total on small folders.

### KDE Dolphin: switching columns

| dataset | entries | column | order | Dolphin re-sort | of which sort | partial_sort | Bucketization switch | switch back | pre-sort (once) |
|---|---:|---|---|---:|---:|---:|---:|---:|---:|
| node-modules-tree | 36,069 | name | asc | 236 | 201 | 0.81 | 0.26 | <0.01 | 11 |
| node-modules-tree | 36,069 | modified | desc | 90 | 69 | 1.08 | 0.37 | <0.01 | 16 |
| usr-tree | 97,974 | name | asc | 550 | 498 | 2.97 | 0.72 | <0.01 | 22 |
| usr-tree | 97,974 | size | desc | 208 | 166 | 6.37 | 0.27 | <0.01 | 14 |
| linux-tree | 97,005 | name | asc | 671 | 581 | 30 | 0.47 | <0.01 | 27 |
| linux-tree | 97,005 | modified | desc | 208 | 142 | 212 | 0.25 | <0.01 | 47 |
| photos-100k | 100,000 | name | asc | 666 | 578 | 28 | 0.18 | <0.01 | 48 |
| photos-100k | 100,000 | modified | desc | 64 | 17 | 2.31 | 0.13 | <0.01 | 3.02 |
| photos-1m | 1,000,000 | name | asc | 8,876 | 7,516 | 411 | 4.27 | <0.01 | 567 |
| photos-1m | 1,000,000 | size | desc | 2,334 | 850 | 13 | 1.45 | <0.01 | 50 |
| photos-1m | 1,000,000 | modified | desc | 1,278 | 233 | 21 | 1.44 | <0.01 | 37 |
| usr-lib | 1,533 | name | asc | 4.42 | 3.51 | 0.30 | 0.04 | <0.01 | 0.73 |
| usr-bin | 1,172 | size | desc | 0.51 | 0.32 | 0.07 | 0.01 | <0.01 | 0.18 |

### KDE Dolphin: opening a folder

| dataset | entries | Dolphin `insertItems` | Bucketization, visible column | all 3 columns pre-sorted |
|---|---:|---:|---:|---:|
| usr-bin | 1,172 | 2.19 | 0.44 | 1.16 |
| usr-lib | 1,533 | 9.11 | 0.87 | 2.03 |
| node-modules-top | 395 | 1.03 | 0.15 | 1.39 |
| node-modules-tree | 36,069 | 208 | 13 | 33 |
| usr-tree | 97,974 | 434 | 40 | 92 |
| linux-tree | 97,005 | 346 | 28 | 66 |
| photos-100k | 100,000 | 216 | 46 | 54 |
| photos-1m | 1,000,000 | 2,224 | 657 | 731 |

### GNOME family: switching columns

| dataset | entries | column | order | `g_sequence_sort` | `stable_sort` | partial_sort | Bucketization switch | pre-sort |
|---|---:|---|---|---:|---:|---:|---:|---:|
| node-modules-tree | 36,069 | name | asc | 39 | 22 | 0.30 | 0.07 | 2.44 |
| usr-tree | 97,974 | name | asc | 147 | 63 | 0.84 | 0.22 | 4.59 |
| linux-tree | 97,005 | name | asc | 199 | 68 | 2.35 | 0.18 | 3.54 |
| linux-tree | 97,005 | modified | desc | 42 | 25 | 20 | 0.19 | 6.52 |
| photos-100k | 100,000 | name | asc | 209 | 61 | 2.68 | 0.17 | 5.31 |
| photos-1m | 1,000,000 | name | asc | 4,909 | 2,100 | 123 | 2.60 | 80 |
| photos-1m | 1,000,000 | modified | desc | 669 | 169 | 10 | 1.57 | 11 |
| usr-lib | 1,533 | name | asc | 0.76 | 0.26 | 0.03 | <0.01 | 0.22 |

The GNOME benchmark uses one composite key per direction (a folder bit above
the collation-key prefix), so its pre-sort is per direction.

## Analysis

**Against Dolphin today.** A column click in Dolphin costs a full
O(n log n) sort with an expensive comparison (QCollator on every call), plus
17–123 ms of bookkeeping on the real 36k–98k folders (≈1.1 s at 1M entries)
for the URL→row hash and the moved-items list. Bucketization does no
comparisons at the click: it regroups by stored bucket ids and sorts the
visible bucket, so the click costs well under a millisecond, independent of
how expensive the comparison is. Any real integration still has to avoid
Dolphin's full-order bookkeeping (see the contribution plan); otherwise that
part alone stays at 17–123 ms.

**Against `std::partial_sort`.** Sorting only the visible page without any
pre-sorting is the obvious simpler alternative, and it is good: 0.8–30 ms on
the real folders, median about 10× slower than a Bucketization switch. Its
weaknesses: it compares every element on every click (n comparisons at
least), it cannot reuse work when the user switches back, and it degrades
badly when many keys are equal. On linux-tree sorted by date (all
timestamps equal) every comparison falls through to Dolphin's expensive name
fallback: 212 ms, against 0.25 ms for Bucketization, whose layout splits
runs of equal keys with the tie-breaker.

**Pre-sort cost.** One column's pre-sort on the real folders costs 3–50 ms,
i.e. 2–26 % of a single Dolphin re-sort of that folder, so it pays for itself
on the first click. It can also run while the folder is being listed, which
takes far longer than sorting. Comparator mode (no key) switches just as
fast but pre-sorts 2–5× slower (8–68 ms).

**Opening a folder.** Dolphin sorts every item before showing the first
row; Bucketization sorts the visible page: 11–16× faster on the real large
folders with only the visible column pre-sorted, 5–6× with all three (5×
and 4× on the synthetic camera roll). Folder opening therefore
gets faster too, as long as the other columns are pre-sorted lazily or in
the background.

**Small folders.** Below about 2,000 entries Dolphin needs 0.3–9 ms and
Bucketization 0.003–2 ms; nobody can see either. Eagerly pre-sorting all
three columns is the one case where Bucketization measured slower (395
entries: 1.39 vs 1.03 ms). An integration should keep the existing sort for
small folders and pre-sort columns on first use.

**GNOME family.** Cached collation keys make each comparison a `strcmp`, so
full sorts are 3–11× cheaper than Dolphin's; `g_sequence_sort` (a balanced
tree) is another 1.7–3× slower than sorting a vector. Bucketization's switch
is still 119–380× faster than the vector sort, and its pre-sort costs almost
nothing extra (1.5–7 ms) because the 64-bit key is just the first 8 bytes of
the collation key these file managers already compute.

**Memory.** Per element and column: 8 bytes of key (keyed mode), 2 bytes of
bucket id and 4 bytes of cached grouping, i.e. 14 bytes. Three columns over
100,000 files: about 4.2 MB, plus one pointer per element for the element
list.

**Limits of these measurements.**

- Only the sorting stage is measured: not KIO listing, not item creation,
  not the view's layout and painting.
- Dolphin's code is an extract for flat views (no tree view, no grouping),
  rebuilt with Qt 6.4; Dolphin's 4-thread name sort is compared with
  single-threaded Bucketization, which favours Dolphin.
- One machine (a 4-vCPU cloud container); timings are for `en_US`, while
  correctness was checked in seven locales.
- The camera rolls and the multilingual names are synthetic.
- Medians of 5 runs; the 1,000,000-entry rows are single runs.

## Correctness findings

**Bucketization reproduced the stack's exact order in every case where that
order is well defined:** all real datasets, both stacks, all columns, both
directions.

The multilingual dataset exposed two things:

1. **Dolphin's natural comparison has cycles.** It reads a two-part number
   such as `6.10` as a decimal (6.10 < 6.9) but compares two-part with
   three-part numbers part by part (6.9.1 < 6.10). Ordinary download names
   therefore cycle:

   ```
   kernel-6.10.tar.xz < kernel-6.9.tar.xz < kernel-6.9.1.tar.xz < kernel-6.10.tar.xz
   app-1.10.zip < app-1.5.zip < app-1.7.1.zip < app-1.10.zip
   ```

   A comparison with cycles has no single correct order: what Dolphin shows
   depends on the order the folder was listed in, and even `std::stable_sort`
   with Dolphin's own comparison gives a different order than Dolphin's merge
   sort (32 of 20,000 positions on intl-20k). No algorithm can match it
   exactly there; the benchmark reports these cases as "n/a" instead of a
   mismatch. `speedup_find_comparator_cycles` reproduces the cycles; this is
   worth reporting to Dolphin on its own (see below).

2. **The ICU key never contradicted Dolphin.** `speedup_verify_icu_key`
   walks Dolphin's sorted output and checks that the key never decreases
   between neighbours (up to 8 apart) and random pairs: about 2.3 million
   pairs per locale over intl-20k, usr-lib, node-modules-tree and
   linux-tree, with **0 violations in en_US, cs_CZ, de_DE, sv_SE, tr_TR,
   fr_FR and ja_JP**. These locales cover contractions (Czech "ch" sorts
   after "h"), Swedish å/ä/ö, Turkish dotted/dotless i, French accents and
   Japanese scripts.

Because real comparators can have cycles, Bucketization now sorts buckets
with merge sorts only (`std::stable_sort` / `std::inplace_merge` in C++, its
own merge sort in Java): with an inconsistent comparator `std::sort` is
undefined behaviour. Java's TimSort, still used for the small layout sample,
throws "Comparison method violates its general contract" on such
comparators, so the layout falls back to the merge sort when that happens.
Two cyclic test comparators (one of which makes TimSort throw) are part of
both test suites.

## Improvements made because of these benchmarks

| Problem found | Change | Effect |
|---|---|---|
| Dolphin keeps folders first in both directions; Bucketization's descending order reversed everything, so each direction needed its own key and pre-sort | **groups** (`SortKey::with_groups`): classes that keep their order in both directions, each with its own layout | one pre-sort per column instead of two; descending first page is page-sized too |
| The first ICU key (letters before the first digit) gave every `IMG_…` name the same key, so pre-sorting camera folders cost more than comparator mode (81 vs 59 ms on photos-100k) | the key now also covers the **first number, by value** (still provably consistent with Dolphin) | pre-sort 81 → 48 ms on photos-100k (name, ascending) |
| Real comparators can be cyclic | **merge sorts only** inside buckets (C++ and Java) and for C++ layout samples; Java samples fall back to the merge sort when TimSort rejects the comparator | no undefined behaviour or exceptions; every element always returned |
| lf has no tie-breaker for size and time, and linux-tree has no timestamps: every file tied, so the first bucket held the whole folder and a switch cost as much as lf's own sort (7.5 vs 7.2 ms, [cross-project](cross-project-benchmarks.md)) | C++ buckets are **checked for order before sorting** (Java's merge sort already skips sorted runs) | a bucket of ties costs one pass; same results, since a stable sort would not move a sorted bucket |

## Reproducing

```bash
sudo apt install qt6-base-dev libicu-dev libglib2.0-dev libgtk-4-dev   # optional stacks
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build          # all datasets
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --quick  # skip 1M entries
./build/speedup_verify_icu_key cs_CZ build/bucketization-datasets/*.tsv
./build/speedup_find_comparator_cycles
```

Your own folders: capture them with
`benchmarks/datasets/capture_listing.py dir PATH -o mine.tsv` and pass
`--extra mine.tsv` to the runner. The CSV files and the generated tables go
to `benchmarks/outputs/`.
