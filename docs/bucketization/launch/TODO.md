# To do later

Every step that has to be done by hand, from your own accounts. The
repository went public on 2026-10-27 and `main` holds v0.2.0. Work through the
sections in order; within a section, top to bottom.

The filled-in posts come from:

```bash
python3 docs/bucketization/launch/fill_placeholders.py \
    --name "Attila Torda" --repo https://github.com/attilatorda/SpeedUp --date 2026-10-27
```

which writes them to `docs/bucketization/launch/out/` (git-ignored).

## 1. Release (GitHub account only, before any post)

- [ ] **Zenodo, optional:** only if you want a DOI. Turn on the GitHub
      integration at <https://zenodo.org/account/settings/github/> for the
      repository *before* the next step; Zenodo only archives releases
      published after that.
- [ ] **Release `v0.2.0`:** github.com/attilatorda/SpeedUp → **Releases →
      Draft a new release**:
  - tag: type `v0.2.0` ("Create new tag on publish"), target `main`;
  - title: `v0.2.0: Bucketization, fast re-sorting for multi-column views`;
  - body: the "GitHub release notes" section of `out/announcement.md`;
  - publish.

  Until this exists, the design and benchmark links in the posts return 404.
- [ ] **Tag `v0.1.0`:** draft another release:
  - tag: type `v0.1.0`, target: commit `a6e6d06` ("Phase 3A…") under
    "Recent commits";
  - title: `v0.1.0: optimized binary search`;
  - body: the 0.1.0 section of [CHANGELOG.md](../../../CHANGELOG.md);
  - publish. The changelog's comparison link needs it.
- [ ] **Check:** <https://github.com/attilatorda/SpeedUp/blob/v0.2.0/docs/bucketization/README.md>
      opens.

## 2. Archive (no account)

- [ ] **Software Heritage:** <https://archive.softwareheritage.org/save/>,
      origin type `git`, URL `https://github.com/attilatorda/SpeedUp`, submit.
      When it finishes (minutes to hours), note the SWHID of the `v0.2.0`
      revision.
- [ ] **If you have a SWHID or DOI:** re-run `fill_placeholders.py` with
      `--swh swh:1:rev:...` and/or `--doi 10.5281/zenodo.NNNNNNN`, and add the
      line to the release notes (edit the release).

## 3. Posts on GitHub (GitHub account only)

Before each one: re-check the project's AI policy (the kit records what was
true on 2026-10-24) and search its tracker for duplicates (the kit says what
to search for). Space them out over a day or two; edit each into your own
words. If you post after 2026-10-27, change "released today" to "released on
2026-10-27".

- [ ] **Nemo:** <https://github.com/linuxmint/nemo/issues>,
      [projects/nemo.md](projects/nemo.md)
- [ ] **Caja:** <https://github.com/mate-desktop/caja/issues>,
      [projects/caja.md](projects/caja.md)
- [ ] **PCManFM-Qt:** <https://github.com/lxqt/libfm-qt/issues> (the sort is
      in libfm-qt), [projects/pcmanfm-qt.md](projects/pcmanfm-qt.md)
- [ ] **yazi:** <https://github.com/sxyazi/yazi/issues/new?template=feature.yml>,
      [projects/yazi.md](projects/yazi.md). **Write it yourself**; yazi's AI
      policy requires human-written issue text and the model used to be named
- [ ] **nnn, optional:** <https://github.com/jarun/nnn/discussions>,
      [projects/nnn.md](projects/nnn.md)

## 4. Posts that need new accounts

- [ ] **Sign up** (with 2FA, under the name you want credited):
  - [ ] KDE Identity: <https://invent.kde.org> (used by KDE Discuss)
  - [ ] gitlab.xfce.org: new accounts can need approval, so sign up a few
        days before posting
  - [ ] gitlab.gnome.org
- [ ] **KDE Discuss:** <https://discuss.kde.org>, category *Development*,
      [projects/kde-discuss.md](projects/kde-discuss.md). Write it yourself
      (KDE's draft AI guidance asks for self-written text)
- [ ] **Thunar:** <https://gitlab.xfce.org/xfce/thunar/-/issues>,
      [projects/thunar.md](projects/thunar.md)
- [ ] **Nautilus:** <https://gitlab.gnome.org/GNOME/nautilus/-/issues>,
      [projects/nautilus.md](projects/nautilus.md)

## 5. URGENT: follow up on the three filed bug tickets

Filed on 2026-10-27, before the launch. They don't mention Bucketization,
and the launch does not depend on them.

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
        sorts equal names in descending order (needs a KDE Identity account
        for invent.kde.org).
  - [ ] **lf:** a PR that normalizes each name once per sort (Follow-ups in
        [projects/lf.md](projects/lf.md)); re-measure with
        [tickets/lf-sort-benchmark_test.go](tickets/lf-sort-benchmark_test.go).
- [ ] **Optional:** once the links are in, the KDE Discuss post may mention the
      Dolphin and Krusader tickets.

## 6. After the posts

- [ ] **Answer every reply** within a day or two. Where numbers are disputed,
      re-run the benchmark with the other person's settings
      (`benchmarks/scripts/run_bucketization_benchmarks.py`).
- [ ] **Where a maintainer is positive,** open the small first merge request
      named in that project's kit (a benchmark, a bug fix, or the lf change).
- [ ] **Later:** the ports the integrations need (a C port for the GNOME family
      and nnn, a Rust crate for yazi, a Go package for lf), each checked
      against the specification's golden values. See "After launch day" in
      [README.md](README.md).
