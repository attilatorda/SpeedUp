# KDE Dolphin

| | |
|---|---|
| Where | bug: <https://bugs.kde.org> (product *dolphin*, component *view-engine: general*); code: <https://invent.kde.org/system/dolphin>; discussion: the KDE Discuss post in [kde-discuss.md](kde-discuss.md) |
| Account | KDE Identity (invent.kde.org) and a bugs.kde.org account (separate signup) |
| License | GPL-2.0-or-later. Bucketization under MIT fits |
| AI policy (2026-10-24) | KDE has no adopted policy. The draft under discussion (September 2026) allows AI-assisted code under human review, lets maintainers close "obvious and lazy" LLM contributions, and asks you to write commit messages and merge request descriptions yourself. **Disclose; write the bug report and MR text yourself** |
| Fit | **highest**: the largest measured gains, and the stack the design was made for |
| First post | the bug report below (useful on its own), then the KDE Discuss post |
| Step-by-step | [../../kde-contribution-plan.md](../../kde-contribution-plan.md) |

## Numbers

From [benchmarks.md](../../benchmarks.md) (Dolphin's own `KFileItemModel`
code on Qt 6.4.2 and ICU 74):

| | Dolphin today | Bucketization |
|---|---:|---:|
| Column click on real 36k–98k-entry folders | 59–671 ms | 0.08–0.72 ms |
| Click on a column used before | 59–671 ms | < 0.001 ms |
| Opening a folder (name order, sorting only) | 208–434 ms | 13–40 ms |
| 1,000,000 entries, names | 8.9 s | 4.3 ms |

## Day-0 post 1: bug report

Search first: bugs.kde.org, product dolphin, for "natural sort", "version",
"decimal" and "transitive". If a report exists, add the reproducer there
instead.

**Title:** Natural sorting is not transitive for version numbers; order depends on listing order

> **Summary**
>
> `decimalAwareNaturalCompare()` reads a two-part number as a decimal
> ("6.10" < "6.9"). It compares a two-part with a three-part number part by
> part ("6.9" < "6.9.1" < "6.10"). Together these create cycles:
>
> `kernel-6.10.tar.xz < kernel-6.9.tar.xz < kernel-6.9.1.tar.xz < kernel-6.10.tar.xz`
>
> `app-1.10.zip < app-1.5.zip < app-1.7.1.zip < app-1.10.zip`
>
> With a cyclic comparison, the merge sort's result depends on the input
> order. So the same folder can show these files in different orders, for
> example after a rename or when it is listed in another order. Measured on
> 20,000 generated multilingual names: `std::stable_sort` with Dolphin's
> own comparison and Dolphin's own merge sort disagree on 32 positions.
>
> **Steps to reproduce**
>
> 1. Create empty files `kernel-6.9.tar.xz`, `kernel-6.9.1.tar.xz` and
>    `kernel-6.10.tar.xz` in one folder.
> 2. Sort by name with natural sorting on (the default).
> 3. Rename one of them away and back, or create them in a different order,
>    and compare the order shown.
>
> A stand-alone program that prints such cycles with a copy of the
> comparison (Dolphin commit c5849f9): <CYCLES_URL>
>
> **Expected:** a consistent order. Either way of comparing version-like
> numbers is fine, as long as one rule applies to all of them.
>
> (Found while benchmarking a sorting design against Dolphin's code:
> <RELEASE_URL>. The benchmark code was written with an AI assistant; this
> report and its analysis are mine.)

Picking the rule is the maintainers' call. Offer a patch once they say which
rule they want.

## Day-0 post 2: KDE Discuss

See [kde-discuss.md](kde-discuss.md). It covers Dolphin, the file dialogs and
Krusader in one post.

## Follow-ups

Steps 2–4 of [the KDE plan](../../kde-contribution-plan.md):

1. A benchmark-only merge request: `changeSortRole` and `toggleSortOrder`
   in `src/tests/kfileitemmodelbenchmark.cpp`.
2. A discussion about lazy URL-hash and `itemsMoved` bookkeeping. That costs
   17–123 ms per re-sort by itself.
3. A prototype in `KFileItemModel` behind a build option.
