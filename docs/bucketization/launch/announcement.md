# Release notes and announcement

Two texts for launch day: the GitHub release notes for the SpeedUp tag (they
are the dated public record), and a short announcement for anywhere you post
it. Run `fill_placeholders.py` first. The release notes say what is new and
who designed it; keep the numbers as they are, or replace them with numbers
from a fresh run.

---

## GitHub release notes (`<RELEASE_TAG>`)

**Title:** v0.2.0: Bucketization, fast re-sorting for multi-column views

Bucketization is a sorting design for views that can be sorted by several
columns, such as a file manager's name, size and date columns. Designed by
<YOUR_NAME>.

Each element keeps a bucket per sort key. The buckets at both ends are sized to
the visible page. Switching the column or the direction then regroups the
elements in linear time and sorts only the bucket on screen. Each key's
grouping is cached, so switching back costs microseconds. Groups keep folders
first in both directions. The rest of the list is sorted on demand.

This release contains:

- the interface and reference implementations in C++ (header-only, C++17)
  and Java 17, verified against the same cross-language specification:
  [design and specification](<DESIGN_DOC_URL>)
- benchmarks against the sorting code of 11 Linux file managers and the Godot
  editor's FileSystem dock, run on real directory listings:
  [results](<CROSS_PROJECT_URL>), [method and Dolphin details](<BENCHMARKS_URL>)

Highlights, on folders of 36,000–100,000 entries (real listings and a
100,000-photo camera roll), until the visible rows are in their final order
after a column click:

| File manager stack | Full re-sort today | Bucketization |
|---|---:|---:|
| KDE Dolphin (Qt 6, ICU) | 59–713 ms | 0.08–0.72 ms |
| PCManFM-Qt, KDE file dialogs, Krusader (Qt) | 48–1,500 ms | 0.06–2.0 ms |
| Nautilus (GTK 4 `GtkSortListModel`) | 63–480 ms | 0.05–0.26 ms |
| Thunar, Nemo, Caja (GLib `g_sequence_sort`) | 36–389 ms | 0.05–0.31 ms |
| yazi (Rust), lf (Go), nnn (C) | 6–3,050 ms | 0.06–1.0 ms |

In every case Bucketization's order is sorted under the file manager's own
comparison function. Along the way the benchmarks found a non-transitive
natural sort in Dolphin and a comparator that is not a strict weak ordering in
Krusader. Both have been reported upstream.

All changes since v0.1.0 (the optimized binary search): see `CHANGELOG.md`.

<DOI_LINE>

License: AGPL-3.0, with an exception that lets Dolphin, KIO, Krusader,
Thunar, Nemo, Caja, Nautilus, GLib, GTK, PCManFM-Qt, libfm-qt, yazi, lf, nnn
and Godot use the Bucketization code under their own licenses
(`LICENSE-EXCEPTION-Bucketization.md`). Parts of the implementation were written with an AI
coding assistant; the design, the direction of the work and the review are the
author's.

---

## Short announcement

> I've published Bucketization, a sorting design for file managers and other
> multi-column views. A column click regroups the list in linear time and
> sorts only what's on screen. On 36k–100k-entry folders that takes under
> 2 ms, where today's file managers' own sort code takes 6 ms to 3 s. C++
> and Java implementations, plus benchmarks against Dolphin, Nautilus,
> Thunar, Nemo, Caja, PCManFM-Qt, the KDE file dialogs, Krusader, yazi, lf
> and nnn: <RELEASE_URL>
