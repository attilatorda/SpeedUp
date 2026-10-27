# PCManFM-Qt (LXQt, libfm-qt)

| | |
|---|---|
| Where | <https://github.com/lxqt/libfm-qt/issues> (the sort is in libfm-qt). Search first for "sort" and "slow" |
| Account | GitHub |
| License | LGPL-2.1-or-later. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license |
| AI policy (2026-10-24) | none found in the repository. Disclose anyway |
| Fit | medium-high. The gains are large: its name comparison is the most expensive one measured. But `QSortFilterProxyModel` owns the sort, so Bucketization needs a custom proxy model |
| First post | issue: numbers, plus a cheap first fix (cached sort keys) and the larger proposal |

## Numbers

`QSortFilterProxyModel::sort()` with `ProxyFolderModel::lessThan()` over a
`FolderModel`, extracted from `8e15258`, on Qt 6.4.2. Ranges cover name,
size and modified, ascending and descending.

<!-- numbers:full PCManFM-Qt -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.47–5.1 | 0.019–0.13 | 13–95× | 0.32–1.8 |
| usr-bin | 1,172 | 0.34–3.3 | 0.005–0.024 | 19–368× | 0.21–0.54 |
| node-modules-tree | 36,069 | 51–251 | 0.095–0.21 | 322–2,136× | 4.3–17 |
| usr-tree | 97,974 | 178–1,184 | 0.22–0.65 | 399–3,704× | 10–31 |
| linux-tree | 97,005 | 162–1,265 | 0.15–2.0 | 647–3,793× | 4.1–42 |
| photos-100k | 100,000 | 48–1,499 | 0.092–0.40 | 384–7,640× | 2.6–206 |
<!-- /numbers -->

A name sort of a 100k-entry folder takes over a second. Each comparison:
- fetches both names through the model's `data()` as QVariants;
- splits them at every dot into new substrings;
- compares the parts with `QCollator`.

## Day-0 post

**Title:** Name sorting is slow in large folders (about 1.4–1.5 s for 100k files); two ways to speed it up

> Hi! While benchmarking a sorting design for file views, Bucketization
> ([design](<DESIGN_DOC_URL>), released today: <RELEASE_URL>), I measured
> libfm-qt's sort path: `QSortFilterProxyModel::sort()` with
> `ProxyFolderModel::lessThan()`, extracted from 8e15258, on Qt 6.4. The
> high end of each range is the name column; size and date are several
> times faster.
>
> <!-- numbers:post PCManFM-Qt -->
> | folder | files | re-sort today | with Bucketization |
> |---|---:|---:|---:|
> | /usr/lib/x86_64-linux-gnu | 1,533 | 0.47–5.1 ms | 0.019–0.13 ms |
> | node_modules, flattened | 36,069 | 51–251 ms | 0.095–0.21 ms |
> | /usr, flattened | 97,974 | 178–1,184 ms | 0.22–0.65 ms |
> | 100,000-photo camera roll | 100,000 | 48–1,499 ms | 0.092–0.40 ms |
> <!-- /numbers -->
>
> **Cheap first step:** each name comparison fetches both names through
> `data()`, splits them at dots into new strings and compares the parts with
> QCollator. Caching a per-file sort key would remove most of that, for
> example the dot-split parts or QCollator sort keys. I can open a PR for
> this if you like.
>
> **Larger step:** Bucketization keeps a bucket per sort column for every
> file, with page-sized end buckets. A click regroups the files in O(n) and
> sorts only the visible rows; switching back to an earlier column is about
> a microsecond. It needs a proxy model that owns its order instead of
> `QSortFilterProxyModel`'s sort, so it's a bigger change. Is that something
> you'd consider? Tables: <CROSS_PROJECT_URL>
>
> Small folders gain nothing visible for the non-name columns. Name sorting
> is already 4–5 ms at 1,500 files.
>
> Disclosure: the reference implementation (C++/Java; libfm-qt may use it
> under LGPL-2.1-or-later) and the
> benchmarks were written with an AI coding assistant under my direction and
> review. The design is mine.

## Follow-ups

1. The cached-sort-key PR: small, self-contained, and it measurably helps
   without Bucketization. Re-measure with the harness
   (`--only "PCManFM-Qt"`).
2. Then, if wanted: a custom proxy model backed by BucketIndex. The C++
   implementation can be used directly.
