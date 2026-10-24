# Bucketization across Linux file managers

Can Bucketization speed up file managers other than KDE Dolphin? This page
measures it in each project's own sort code, on the same real directory
listings as the Dolphin benchmarks ([benchmarks.md](benchmarks.md)), and gives
a verdict per project. The launch plan built on it is in
[launch/README.md](launch/README.md).

## Conclusion

**Yes, in every file manager measured.** On folders of 36,000–100,000
entries, a column click costs 6 ms to 3 s today. Bucketization gets the
visible rows final in 0.05–2 ms, plus a pre-sort of 1.3–206 ms paid once
per column. All 420 Bucketization orders measured here (10 projects ×
7 datasets × 3 columns × 2 directions) are sorted under the project's own
comparison; Dolphin's are in benchmarks.md.

<!-- numbers:summary -->
| project | re-sort today (ms) | Bucketization (ms) | speed-up | pre-sort, once (ms) |
|---|---:|---:|---:|---:|
| Dolphin | 59–713 | 0.080–0.72 | 178–3,842× | 2.1–73 |
| Thunar | 38–333 | 0.047–0.30 | 218–1,552× | 2.8–10 |
| Nemo | 36–382 | 0.057–0.22 | 380–2,206× | 2.6–11 |
| Caja | 45–389 | 0.048–0.24 | 312–1,953× | 2.5–10 |
| Nautilus | 63–480 | 0.052–0.26 | 729–2,921× | 2.6–12 |
| PCManFM-Qt | 48–1,499 | 0.092–2.0 | 322–7,640× | 2.6–206 |
| KIO dialogs | 84–1,384 | 0.079–1.2 | 356–6,647× | 2.0–159 |
| Krusader | 115–801 | 0.056–0.54 | 587–4,207× | 1.9–68 |
| nnn | 7.7–290 | 0.072–0.28 | 72–1,379× | 1.9–106 |
| yazi (native Rust) | 6.3–63 | 0.060–0.33 | 37–425× | 1.5–12 |
| lf (native Go) | 8.0–3,050 | 0.057–1.0 | 8–12,108× | 1.3–184 |
<!-- /numbers -->

Re-sort today is the project's own code path for a column click (yazi and
lf natively in Rust and Go). Bucketization is `sort_by` plus the first 64
rows, without a cached grouping; the ranges cover name, size and modified in
both directions. A column used before costs about 1 µs or less.

The gain is largest where comparisons are expensive and a full sort is
slow:

- Qt projects compare names with QCollator (or `localeAwareCompare`) on
  every comparison.
- lf normalizes both names on every comparison.

It is smallest where the project already sorts fast:

- yazi: Rust, with no allocation per comparison.
- The GNOME family compares cached collation keys.

Today, a 100,000-entry folder fits a 60 Hz frame (16.7 ms) only for a few
size and date columns of yazi, nnn and lf. With Bucketization, every column
of every project fits.

**Where it does not help, and where it costs:**

- **Small folders.** Under about 2,000 entries every project re-sorts in
  0.1–5 ms (lf: up to 17 ms, see below), already under a frame. The gain is
  real but invisible.
- **The pre-sort** is the price of the first click on a column: a median
  of 7 ms on the 100,000-entry folders. It reaches 40–206 ms where the
  64-bit key cannot tell the items apart, so the project's own comparison
  has to break the ties:
  - camera-roll names (`IMG_…`) under ICU and lf's normalized keys;
  - the kernel tree's identical timestamps in the Qt projects, which then
    tie-break by name through QCollator;
  - nnn's names, whose key needs `strxfrm()` per file.

  Even then, it stays below one full re-sort of the same folder.
- **Columns where everything ties.** lf has no tie-breaker for size and
  time, and the kernel tree has no timestamps. In that case every file
  belongs in one bucket. Bucketization now checks whether a bucket is
  already in order before sorting it. That keeps this case at one pass:
  0.9 ms against lf's 8 ms. Before the check, it was as slow as lf's own
  sort.

