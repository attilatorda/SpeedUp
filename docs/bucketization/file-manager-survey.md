# How file managers sort today

A survey of where Bucketization could be contributed. For each file manager:
what happens when the user changes the sort column, how names are compared,
and how well Bucketization fits. Sources were read at the commits listed at
the end (shallow clones, 2026-10-24).

## Summary

Every file manager surveyed re-sorts **the whole folder** with a comparison
sort whenever the sort column or direction changes. None sorts lazily (only
what is on screen), and none keeps a per-column grouping for switching back.
The differences are in how expensive one comparison is:

- **KDE family** (Dolphin, Krusader, KDE file dialogs, PCManFM-Qt) compares
  names with `QCollator` on every comparison, with no cached keys. Each
  comparison is expensive, so these benefit most.
- **GNOME family** (Nautilus, Thunar, Nemo, Caja) caches a GLib collation key
  per file and compares keys with `strcmp`. Comparisons are cheap, so full
  sorts are 3–8× faster than Dolphin's, but still O(n log n) per click. For
  these, Bucketization's pre-sort costs almost nothing extra: its 64-bit key
  is the start of the collation key they already compute.
- **Terminal and cross-platform managers** (yazi, lf, nnn, Files for
  Windows) call their language's standard sort on every change.

| File manager | Language / toolkit | On sort change | Name comparison | Cached keys | License | Fit |
|---|---|---|---|---|---|---|
| **Dolphin** (KDE) | C++ / Qt 6 | `KFileItemModel::resortAllItems()`: full parallel merge sort, then rebuilds the URL→row hash and the moved-items list | `QCollator` + Dolphin's decimal-aware natural compare | no | GPL-2.0-or-later | **high**: largest measured gains |
| KDE file dialogs (KIO) | C++ / Qt 6 | `KDirSortFilterProxyModel` (QSortFilterProxyModel) re-sorts everything | `QCollator` | no | LGPL | medium: Qt's proxy model owns the sort |
| Krusader | C++ / Qt 6 | `KrSort::Sorter::sort()`: `std::stable_sort` | `QString::localeAwareCompare` per character | no | GPL-2.0-or-later | medium-high: owns its sort |
| PCManFM-Qt (libfm-qt) | C++ / Qt | `ProxyFolderModel` (QSortFilterProxyModel) | `QCollator`, numeric mode | no | LGPL-2.1 | medium: proxy model |
| **Nautilus** (GNOME Files) | C / GTK 4 | `GtkSortListModel` with a `GtkCustomSorter` re-sorts every item | cached `g_utf8_collate_key_for_filename` + strcmp | yes | GPL-3.0 (files GPL-2.0-or-later) | medium: GTK owns the sort; needs a custom `GListModel` or a GTK change |
| **Thunar** (Xfce) | C / GTK 3 | `g_sequence_sort()` on its tree-view model | cached collate keys + strcmp | yes | GPL-2.0 | **high**: owns its sort, keys already exist |
| **Nemo** (Cinnamon) | C / GTK 3 | `g_sequence_sort()` in `nemo-list-model.c` | cached collation keys | yes | GPL-2.0 | **high**: same as Thunar |
| Caja (MATE) | C / GTK 3 | `g_sequence_sort()` in `fm-list-model.c` | cached collation keys | yes | GPL-2.0 | high: same as Nemo |
| **yazi** | Rust (terminal) | `sort_unstable_by` over every entry (`yazi-fs/src/sorter.rs`) | own natural sort | no | MIT | medium-high: needs a Rust port; performance-minded project |
| lf | Go (terminal) | `slices.SortStableFunc` (`nav.go`) | natural / plain | no | MIT | medium: needs a Go port |
| nnn | C (terminal) | `qsort` with its own comparators | `xstrverscasecmp` | no | BSD-2-Clause | low-medium: tiny codebase, small folders |
| Files (Windows) | C# / WinUI | LINQ `OrderBy(…).ThenBy(…)` with `NaturalStringComparer` | natural comparer | no | MIT | medium: needs a C# port |

## Measured on real stacks

From `docs/bucketization/benchmarks.md` (real listings of 36k–98k entries):

| Stack | Full re-sort on a column click | Bucketization: switch + first page | Pre-sort per column (once) |
|---|---|---|---|
| Dolphin (Qt/ICU, its own code) | 59–671 ms | 0.08–0.72 ms | 3–50 ms |
| GNOME family (`g_sequence_sort`) | 21–203 ms | 0.06–0.27 ms | 1.5–7 ms |
| GTK 4-like (`stable_sort`) | 9–75 ms | (same) | (same) |

Bucketization produced exactly the file manager's order in every case where
that order is well defined (Dolphin's natural compare has cycles for
version-numbered names; see benchmarks.md).

## Where to contribute, in order

1. **Dolphin**: the largest gains, the stack the idea was designed for, and
   a C++ code base Bucketization already fits. Start with a benchmark-only
   merge request (`changeSortRole` in `kfileitemmodelbenchmark.cpp`), then a
   prototype behind an option. Two integration topics to discuss with the
   maintainers first: `itemsMoved` / the URL→row hash require the full order
   (they cost Dolphin 17–123 ms by themselves on the real 36k–98k folders,
   about 1.1 s at 1M entries), and tree view (expanded folders) should stay
   on the current path at first.
2. **Thunar or Nemo**: they own their sort (`g_sequence_sort`) and already
   compute collation keys, so the integration is small and the pre-sort is
   nearly free. A C port (or a C wrapper of the C++ core) is needed. Both
   are GPL-2.0, so the code must be offered under a GPL-2.0-compatible
   license.
3. **yazi**: a Rust port; the project is performance-focused, MIT-licensed
   and already has an agent-guidelines file (`AGENTS.md`), so AI-assisted
   work is not unusual there. A terminal shows one page at a time, which is
   exactly Bucketization's strong case.
4. **Krusader**: owns its `std::stable_sort`, so swapping it is local.

Nautilus and the QSortFilterProxyModel-based managers need deeper changes
(a custom list or proxy model), so they come later.

## Licensing note

The SpeedUp repository is AGPL-3.0. None of these projects can take AGPL
code, and GPL-2.0-only projects (Thunar, Nemo, Caja) cannot take GPL-3.0 or
AGPL code either. As the copyright holder you can additionally publish the
Bucketization code under a compatible license, for example LGPL-2.1-or-later
(works for all of the above) or MIT (works everywhere).

## Source snapshots

| Project | Repository | Commit |
|---|---|---|
| Dolphin | invent.kde.org/system/dolphin (GitHub mirror) | `c5849f9` |
| KIO | invent.kde.org/frameworks/kio (mirror) | `0b1b56b` |
| Krusader | invent.kde.org/utilities/krusader (mirror) | `d20c6d0` |
| libfm-qt | github.com/lxqt/libfm-qt | `8e15258` |
| Nautilus | gitlab.gnome.org/GNOME/nautilus (mirror) | `9f3a110` |
| Thunar | gitlab.xfce.org/xfce/thunar (mirror) | `b1d2264` |
| Nemo | github.com/linuxmint/nemo | `60120b8` |
| Caja | github.com/mate-desktop/caja | `4de76eb` |
| yazi | github.com/sxyazi/yazi | `40a9142` |
| lf | github.com/gokcehan/lf | `664d7c8` |
| nnn | github.com/jarun/nnn | `d1ee163` |
| Files | github.com/files-community/Files | `ee399b0` |
