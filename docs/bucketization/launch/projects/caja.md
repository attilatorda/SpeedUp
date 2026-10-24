# Caja (MATE)

| | |
|---|---|
| Where | <https://github.com/mate-desktop/caja/issues> (search first for "sort") |
| Account | GitHub |
| License | GPL-2.0-or-later. Bucketization under MIT (or LGPL-2.1-or-later) fits |
| AI policy (2026-10-24) | none found (`HACKING`, web). Disclose anyway |
| Fit | **high**, same code structure as Nemo (`fm_list_model_sort_file_entries()`) |
| First post | issue: proposal with numbers |
| Code needed | the C port shared with Nemo; a patch to `src/file-manager/fm-list-model.c` |

## Numbers

Caja's comparison and sort path, extracted from `4de76eb`, on GLib 2.80.
Ranges cover name, size and modified, ascending and descending.

<!-- numbers:full Caja -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.66–0.95 | 0.009–0.011 | 63–92× | 0.16–0.22 |
| usr-bin | 1,172 | 0.48–0.59 | 0.003–0.006 | 80–184× | 0.085–0.11 |
| node-modules-tree | 36,069 | 45–63 | 0.048–0.15 | 312–1,243× | 2.5–7.0 |
| usr-tree | 97,974 | 101–226 | 0.14–0.23 | 448–1,591× | 6.1–8.2 |
| linux-tree | 97,005 | 78–308 | 0.10–0.24 | 330–1,953× | 4.3–10 |
| photos-100k | 100,000 | 65–389 | 0.096–0.21 | 677–1,885× | 2.7–10 |
<!-- /numbers -->

## Day-0 post

**Title:** Faster column sorting for large folders (45–389 ms → under 0.25 ms at 36k–100k files)

> Hello MATE team! I've designed a sorting scheme for multi-column file
> views, Bucketization ([design](<DESIGN_DOC_URL>), released today:
> <RELEASE_URL>), and benchmarked it against Caja's sort code.
>
> Today `fm_list_model_sort_file_entries()` runs on every column click and
> direction change. It runs `g_sequence_sort()` over all entries with
> `caja_file_compare_for_sort()` and then builds `new_order`
> (`g_sequence_get_iter_at_pos()` and `g_sequence_iter_get_position()` per
> row).
>
> Measured with Caja's comparison and sort path (extracted from 4de76eb,
> GLib 2.80, real listings, medians of 5, without the tree view's
> rows-reordered handling):
>
> <!-- numbers:post Caja -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 0.66–0.95 ms | 0.009–0.011 ms |
> | node_modules, flattened | 36,069 | 45–63 ms | 0.048–0.15 ms |
> | /usr, flattened | 97,974 | 101–226 ms | 0.14–0.23 ms |
> | 100,000-photo camera roll | 100,000 | 65–389 ms | 0.096–0.21 ms |
> <!-- /numbers -->
>
> Each file keeps a bucket per sort column, and the end buckets are
> page-sized. A click regroups in linear time and sorts only the visible
> bucket. Switching back is about a microsecond. Building a column's buckets
> costs 3–10 ms once, on 100k files. The result is exactly Caja's order,
> folders first in both directions, verified on every run. Details:
> <CROSS_PROJECT_URL>
>
> The design question is rows-reordered, which needs a full permutation.
> I'd emit it with the visible rows final and finish the other buckets
> when idle. Is that acceptable for Caja?
>
> Small folders won't change visibly. I'd be glad to do the C work. The
> reference implementation is C++/Java under MIT.
>
> Disclosure: the reference implementation and benchmarks were written with
> an AI coding assistant under my direction and review. The design is mine.

## Follow-ups

Same as Nemo: one C port, then the list model. The patches for Nemo and
Caja will look alike; mention the other project once you have a merge
request, not before.
