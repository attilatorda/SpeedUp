# Launch-day contribution plan

This is the plan for the day Bucketization goes public on SpeedUp. That day
should leave a dated, citable public record with your name on it. Within a few
hours, every file manager where Bucketization measurably helps should hear
about it, each in its own tracker, with its own numbers. Everything below is
prepared so that launch day is mostly copy, check and post.

| File | What it is |
|---|---|
| this README | strategy, the timeline, the checklist |
| [announcement.md](announcement.md) | GitHub release notes and a short announcement |
| [projects/](projects/) | one kit per project: where to post, account, license, AI policy, the post itself, follow-ups (Godot's kit is on hold) |
| [tickets/](tickets/) | bug tickets for Dolphin, Krusader and lf, filed 2026-10-27, without mentioning Bucketization |
| [CITATION.cff.template](CITATION.cff.template) | citation metadata for the repository root |
| [fill_placeholders.py](fill_placeholders.py) | fills `<RELEASE_URL>`, `<YOUR_NAME>` and the other placeholders into ready-to-paste copies |
| [update_numbers.py](update_numbers.py) | refreshes every number table in the kits after a benchmark re-run |

Numbers come from [../cross-project-benchmarks.md](../cross-project-benchmarks.md)
(per project) and [../benchmarks.md](../benchmarks.md) (Dolphin in depth).

## What a same-day launch does and does not give you

- **Priority is the dated public release, not the posts.** The tagged GitHub
  release establishes that the design is yours and when it appeared. So do its
  archived copies: Software Heritage, with a permanent timestamped ID, and
  optionally Zenodo, with a DOI. Posting in eleven trackers on the same day
  does not add to that. It gets the projects talking to you while the release
  is new.
- **Claim the design, not the building blocks.** Bucket sort, sample sort,
  partial sorting, per-column database indexes and abbreviated keys are all
  well known. The new part is the combination for multi-column views:
  - a bucket per element for every sort key, with page-sized buckets at both
    ends;
  - lazy per-bucket sorting, with each key's grouping cached;
  - groups that keep folders first in both directions;
  - all of it behind one interface with a cross-language specification.

  Each post says this up front. Maintainers will point out prior art if a post
  doesn't, and that conversation goes better if you started it.
- **It is credit, not control.** Publishing gives you authorship credit and
  makes the design prior art. Anyone may implement the idea. Copyright covers
  your code, not the algorithm.
- **Patents:** if you might ever want one, talk to a patent attorney
  *before* the public release, and keep the repository private until then.
  - In Europe, any public disclosure before filing destroys novelty; there is
    no general grace period.
  - The US, Canada, Japan, Korea and Australia give the inventor 12 months
    after their own disclosure.

  The release, the tickets that mention Bucketization and the launch-day
  archiving (Software Heritage, Zenodo) all count as disclosure. The bug
  tickets in [tickets/](tickets/) don't describe the design, so they don't.
  For an open-source path this usually does not matter, but the release
  makes the choice for you.
- **How credit carries into each project:**
  - you author the commits from your own account, under your name;
  - every issue and merge request links the design doc;
  - the copyright notice stays in any code you contribute, which both MIT and
    LGPL require.

## Where it pays off

Summarized from [../cross-project-benchmarks.md](../cross-project-benchmarks.md).
Times are for one column click on folders of 36,000–100,000 entries (three
real folders and the 100,000-photo camera roll). The first two columns are
the time until the visible rows are final: today, and with Bucketization.
The pre-sort column is paid once per column.

<!-- numbers:summary -->
| project | re-sort today (ms) | Bucketization (ms) | speed-up | pre-sort, once (ms) |
|---|---:|---:|---:|---:|
| Dolphin | 59–713 | 0.080–0.72 | 178–3,842× | 2.1–73 |
| Thunar | 38–333 | 0.047–0.30 | 218–1,552× | 2.8–10 |
| Nemo | 36–382 | 0.057–0.22 | 380–2,206× | 2.6–11 |
| Caja | 45–389 | 0.048–0.24 | 312–1,953× | 2.5–10 |
| Nautilus | 63–480 | 0.052–0.26 | 729–2,921× | 2.6–12 |
| PCManFM-Qt | 48–1,499 | 0.092–2.0 | 322–7,640× | 2.6–206 |
| KIO dialogs | 84–1,384 | 0.079–1.2 | 356–6,647× | 2.0–159 |
| Krusader | 115–801 | 0.056–0.54 | 587–4,207× | 1.9–68 |
| nnn | 7.7–290 | 0.072–0.28 | 72–1,379× | 1.9–106 |
| yazi (native Rust) | 6.3–63 | 0.060–0.33 | 37–425× | 1.5–12 |
| lf (native Go) | 8.0–3,050 | 0.057–1.0 | 8–12,108× | 1.3–184 |
<!-- /numbers -->

Nothing changes visibly in folders under about 2,000 entries: both ways take
a few milliseconds or less, and a 60 Hz frame is 16.7 ms. Every post says
this.

## Drop rules

A project leaves the launch if either rule fails. Check both again on launch
day.

1. **Speed.** Bucketization must not make the project slower.
   - Later clicks on a column were faster in every case.
   - The first click on a column pays the pre-sort. That first click must not
     be slower than the project's own sort on folders where the
     Bucketization path is used.
   - Every integration keeps the project's current sort for folders under
     about 2,000 entries. Below that size, a few projects' first click was up
     to 0.8 ms slower (PCManFM-Qt, KIO, nnn, yazi, lf, Dolphin).
2. **AI policy.** The project's contribution policy must not forbid
   AI-assisted contributions. Policies that allow them with conditions
   (disclosure, human-written text, approval first) are fine, and the kits
   follow those conditions.

Checked on 2026-10-24 ([../cross-project-benchmarks.md](../cross-project-benchmarks.md#drop-rules-applied)):

| Project | Speed: first click slower on big folders | AI policy | In the launch |
|---|---:|---|:---:|
| Dolphin, Krusader, KDE file dialogs | 0 of 24 each | KDE: no adopted policy; the draft allows AI with human review and asks for self-written MR text | yes |
| Thunar | 0 of 24 | Xfce (Aug 2026): allowed, disclosed in every merge request and issue | yes |
| Nemo, Caja | 0 of 24 each | none published | yes |
| Nautilus | 0 of 24 | no Nautilus policy (several other GNOME apps ban LLM contributions; re-check) | yes |
| PCManFM-Qt | 0 of 24 | none published | yes |
| yazi | 0 of 24 | allowed with disclosure; human-written text; design approval before a PR | yes |
| lf, nnn | 0 of 24 each | none published | yes (nnn optional) |
| **Godot** | **6 of 24** (up to 1.1 s slower) | **forbids AI-written code and AI-written text** | **on hold** |

**Godot is on hold, not dropped.** Its benchmark and results stay in the
repository, and its kit ([projects/godot.md](projects/godot.md)) lists what
would have to change before contributing. It gets no launch-day post.

## Order of posting on launch day

In each kit, the first post is the one that is useful to the project even if
Bucketization is never merged. Order:

1. **KDE Dolphin**: bug report about the non-transitive natural sort.
2. **KDE Krusader**: bug report about the descending comparator that is not a
   strict weak ordering.
3. **lf**: issue about normalizing names on every comparison. Normalizing
   once per sort makes name sorting 4–8× faster with the same order.

   Items 1–3 were filed on 2026-10-27, before the launch. They don't mention
   Bucketization ([tickets/](tickets/)). On launch day, only link to them
   from the other posts.
4. **KDE**: one KDE Discuss post covering Dolphin, the file dialogs (KIO) and
   Krusader, with the numbers. Ask whether a benchmark merge request for
   Dolphin is welcome.
5. **Thunar**, **Nemo**, **Caja**: issues proposing Bucketization for their
   list models, each with its own numbers.
6. **Nautilus**: issue (the sort lives in GTK's `GtkSortListModel`, so this is
   a discussion first).
7. **PCManFM-Qt** (libfm-qt): issue with numbers.
8. **yazi**: feature request issue, **written by you** (yazi's AI policy
   requires human-written issue text, and a maintainer must approve the design
   before any PR).
9. **nnn**: optional, low priority (small gains on typical folders).

Godot is not in this list: it is on hold (see [Drop rules](#drop-rules)).

Space the posts out over the day. Make each one project-specific, not one
text pasted eleven times. Projects notice cross-posting, and some treat it as
spam.

## Before launch day (T-7 to T-1)

- [ ] **License for the Bucketization sources.** SpeedUp is AGPL-3.0, which
      none of these projects can take. As the copyright holder, additionally
      license `include/speedup/bucketization*`,
      `bindings/java/bucketization` and the future ports under:
  - **MIT (recommended):** it fits every project here, whether GPL-2.0-or-later,
    LGPL, MIT or BSD. It keeps your copyright notice in every copy. It also
    allows proprietary use.
  - **LGPL-2.1-or-later:** fits the GPL and LGPL projects, but yazi and lf
    (MIT) would not take it.

  Add SPDX headers and a note in the README. The kits assume this is done.
- [ ] **Accounts** (with 2FA, under the name you want credited):
  - KDE Identity, for invent.kde.org and bugs.kde.org (separate signups);
  - gitlab.gnome.org;
  - gitlab.xfce.org;
  - GitHub.
- [ ] **AI policies**, re-checked on the day; the kits record what was true on
      2026-10-24. A project whose policy now forbids AI-assisted work leaves
      the launch ([Drop rules](#drop-rules)). The implementation was written
      with an AI assistant, so for every project that stays:
  - say so in every post;
  - write post text yourself, or at least edit it into your own words;
  - follow the policy's conditions (disclosure format, human-written text,
    approval before a PR).
- [ ] **Duplicates:** search each tracker for an existing report of the same
      issue (the links are in the kits).
- [ ] **Fresh numbers:** re-run against current upstream sources if they have
      moved, at least for Dolphin and lf, whose posts quote exact behaviour:
      `python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --quick`
- [ ] **Release prepared:**
  - choose the tag and date;
  - run `fill_placeholders.py` with them;
  - copy `out/CITATION.cff` to the repository root and commit it, so it is in
    the tagged commit;
  - draft the release from `out/announcement.md`.
- [ ] **Zenodo (optional):** enable the GitHub integration for the repository
      *before* publishing the release. Zenodo only archives releases
      published after it is enabled.

## Runbook (commands)

The work lives on the branch `claude/bucketisation-sorting-interface-xjfpek`.
Merge it into `main` first (pull request), then:

```bash
# T-1: numbers and placeholders (use the tag and date you will release with)
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --quick   # optional refresh
python3 docs/bucketization/launch/update_numbers.py                               # tables in the kits
python3 docs/bucketization/launch/fill_placeholders.py \
    --name "Your Name" --repo https://github.com/attilatorda/SpeedUp --tag v0.2.0 --date 2026-11-01
cp docs/bucketization/launch/out/CITATION.cff .
git add CITATION.cff docs/bucketization && git commit -m "Add citation metadata for the Bucketization release"
git tag -a v0.2.0 -m "Bucketization"
git push origin main v0.2.0

# T0: GitHub → Releases → draft from tag v0.2.0, body = out/announcement.md (release notes part), publish
# T0 + 5 min: https://archive.softwareheritage.org/save/ → the repository URL → note the SWHID
# T0 + 10 min, if you have a DOI or SWHID:
python3 docs/bucketization/launch/fill_placeholders.py --name "Your Name" \
    --repo https://github.com/attilatorda/SpeedUp --tag v0.2.0 --date 2026-11-01 \
    --doi 10.5281/zenodo.NNNNNNN --swh swh:1:rev:...
# then post from docs/bucketization/launch/out/projects/*.md in the order above
```

`fill_placeholders.py` exits with an error and a list if any placeholder is left
unfilled. The `out/` folder is git-ignored.

## Launch day (T0)

| When | Step |
|---|---|
| T0 | Make the repository public (if it is not), push the release commit, publish the GitHub release `<RELEASE_TAG>` |
| T0 + 5 min | Save the repository on Software Heritage ("Save code now" at archive.softwareheritage.org); note the SWHID. Zenodo mints the DOI by itself if enabled |
| T0 + 10 min | Re-run `fill_placeholders.py` with `--doi`/`--swh` if you have them |
| T0 + 30 min | Posts 1–3 (the two bug reports, the lf issue) |
| T0 + 2 h | Post 4 (KDE Discuss) |
| T0 + 3–5 h | Posts 5–8 |
| T0 + 1 day | Answer every reply. Nothing else is due |

## After launch day

- **Week 1:** answer questions and correct any numbers people dispute, by
  re-running with their settings. Where a maintainer is positive, open the
  small first merge request named in that kit: a benchmark, a bug fix, or the
  lf normalization change.
- **Weeks 2–6:** the ports the integrations need: a C API (or a C port) for
  the GNOME family and nnn, a Rust crate for yazi, a Go package for lf. Each
  port must pass the cross-language specification (S1–S8 golden values) like
  the C++ and Java implementations.
- **Then:** a prototype behind an option in the project that responded best
  (Dolphin: [../kde-contribution-plan.md](../kde-contribution-plan.md),
  step 4), measured with the project's own tooling, then a merge request.

## Disclosure text

Use in every post (adapt the wording):

> Disclosure: the reference implementation and the benchmark harness were
> written with an AI coding assistant, under my direction and review; the
> design is mine. Any patch to [project] I would write to your coding style and
> review standards, and I'm happy to write it by hand if you prefer.
