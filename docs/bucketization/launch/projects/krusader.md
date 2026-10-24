# Krusader (KDE)

| | |
|---|---|
| Where | bug: <https://bugs.kde.org> (product *krusader*); code: <https://invent.kde.org/utilities/krusader> |
| Account | KDE Identity and bugs.kde.org (same as Dolphin) |
| License | GPL-2.0-or-later. Bucketization under MIT fits |
| AI policy (2026-10-24) | as for KDE in general (see [dolphin.md](dolphin.md)). Disclose |
| Fit | high. Krusader owns its sort (`KrSort::Sorter`), and its comparison is expensive (`localeAwareCompare` on lowercased copies of the names) |
| First post | the bug report in [../tickets/](../tickets/), filed before launch. Performance goes into the KDE Discuss post |

## Numbers

`ListModel::sort()` extracted from `d20c6d0`. It creates a `SortProps` per
item (lowercasing every name), runs `std::stable_sort`, then rebuilds the
item list and three lookup hashes. Qt 6.4.2, default settings ("Krusader"
sort method, ignore case, folders first). Ranges cover name, size and
modified, ascending and descending.

<!-- numbers:full Krusader -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 1.7–2.2 | 0.014–0.031 | 56–163× | 0.31–0.57 |
| usr-bin | 1,172 | 1.4–2.0 | 0.005–0.022 | 69–428× | 0.27–0.45 |
| node-modules-tree | 36,069 | 115–210 | 0.056–0.20 | 587–3,758× | 5.9–12 |
| usr-tree | 97,974 | 415–671 | 0.19–0.54 | 888–3,492× | 11–27 |
| linux-tree | 97,005 | 415–760 | 0.13–0.28 | 1,505–4,104× | 3.4–44 |
| photos-100k | 100,000 | 282–801 | 0.074–0.25 | 2,807–4,207× | 1.9–68 |
<!-- /numbers -->

## Bug report (file before launch)

The ready-to-file text is in [../tickets/krusader-descending-comparator.md](../tickets/krusader-descending-comparator.md).
It doesn't mention Bucketization, so it can go out before the launch. The
fix it proposes is `return !itemLessThan(sp, sp2) && itemLessThan(sp2, sp);`.
The simpler `itemLessThan(sp2, sp)` breaks "always sort dirs by name"; the
ticket explains why.

## Follow-ups

1. If the fix is welcome: a merge request with the one-line change and
   a unit test for sorting equal names in descending order.
2. Performance (via the KDE Discuss post), each step a separate merge
   request:
   - `SortProps::init()` lowercases every name on every sort; cache it per
     file item;
   - then Bucketization in `ListModel::sort()`, since Krusader owns its sort.
