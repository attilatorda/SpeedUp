# Nemo (Linux Mint / Cinnamon)

| | |
|---|---|
| Where | <https://github.com/linuxmint/nemo/issues> (search first for "sort" and "slow") |
| Account | GitHub |
| License | GPL-2.0-or-later. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license |
| AI policy (2026-10-24) | none found. Disclose anyway |
| Fit | **high**. Nemo owns its sort (`nemo_list_model_sort_file_entries()`) and caches collation keys |
| First post | issue: proposal with numbers |
| Code needed | a C port (or C API) of BucketIndex; then a patch to `src/nemo-list-model.c` |

Nemo and Caja share this code (both descend from Nautilus 2's list model),
so one C port serves both. Post to each project separately, each with its
own numbers.

## Numbers

Nemo's comparison and sort path, extracted from `60120b8`, on GLib 2.80.
Each range covers name, size and modified, ascending and descending. The
re-sort excludes GtkTreeView's rows-reordered handling.

<!-- numbers:full Nemo -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.68–0.92 | 0.009–0.013 | 61–86× | 0.14–0.23 |
| usr-bin | 1,172 | 0.48–0.62 | 0.003–0.006 | 88–204× | 0.086–0.12 |
| node-modules-tree | 36,069 | 36–68 | 0.057–0.11 | 427–1,183× | 2.7–6.1 |
| usr-tree | 97,974 | 92–236 | 0.15–0.22 | 409–1,458× | 6.6–9.8 |
| linux-tree | 97,005 | 74–300 | 0.12–0.20 | 380–1,731× | 3.8–11 |
| photos-100k | 100,000 | 67–382 | 0.080–0.19 | 599–2,206× | 2.6–10 |
<!-- /numbers -->

## Day-0 post

**Title:** List view: each column click re-sorts the whole folder (36–382 ms at 36k–100k files)

> Hi! I've designed a sorting scheme for multi-column file views,
> Bucketization ([design](<DESIGN_DOC_URL>), released today:
> <RELEASE_URL>), and measured it against Nemo's own sort code.
>
> **Today:** clicking a column or changing direction runs
> `nemo_list_model_sort_file_entries()`. It walks the old order with
> `g_sequence_get_iter_at_pos()`, then runs `g_sequence_sort()` over every
> entry with `nemo_file_compare_for_sort_by_attribute_q()`, then builds
> `new_order` with `g_sequence_iter_get_position()`. That is three O(n log n)
> passes per click.
>
> **Measured** with Nemo's comparison and sort path (extracted from
> 60120b8, GLib 2.80, real folder listings, medians of 5, without the tree
> view's rows-reordered handling):
>
> <!-- numbers:post Nemo -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 0.68–0.92 ms | 0.009–0.013 ms |
> | node_modules, flattened | 36,069 | 36–68 ms | 0.057–0.11 ms |
> | /usr, flattened | 97,974 | 92–236 ms | 0.15–0.22 ms |
> | 100,000-photo camera roll | 100,000 | 67–382 ms | 0.080–0.19 ms |
> <!-- /numbers -->
>
> **How:** every file keeps a bucket per sort column. The buckets at both
> ends are the size of the visible rows. A click regroups in O(n), without
> comparisons, and sorts only what is on screen. Switching back to a column
> used before takes about a microsecond. A column's buckets are built once,
> 3–10 ms on 100k files, and the name key comes straight from the cached
> collation key. The order is exactly Nemo's comparison: folders first, the
> sort-last names starting with `.` and `#`, both directions. Every run
> checks this. Tables: <CROSS_PROJECT_URL>
>
> **Integration question:** rows-reordered needs a full permutation. I'd
> emit it with the visible rows final and the rest grouped by bucket, then
> finish the other buckets from an idle callback. Would that be acceptable?
>
> Small folders gain nothing visible. If you're interested I'd write it in
> C for Nemo's list model (the reference code is C++/Java, with a license exception
> for Nemo).
>
> Disclosure: the reference implementation and the benchmarks were written
> with an AI coding assistant, under my direction and review. The design is
> mine. Patches for Nemo I'm happy to write by hand.

## Follow-ups

1. The same C port as for Caja and Thunar.
2. A prototype in `nemo-list-model.c` for the flat list. Expanded folders
   keep the current path.
3. The grid (icon) view sorts through `nemo-icon-container`. Look at it
   after the list view.
