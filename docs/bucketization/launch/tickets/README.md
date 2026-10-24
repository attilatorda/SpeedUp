# Bug tickets to file now

These are bugs found in the projects' own sort code. They are written so they
can be filed **before** the launch: they do not mention Bucketization or link
to SpeedUp, and each carries its evidence inline. All were re-checked on
2026-10-24 against the projects' current master branches.

| Ticket | Project | Where to file | Kind | Status |
|---|---|---|---|---|
| [dolphin-natural-sort-cycle.md](dolphin-natural-sort-cycle.md) | Dolphin | bugs.kde.org, product dolphin | bug: non-transitive comparison (since 0d10eff3) | ready, not filed |
| [krusader-descending-comparator.md](krusader-descending-comparator.md) | Krusader | bugs.kde.org, product krusader | bug: undefined behaviour in std::stable_sort (low severity) | ready, not filed |
| [lf-normalize-per-comparison.md](lf-normalize-per-comparison.md) | lf | github.com/gokcehan/lf/issues | performance (4–8× on name sorting) | ready, not filed |

They could not be filed from the session that prepared them. bugs.kde.org is
not reachable from it, and it has no GitHub access outside the SpeedUp
repository. File them from your own accounts. Each file starts with notes for
you (where to file, what to search for first), and the ticket text follows
the `---` line.

**Not filed: the KDE file dialogs' folder counting.**
`KDirModel::data(ChildCountRole)` counts a local folder's entries with
`readdir()` the first time it is asked. A size sort asks for every folder. On
re-check this is deliberate: the code comments on the cost, caches the count,
and the count is shown in the view anyway. It was not measured either (the
benchmark has no real folders behind its listings). A ticket would need
measurements on a real disk first.

When filing, update each project's kit in [../projects/](../projects/) with the
ticket link, so the launch-day posts can refer to it.
