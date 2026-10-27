# Bug tickets (filed)

These are bugs found in the projects' own sort code. They are written so they
can be filed **before** the launch: they do not mention Bucketization or link
to SpeedUp, and each carries its evidence inline. All were re-checked on
2026-10-24 against the projects' current master branches.

| Ticket | Project | Filed at | Kind | Status |
|---|---|---|---|---|
| [dolphin-natural-sort-cycle.md](dolphin-natural-sort-cycle.md) | Dolphin | bugs.kde.org, product dolphin | bug: non-transitive comparison (since 0d10eff3) | filed 2026-10-27 (link to add) |
| [krusader-descending-comparator.md](krusader-descending-comparator.md) | Krusader | bugs.kde.org, product krusader | bug: undefined behaviour in std::stable_sort (low severity) | filed 2026-10-27 (link to add) |
| [lf-normalize-per-comparison.md](lf-normalize-per-comparison.md) | lf | github.com/gokcehan/lf/issues, "Bug report" form | performance (4–8× on name sorting) | filed 2026-10-27 (link to add); benchmark: [lf-sort-benchmark_test.go](lf-sort-benchmark_test.go) |

All three were filed on 2026-10-27 from your own accounts. Each file keeps
the filing notes (where, what to search for first, what to pick in each
field of the form) and the text as prepared; the posted versions may differ
where you edited them.

**Not filed: the KDE file dialogs' folder counting.**
`KDirModel::data(ChildCountRole)` counts a local folder's entries with
`readdir()` the first time it is asked. A size sort asks for every folder. On
re-check this is deliberate: the code comments on the cost, caches the count,
and the count is shown in the view anyway. It was not measured either (the
benchmark has no real folders behind its listings). A ticket would need
measurements on a real disk first.

When filing, update each project's kit in [../projects/](../projects/) with the
ticket link, so the launch-day posts can refer to it.