### Verdict per project

| Project | Gain on big folders | Integration effort | License fit (with Bucketization under MIT) | First contribution | Verdict |
|---|---|---|---|---|---|
| **Dolphin** | very large | medium: `KFileItemModel` owns the sort; its bookkeeping needs a lazy path | yes | bug report (natural-sort cycles), then a benchmark MR | **contribute first** |
| **Krusader** | very large | low: owns `std::stable_sort` in `ListModel::sort()` | yes | bug report: descending comparator is not a strict weak ordering | **contribute** |
| **KDE file dialogs (KIO)** | very large | high: `QSortFilterProxyModel` owns the sort | yes (LGPL-2.0-only file) | part of the KDE Discuss post | discuss |
| **PCManFM-Qt** | very large | high: `QSortFilterProxyModel`; a cheap win exists first (cached sort keys) | yes | issue: cached sort keys, then Bucketization | **contribute** |
| **Thunar** | large | medium: owns `g_sequence_sort()`; rows-reordered needs a full permutation; C port needed | yes | issue with numbers | **contribute** |
| **Nemo** | large | medium: same structure as Thunar | yes | issue with numbers | **contribute** |
| **Caja** | large | medium: same code as Nemo | yes | issue with numbers | **contribute** |
| **Nautilus** | large | high: GTK's `GtkSortListModel` owns the sort; strict about AI | yes | issue: measurements and a design question | discuss |
| **lf** | very large (name) | low for the first step (normalize once: 4–8×, no Bucketization needed); Go port for Bucketization | yes | issue + small PR (normalize once) | **contribute** |
| **yazi** | moderate in absolute terms (12–63 ms → 0.1–0.3 ms at 100k entries) | medium: Rust port; design approval required first | yes | feature request, human-written (policy) | contribute, carefully |
| **nnn** | moderate | low code, but the project avoids features | yes | optional discussion | low priority |

The ranking for a same-day launch is in [launch/README.md](launch/README.md).
The bug reports and lf's normalization fix go first: they help the projects
even if Bucketization is never merged.

## What was measured

For each project, its own code path for a column click (or a direction
change), extracted line by line from its source with its default settings.
That path runs against Bucketization on the same items, in the same order
as before the click:

| Project | Version (commit) | What runs on a column click | Measured with |
|---|---|---|---|
| Thunar (Xfce) | `b1d2264` | `_thunar_tree_view_model_sort()`: `g_sequence_sort()` with `thunar_tree_view_model_cmp_nodes()`, then the `new_order` array for rows-reordered | GLib 2.80 |
| Nemo (Cinnamon) | `60120b8` | `nemo_list_model_sort_file_entries()`: same pattern with `nemo_file_compare_for_sort_by_attribute_q()` | GLib 2.80 |
| Caja (MATE) | `4de76eb` | `fm_list_model_sort_file_entries()`: Nemo's code without favorites and pins | GLib 2.80 |
| Nautilus (GNOME) | `9f3a110` | GTK 4 `GtkSortListModel` over `GtkTreeListModel`, sorter `GtkTreeListRowSorter`( `GtkMultiSorter`(folders first, column) ) as built in `nautilus-view-model.c` / `nautilus-list-view.c` | GTK 4.14.5 |
| PCManFM-Qt (libfm-qt) | `8e15258` | `QSortFilterProxyModel::sort()` with `ProxyFolderModel::lessThan()` over a `FolderModel` | Qt 6.4.2 |
| KDE file dialogs (KIO) | `0b1b56b` | `QSortFilterProxyModel::sort()` with `KDirSortFilterProxyModel::subSortLessThan()` over a `KDirModel`-like model | Qt 6.4.2 |
| Krusader | `d20c6d0` | `ListModel::sort()`: `KrSort::Sorter` (`std::stable_sort`), then the item list and its three lookup hashes are rebuilt | Qt 6.4.2 |
| nnn | `d1ee163` | `qsort()` of the entry array with `entrycmp()` / `reventrycmp()` | glibc 2.39 |
| yazi | `40a9142` | `FilesSorter::sort()`: `sort_unstable_by` | Rust 1.97 (native) |
| lf | `664d7c8` | `dir.sort()`: `slices.SortStableFunc` by the column, then by folders first | Go 1.24 (native) |
| Dolphin (KDE) | `c5849f9` | `KFileItemModel::resortAllItems()` | Qt 6.4.2, see [benchmarks.md](benchmarks.md) |

