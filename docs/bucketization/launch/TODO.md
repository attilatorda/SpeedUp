# To do later

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
