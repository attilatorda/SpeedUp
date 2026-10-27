# KDE Dolphin

| | |
|---|---|
| Where | bug: <https://bugs.kde.org> (product *dolphin*, component *view-engine: general*); code: <https://invent.kde.org/system/dolphin>; discussion: the KDE Discuss post in [kde-discuss.md](kde-discuss.md) |
| Account | KDE Identity (invent.kde.org) and a bugs.kde.org account (separate signup) |
| License | GPL-2.0-or-later. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license |
| AI policy (2026-10-24) | KDE has no adopted policy. The draft under discussion (September 2026) allows AI-assisted code under human review, lets maintainers close "obvious and lazy" LLM contributions, and asks you to write commit messages and merge request descriptions yourself. **Disclose; write the bug report and MR text yourself** |
| Fit | **highest**: the largest measured gains, and the stack the design was made for |
| First post | the KDE Discuss post on launch day. The bug report was filed on 2026-10-27 and is not part of the launch ([../TODO.md](../TODO.md)) |
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

## Bug report (filed 2026-10-27, not part of the launch)

The text as prepared is in [../tickets/dolphin-natural-sort-cycle.md](../tickets/dolphin-natural-sort-cycle.md).
It doesn't mention Bucketization. Following it up is in [../TODO.md](../TODO.md). The cycle
comes from commit `0d10eff3` (2026-04-02, BUG 411707), which added the
decimal and version rules for dotted numbers. The ticket includes three
`testStringCompare` rows that all pass today, which shows the cycle in
Dolphin's own test.

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