- **Settings:** each project's defaults:
  - folders first;
  - names case-insensitive and "natural" (numbers by value) where the
    project does that by default;
  - Thunar, Nemo, Caja and Nautilus compare cached GLib filename collation
    keys.

  Hidden files are shown in every project, so all of them sort the same
  items. lf's default of hiding them is measured separately.
- **Bucketization side:** the C++ implementation, with:
  - one key per column;
  - folders first as a group;
  - the project's own comparison as the tie-breaker.

  Name keys are order-preserving 64-bit prefixes of what each project
  compares:
  - the first 8 bytes of the GLib collation key (GNOME family);
  - an ICU primary-strength sort key (Qt projects);
  - `strxfrm()` (nnn);
  - lowercased bytes (yazi);
  - the first token of the normalized name (lf).
- **yazi and lf** are measured twice:
  - their real sorts run natively (`benchmarks/terminal_stack/yazi_bench` in
    Rust, `lf_bench` in Go);
  - C++ ports of their comparisons feed Bucketization.

  The ports produce exactly the native programs' order (same order hash in
  every case with a unique order, and even with ties for lf). Bucketization
  would need Rust and Go ports to run inside them; the C++ figures are what
  such a port should approach.
- **Correctness:** every Bucketization order (first page in every run, whole
  order once per case) is checked against the project's own comparison:
  each element must not compare greater than the next. Ties are allowed,
  because several projects leave them to the sort algorithm (equal names in
  the flattened trees, equal sizes or times where the project has no
  tie-breaker).
- **Environment:** the same as [benchmarks.md](benchmarks.md):
  - a 4-vCPU Xeon cloud container, GCC 13.3 `-O3`;
  - locale `en_US.UTF-8`;
  - page size 64;
  - medians of 5 runs.

  The datasets are the real listings (395 to 98,000 entries) and the
  synthetic 100,000-photo camera roll. The 1,000,000-entry case is covered
  in benchmarks.md.

Sort paths that trigger UI work (GTK's rows-reordered handling, Qt views'
layout change) are measured without a view attached, so the projects' real
cost on a click is higher than shown here, never lower.

## Results by family

Every row (dataset × column × direction, all projects) is in the generated
[`bucketization_file_manager_stacks.md`](../../benchmarks/outputs/bucketization_file_manager_stacks.md);
each launch kit in [launch/projects/](launch/projects/) has its project's
table. The highlights:

**GNOME family (Thunar, Nemo, Caja, Nautilus).** Comparisons are cheap
(`strcmp` on cached GLib collation keys). Still, a click on a 100k-entry
folder costs 48–480 ms, because the whole folder is sorted and then walked
again for the `new_order` permutation. Thunar, Nemo and Caja look up each
row's new position with `g_sequence_iter_get_position()` (O(log n) per row),
and Nemo and Caja also build the old order with `g_sequence_get_iter_at_pos()`.
Nautilus's GTK 4 path is the slowest of the four with the same comparisons,
taking up to 2.3× as long as Nemo's: every comparison goes through `GtkTreeListRowSorter` → `GtkMultiSorter` →
`GtkCustomSorter`. The name key costs nothing extra: it is the first 8 bytes
of the collation key these projects already compute.

**Qt family (PCManFM-Qt, KDE file dialogs, Krusader).** Every name
comparison runs `QCollator` (ICU), so a name sort of 100,000 entries takes
0.7–1.5 s. Each project adds its own overhead:

