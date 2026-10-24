# Contributing to KDE Dolphin: plan

A step-by-step path from these benchmarks to merge requests in Dolphin,
smallest and least controversial first. Each step stands on its own, so the
work is useful even if a later step is not accepted. Numbers come from
[benchmarks.md](benchmarks.md).

## Before the first step

- **Credit.** Keep this repository public; tag a release so the design doc
  and benchmarks have a dated, citable version (a Zenodo DOI is optional).
  Add your name to the design doc and a `CITATION.cff`. In KDE, submit from
  your own invent.kde.org account so you are the commit author, and link the
  design doc in each merge request.
- **What to claim.** The building blocks are known (bucket and sample sort,
  incremental sorting, abbreviated keys as in PostgreSQL). The idea to claim
  is the design for multi-column views: every item keeps a bucket per column,
  page-sized buckets at both ends, lazy per-bucket sorting with cached
  groupings per column, and groups for folders-first in both directions.
- **License.** SpeedUp is AGPL-3.0, which KDE cannot accept. Code you bring to
  Dolphin must be GPL-2.0-or-later (or another license on KDE's accepted
  list); as the copyright holder you can offer the Bucketization code under
  such a license as well.
- **AI assistance.** This implementation was written with an AI assistant.
  Check KDE's current policy on AI-assisted contributions and disclose it.
  The Dolphin patches need Qt/KDE style anyway, so writing them yourself from
  the spec also makes the authorship clearly yours.

## Step 1: report the natural-sort cycle (bug report)

Dolphin's natural comparison is not transitive for version numbers, so their
order depends on the order the folder was listed in. The cycle comes from
`0d10eff3` (2026-04-02, BUG 411707). The ready-to-file report, re-checked on
current master, is
[launch/tickets/dolphin-natural-sort-cycle.md](launch/tickets/dolphin-natural-sort-cycle.md).
It doesn't mention Bucketization, so it can be filed before the launch. A fix
has to pick one rule for two- vs three-part numbers. That is a design
decision for the maintainers, so offer a patch only once they say which rule
they want.

## Step 2: a benchmark for changing the sort role (merge request)

Dolphin's `src/tests/kfileitemmodelbenchmark.cpp` measures inserting items,
and since 2026-09-30 (`469570fb`) also sorting with natural sorting on
(`sortManyItemsNaturally`). It does not measure re-sorting when the user
changes the sort role or order. Add
`changeSortRole` and `toggleSortOrder` cases (QBENCHMARK, 1,000 / 5,000 /
50,000 items, natural sorting, folders first). This changes no behaviour, is
easy to review, gives KDE its own baseline numbers, and introduces you to the
maintainers.

## Step 3: cheaper bookkeeping after a re-sort (discussion, then MR)

`resortAllItems()` rebuilds the URL→row hash for every item and computes the
moved-items list after the sort. On the benchmarks that bookkeeping alone
costs:

| Dataset | Entries | resortAllItems | of which sort | bookkeeping |
|---|---:|---:|---:|---:|
| node-modules-tree, all columns | 36,069 | 59–250 ms | 42–230 ms | 17–53 ms |
| usr-tree / linux-tree, all columns | ≈97,000 | 166–671 ms | 97–610 ms | 26–123 ms |
| photos-1m, modified | 1,000,000 | ≈1,280 ms | 192–233 ms | ≈1,050–1,100 ms |

`insertItems()` already rebuilds the hash lazily. Ask the maintainers whether
`resortAllItems()` could do the same, and whether a large re-sort could emit
a layout change instead of a full `itemsMoved` permutation. This helps every
sort, with or without Bucketization, and it is also what a lazy sort needs.

## Step 4: Bucketization in KFileItemModel (prototype, then proposal)

In a Dolphin fork, behind a build option:

1. Keep a `BucketIndex` next to `m_itemData`: keys per sort role, registered
   the first time a role is used; folders first (and hidden last) as groups.
2. On a role or order change, `sort_by()` and sort only the visible range;
   finish the rest with `sort_all()` from an idle timer, so features that
   need the whole order (grouping, selection ranges, "select all") keep
   working.
3. Keep the current path for tree view (expanded folders) and for folders
   under ~2,000 items, where both take a few milliseconds.
4. Name keys: QCollator does not expose sort-key bytes, so use ICU directly
   (`icu_natural_key.h`), or comparator mode if adding ICU is not acceptable
   (switching is just as fast; pre-sorting costs 2–5× more).
5. Measure with Step 2's benchmark plus `perf`/Hotspot and heaptrack on real
   folders, then post the numbers on KDE Discuss (or the Dolphin channel) and
   ask for direction before opening the merge request.

## Other file managers

If Dolphin's maintainers prefer another direction, Thunar and Nemo are the
next candidates: they own their sort, already compute collation keys (so
pre-sorting is nearly free), and the GNOME-stack benchmark shows the gains.
yazi (Rust, MIT) is a good fit for a Rust port. See
[file-manager-survey.md](file-manager-survey.md).
