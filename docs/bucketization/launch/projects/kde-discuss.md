# KDE: one post for Dolphin, the file dialogs and Krusader

| | |
|---|---|
| Where | <https://discuss.kde.org>, category *Development* (or the Dolphin Matrix room #dolphin:kde.org for a lighter first contact) |
| Account | KDE Identity |
| License | Dolphin and Krusader GPL-2.0-or-later; KIO's `kdirsortfilterproxymodel.cpp` LGPL-2.0-only. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) covers all three |
| AI policy (2026-10-24) | no adopted KDE-wide policy; the draft allows AI-assisted code under human review and asks for self-written commit messages and MR descriptions. **Write this post yourself; the draft is the facts** |
| When | first launch-day post (T0 + 30 min). It doesn't depend on the bug tickets |

## Numbers

Each project's own sort code, on real folder listings, until the visible
rows are final after a column click:

| | Today | Bucketization | Pre-sort, once per column |
|---|---:|---:|---:|
| Dolphin (`KFileItemModel::resortAllItems()`), 36k–98k entries | 59–671 ms | 0.08–0.72 ms | 3–50 ms |
| File dialogs (`KDirSortFilterProxyModel`), 36k–100k entries | 84–1,380 ms | 0.08–1.2 ms | 2.0–159 ms |
| Krusader (`ListModel::sort()`), 36k–100k entries | 115–801 ms | 0.06–0.54 ms | 1.9–68 ms |
| Any of them, 1.2k–1.5k entries | 0.5–4.5 ms | 0.003–0.048 ms | 0.18–0.82 ms |

## Draft (write it in your own words)

**Title:** Faster sorting for Dolphin, the file dialogs and Krusader: measurements and a design

> Hi all! I've designed a sorting scheme for multi-column views,
> Bucketization ([design](<DESIGN_DOC_URL>), released today:
> <RELEASE_URL>). I benchmarked it against the sort code of Dolphin, KIO's
> file dialog proxy model and Krusader, on real folder listings.
>
> [the table above]
>
> **How:** every item keeps a bucket per sort column. The buckets at both
> ends are as big as the visible rows. A column click regroups the items in
> O(n), without comparisons, and sorts only the bucket on screen; the rest
> sorts on demand or when idle. Each column's grouping is cached, so
> switching back is about a microsecond. Folders stay first in both
> directions. Every resulting order is checked against the project's own
> comparison (Dolphin: exact match wherever its comparison is consistent).
>
> **Found on the way:**
> - `KDirModel::data(ChildCountRole)` counts a local folder's entries with
>   `readdir()` the first time a size sort compares it, synchronously inside
>   the sort;
> - Dolphin's `resortAllItems()` spends 17–123 ms on bookkeeping (URL hash,
>   moved-items list) after the sort.
>
> **Questions:**
> 1. Would a benchmark-only MR for Dolphin (`changeSortRole` /
>    `toggleSortOrder` in `kfileitemmodelbenchmark.cpp`) be welcome as a
>    first step?
> 2. Is a prototype in `KFileItemModel`, behind a build option, worth
>    doing? Tree view and small folders would keep the current path.
> 3. Is there interest for the file dialogs? `QSortFilterProxyModel` owns
>    the sort there, so it would need a custom proxy model.
>
> Disclosure: the reference implementation (C++/Java) and the benchmark
> harness were written with an AI coding assistant, under my direction and
> review. The design is mine. Patches for KDE I'd write in KDE style and am
> happy to write by hand.