- PCManFM-Qt fetches both names through the model's `data()` and splits them
  at every dot into new strings, on every comparison.
- The KDE file dialogs copy two `KFileItem`s per comparison and search the
  `UDSEntry` field list linearly for times.
- Krusader lowercases every name into a new `SortProps` on every sort, then
  rebuilds three lookup hashes.

Bucketization's switch stays under 2 ms. The largest first page cost
comes from flattened-tree folders where hundreds of files share a name
(`__init__.py`): they tie under the project's comparison, so they must share
a bucket.

**Terminal managers (nnn, yazi, lf).**
- **nnn:** sorts its entry array with `qsort()` and compares names with
  `strtoll()` + `strcoll()`.
- **yazi:** the fastest project measured (Rust `sort_unstable_by`, no
  allocation), yet still 12–63 ms on 100k entries. Every catch-up of its
  file list (new files while loading, size updates, a sort change) re-sorts
  everything.
- **lf:** the slowest on names (1.9–3.0 s for 100k entries). Its comparison
  lowercases and strips diacritics from both names on every call.
  Normalizing each name once per sort is 4–8× faster with the same order,
  an improvement lf can make without Bucketization.

The C++ ports of yazi's and lf's comparisons reproduce the native orders
exactly. Their own sorts take 1.1–3.2× as long as yazi's native Rust sort,
and 0.16–1.1× as long as lf's native Go sort. So a Rust port of
Bucketization should be at least as fast as the C++ figures here, and a Go
port somewhat slower.

## Findings in the projects' code

Found while extracting and checking the sort paths. Each is useful to report
on its own:

1. **Dolphin: natural sorting is not transitive** for version numbers
   (`kernel-6.10 < kernel-6.9 < kernel-6.9.1 < kernel-6.10`), so the order
   depends on the listing order. Details in [benchmarks.md](benchmarks.md).
2. **Krusader: the descending comparator is not a strict weak ordering.**
   `itemGreaterThan()` is `!itemLessThan()`. It returns true for equal items
   (`README` and `readme` with the default "ignore case") and for an item
   with itself. That is undefined behaviour in `std::stable_sort`, and
   libstdc++'s debug mode aborts on it
   (`speedup_krusader_descending_check`).
3. **lf: names are normalized on every comparison** (above). It also
   glob-matches `hiddenfiles` patterns on every comparison of the stable
   partition that hides dotfiles.
4. **KDE file dialogs: I/O inside the sort.** `KDirModel::data(ChildCountRole)`
   counts a local folder's entries with `readdir()` the first time a size
   sort compares it. It does so synchronously, from inside
   `QSortFilterProxyModel::sort()`; the result is cached afterwards. Not
   measured here: the benchmark has no real folders behind the listing.
5. **PCManFM-Qt: per-comparison string work** (above). Caching a sort key
   per file would help without Bucketization.

Two cautions on the numbers above:

- The flattened-tree datasets contain equal names (thousands of `index.js`),
  which a real folder cannot. Projects that compare only names then have
  ties. Bucketization's orders stay valid, but tied files can come out in a
  different order than the project's own sort left them. The tables mark
  these as "valid, ties ordered differently".
- `linux-tree` has no timestamps (all equal), so its "modified" rows measure
  each project's tie-breaking.

## Reproducing

```bash
sudo apt install qt6-base-dev libicu-dev libglib2.0-dev libgtk-4-dev   # stacks
# Rust (cargo) and Go toolchains for the native yazi and lf benchmarks
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --quick --only-projects
./build/speedup_krusader_descending_check     # Krusader's comparator (aborts in debug mode)
```

The runner writes `benchmarks/outputs/bucketization_{gnome,qt,terminal}_family.csv`,
`bucketization_{yazi,lf}_native.csv` and the tables in
[`bucketization_file_manager_stacks.md`](../../benchmarks/outputs/bucketization_file_manager_stacks.md).
