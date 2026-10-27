# Nautilus (GNOME Files)

| | |
|---|---|
| Where | <https://gitlab.gnome.org/GNOME/nautilus/-/issues> (search first for "sort performance") |
| Account | gitlab.gnome.org |
| License | GPL-3.0-or-later (sort files GPL-2.0-or-later). The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license |
| AI policy (2026-10-24) | no Nautilus policy (repository checked at `9f3a110`), and no GNOME-wide one. Several other GNOME projects (Loupe, Calendar) ban LLM-generated contributions, and GNOME Shell extensions forbid AI-generated ones. **Re-check on launch day: if Nautilus adopts a ban, it leaves the launch.** Write anything you post yourself |
| Fit | medium-high. Large gains, but GTK owns the sort (`GtkSortListModel`), so it needs a custom list model in Nautilus or a change in GTK |
| First post | issue: measurements and a design question (not a patch) |

## Numbers

Nautilus's model chain on GTK 4.14.5: `GtkSortListModel` over
`GtkTreeListModel`, with `GtkTreeListRowSorter`(`GtkMultiSorter`(folders
first, column)), as in `nautilus-view-model.c` and `nautilus-list-view.c` at
`9f3a110`. Ranges cover name, size and modified, ascending and descending.

<!-- numbers:full Nautilus -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 1.3–1.5 | 0.008–0.011 | 133–161× | 0.15–0.19 |
| usr-bin | 1,172 | 0.94–1.1 | 0.004–0.008 | 133–296× | 0.078–0.15 |
| node-modules-tree | 36,069 | 63–134 | 0.052–0.095 | 729–2,403× | 2.6–5.4 |
| usr-tree | 97,974 | 168–416 | 0.13–0.20 | 831–2,620× | 5.4–11 |
| linux-tree | 97,005 | 139–464 | 0.12–0.26 | 760–2,921× | 4.6–9.1 |
| photos-100k | 100,000 | 106–480 | 0.090–0.19 | 1,073–2,835× | 3.0–12 |
<!-- /numbers -->

Side finding: the same comparisons take up to 2.3× as long through this
sorter chain as through Nemo's `g_sequence_sort()`, because each comparison
passes through three GtkSorter layers.

## Day-0 post

Write this one in your own words; the draft gives the facts.

**Title:** Column sort cost on large folders, and a possible faster design

> I measured what a column click costs in Nautilus's view model and tried a
> different sorting design, Bucketization ([design](<DESIGN_DOC_URL>),
> released today: <RELEASE_URL>).
>
> **Today:** a header click changes the column view sorter, and
> `GtkSortListModel` re-sorts every item. The sorter chain is
> `GtkTreeListRowSorter` → `GtkMultiSorter` (folders first, column) →
> `nautilus_file_compare_for_sort()`. Measured with that chain on GTK 4.14,
> on real folder listings, medians of 5:
>
> <!-- numbers:post Nautilus -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 1.3–1.5 ms | 0.008–0.011 ms |
> | node_modules, flattened | 36,069 | 63–134 ms | 0.052–0.095 ms |
> | /usr, flattened | 97,974 | 168–416 ms | 0.13–0.20 ms |
> | 100,000-photo camera roll | 100,000 | 106–480 ms | 0.090–0.19 ms |
> <!-- /numbers -->
>
> **The idea:** every file keeps a bucket per sort column, with page-sized
> buckets at both ends. A click regroups in O(n) and sorts only the visible
> bucket, and switching back to an earlier column is about a microsecond.
> The order is exactly Nautilus's comparison, which the benchmark checks.
> Tables: <CROSS_PROJECT_URL>
>
> **Question:** where would this belong, if anywhere? GTK 4's list models
> suit it well: a `GListModel` can emit `items-changed` for just the range
> it sorted, unlike GtkTreeModel's full rows-reordered. I see two ways:
>
> - a Nautilus-side list model for the flat views;
> - a discussion with GTK about `GtkSortListModel` using sort keys this way.
>
> I'd value your view before writing any code.
>
> Disclosure: the reference implementation (C++/Java) and the benchmarks
> were written with an AI coding assistant under my direction. Any Nautilus
> code I would write by hand, following your contribution rules.

## Follow-ups

1. If Nautilus prefers GTK: open a topic on GNOME Discourse (Platform/GTK)
   with the same numbers, about `GtkSortListModel` with sort keys
   (`gtk_sorter_get_keys()`). GTK already caches keys per item.
2. If Nautilus-side: a C `GListModel` implementation backed by a C port of
   BucketIndex, used for list and grid views without tree expansion.

## Notes

- GTK has an incremental mode (`gtk_sort_list_model_set_incremental()`),
  which Nautilus does not use. It spreads the full sort over frames and
  shows a partly sorted list in the meantime. Bucketization makes the
  visible rows final at once. Mention this if someone suggests incremental
  mode.
