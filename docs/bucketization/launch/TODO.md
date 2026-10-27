# To do later

## Launch posts that need accounts

The repository went public on 2026-10-27 and the GitHub posts went first.
These need accounts that didn't exist on launch day:

- [ ] **KDE Discuss** post ([projects/kde-discuss.md](projects/kde-discuss.md)):
      a KDE Identity account (<https://invent.kde.org>)
- [ ] **Thunar** issue ([projects/thunar.md](projects/thunar.md)): a
      gitlab.xfce.org account; new accounts can need approval, so sign up a
      few days ahead
- [ ] **Nautilus** issue ([projects/nautilus.md](projects/nautilus.md)): a
      gitlab.gnome.org account

The posts say "released today"; change that to the release date
(2026-10-27) when posting later.

## URGENT: follow up on the three filed bug tickets

Filed on 2026-10-27, before the launch. They don't mention Bucketization,
and the launch plan no longer depends on them.

- [ ] **Add the links.** Put each ticket's URL into the Status column of
      [tickets/README.md](tickets/README.md) and into the project's kit:
  - [ ] Dolphin (bugs.kde.org): [projects/dolphin.md](projects/dolphin.md)
  - [ ] Krusader (bugs.kde.org): [projects/krusader.md](projects/krusader.md)
  - [ ] lf (GitHub): [projects/lf.md](projects/lf.md)
- [ ] **Answer replies.** Check each ticket for comments every few days and
      answer every question. Next step per ticket, once a maintainer is
      positive:
  - [ ] **Dolphin:** the maintainers pick a rule for dotted numbers; then
        offer a patch with test rows.
  - [ ] **Krusader:** a merge request with the one-line fix,
        `!itemLessThan(sp, sp2) && itemLessThan(sp2, sp)`, plus a test that
        sorts equal names in descending order.
  - [ ] **lf:** a PR that normalizes each name once per sort (Follow-ups in
        [projects/lf.md](projects/lf.md)); re-measure with
        [tickets/lf-sort-benchmark_test.go](tickets/lf-sort-benchmark_test.go).
- [ ] **After the launch, optional:** once the links are in, a launch-day
      post may mention a ticket where it helps (the KDE Discuss post is the
      natural place). The launch does not need it.
