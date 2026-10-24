# Thunar (Xfce)

| | |
|---|---|
| Where | <https://gitlab.xfce.org/xfce/thunar/-/issues> (search first for "sort" and "large folder") |
| Account | gitlab.xfce.org. Create it a few days early: new accounts can need approval |
| License | GPL-2.0-or-later. Bucketization under MIT (or LGPL-2.1-or-later) fits |
| AI policy (2026-10-24) | none found in the repository (`HACKING`) or on the web. Disclose anyway |
| Fit | **high**. Thunar owns its sort (`_thunar_tree_view_model_sort()`) and already caches collation keys, so the name key is free |
| First post | issue: proposal with numbers and the one integration question |
| Code needed | a C port (or C API) of BucketIndex; then a patch to `thunar-tree-view-model.c` |

## Numbers

Thunar's comparison and sort path, extracted from `b1d2264`, on GLib 2.80.
Each range covers name, size and modified, ascending and descending. The
re-sort excludes GtkTreeView's own work on rows-reordered.

<!-- numbers:full Thunar -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.60–0.83 | 0.008–0.011 | 59–85× | 0.15–0.20 |
| usr-bin | 1,172 | 0.41–0.53 | 0.003–0.005 | 87–162× | 0.075–0.12 |
| node-modules-tree | 36,069 | 38–58 | 0.047–0.14 | 280–1,020× | 2.9–5.9 |
| usr-tree | 97,974 | 87–222 | 0.14–0.23 | 411–1,552× | 6.0–10 |
| linux-tree | 97,005 | 60–290 | 0.13–0.30 | 218–1,526× | 4.1–10 |
| photos-100k | 100,000 | 48–333 | 0.080–0.23 | 572–1,509× | 2.8–9.2 |
<!-- /numbers -->

## Day-0 post

**Title:** Sorting large folders: every column click re-sorts all files (38–333 ms at 36k–100k files)

> Hi! I've designed a sorting scheme for multi-column file views,
> Bucketization ([design](<DESIGN_DOC_URL>), released today:
> <RELEASE_URL>), and benchmarked it against Thunar's own sort code.
>
> **Today:** a column click or direction change runs
> `_thunar_tree_view_model_sort()`. That is `g_sequence_sort()` over every
> file with `thunar_tree_view_model_cmp_nodes()`, then `new_order` via
> `g_sequence_iter_get_position()` per row. The collation keys are cached,
> so each comparison is cheap, but every click is a full O(n log n) sort.
>
> **Measured** with Thunar's comparison and sort path (extracted from
> b1d2264, GLib 2.80, real folder listings, medians of 5, without
> GtkTreeView's rows-reordered handling):
>
> <!-- numbers:post Thunar -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 0.60–0.83 ms | 0.008–0.011 ms |
> | node_modules, flattened | 36,069 | 38–58 ms | 0.047–0.14 ms |
> | /usr, flattened | 97,974 | 87–222 ms | 0.14–0.23 ms |
> | 100,000-photo camera roll | 100,000 | 48–333 ms | 0.080–0.23 ms |
> <!-- /numbers -->
>
> **How:** every file keeps a bucket per sort column. The buckets at both
> ends are as big as the visible rows. A click regroups the files in O(n),
> without comparisons, and sorts only the bucket on screen. Going back to a
> column used before takes about a microsecond. The first time a column is
> used, its buckets are built once (3–9 ms on 100k files). The order is
> exactly Thunar's comparison, folders first in both directions; the
> benchmark checks this on every run. Full tables: <CROSS_PROJECT_URL>
>
> **The open question is GtkTreeModel's rows-reordered**, which wants the
> whole permutation. My proposal: emit it with the visible rows final and
> the rest grouped by bucket, then sort the remaining buckets from an idle
> callback (or when the view reaches them) and emit the final order. Would
> that work for Thunar's model? If not, the regrouping alone still replaces
> most of the comparison work.
>
> Small folders gain nothing visible (both stay under a millisecond). If
> this is welcome I'd do the work in C, in Thunar's style. The reference
> implementation is C++/Java under MIT, so a small C port comes first.
>
> Disclosure: the reference implementation and the benchmark harness were
> written with an AI coding assistant, under my direction and review. The
> design is mine. I'm happy to write Thunar patches by hand if you prefer.

## Follow-ups

1. If they are interested: a C port of BucketIndex, around 600 lines with
   GLib types. It must pass the cross-language spec (S1–S8 golden values).
2. A prototype in `thunar-tree-view-model.c`. Keep a BucketIndex next to
   `node->children` for the flat (non-tree) view, with keys from
   `collate_key_nocase`, size and date. Measure on a real 100k-file folder
   with `perf`.
3. Tree view (expanded subfolders) stays on `g_sequence_sort()` at first.

## Notes

- Thunar's `new_order` loop costs O(n log n) by itself
  (`g_sequence_iter_get_position()` per row). The proposal makes it O(n).
- Xfce maintainers prefer small, focused merge requests. Expect a design
  discussion first.
