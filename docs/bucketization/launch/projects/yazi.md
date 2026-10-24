# yazi

| | |
|---|---|
| Where | <https://github.com/sxyazi/yazi/issues/new?template=feature.yml> (feature request). Search issues and discussions first for "sort" and "performance" |
| Account | GitHub |
| License | MIT. Bucketization under MIT fits; a Rust port would be MIT, under your copyright |
| AI policy (2026-10-24) | [Yazi AI Policy](https://github.com/yazi-rs/.github/blob/main/AI_POLICY.md):<br>• AI use is allowed but must be disclosed, **with the model used and the extent of help**;<br>• **issue, PR, discussion and commit text must be written by a human**, and must not read as AI-written;<br>• a maintainer must approve the design in an issue **before** a PR;<br>• AI-generated code must be reviewed, tested and simplified by a human;<br>• the AI tools used must not claim copyright.<br>yazi's `AGENTS.md` also forbids agents from opening issues or PRs |
| Fit | medium. yazi already sorts fast (Rust, no allocation per comparison), so the absolute gain shows only in big folders. The project cares about performance |
| First post | a feature request **you write yourself**. Below are the facts to use, not a text to paste |
| Code needed | a Rust port of BucketIndex (a crate or a module in `yazi-fs`), passing the cross-language spec |

## Numbers

yazi's `FilesSorter::sort` (`sort_unstable_by`) from `40a9142`, compiled
natively with rustc 1.97 on yazi's `File` reduced to the fields it reads
(152 bytes, moved by the sort). Defaults: alphabetical, case-insensitive,
folders first. Bucketization times come from the C++ port of the same
comparison. Its orders are identical to the native ones (same hash). The
port's own sort takes 1.1–3.2× as long as the native one, so a Rust port of
Bucketization should be at least as fast as these figures.

<!-- numbers:full yazi -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,533 | 0.12–0.57 | 0.006–0.020 | 15–38× | 0.12–0.19 |
| usr-bin | 1,172 | 0.091–0.21 | 0.004–0.008 | 14–49× | 0.092–0.15 |
| node-modules-tree | 36,069 | 6.3–12 | 0.060–0.18 | 37–175× | 1.5–4.0 |
| usr-tree | 97,974 | 18–41 | 0.16–0.26 | 74–248× | 4.3–7.0 |
| linux-tree | 97,005 | 14–44 | 0.097–0.33 | 64–255× | 3.7–12 |
| photos-100k | 100,000 | 12–63 | 0.090–0.17 | 101–425× | 2.6–7.0 |
<!-- /numbers -->

## Facts for your feature request (write it in your own words)

- **What yazi does:** `Entries::catchup_revision()` calls
  `FilesSorter::sort()` on the whole list whenever the revision changes:
  a sort setting, new files arriving while a folder loads, folder sizes
  computed. That is a full `sort_unstable_by` each time.
- **Measured:** the table above (name, size and mtime, both directions).
  - 100k-entry folders: 12–63 ms per sort today; 0.09–0.33 ms with
    Bucketization for what is on screen.
  - Small folders (around 1,500 entries): 0.1–0.6 ms today. **Nothing to
    gain there; say so.**
- **The idea:** each file keeps a bucket per sort key, with page-sized
  buckets at both ends.
  - A sort change regroups the files in O(n) and sorts only the visible
    bucket; going back to an earlier key is about a microsecond.
  - New files get a bucket when they arrive, so a growing folder never needs
    a full re-sort before the first screen is right. A terminal shows one
    page at a time, which is exactly where the design pays off.
- **Not measured yet:** the loading path, where catch-ups re-sort
  repeatedly. If maintainers are interested, measuring it is the next step.
- **Correctness:** the resulting order equals yazi's comparison (folders
  first, case-insensitive bytes, alphabetical fallback); the benchmark
  checks it on every run.
- **Links:**
  - release: <RELEASE_URL>
  - design: <DESIGN_DOC_URL>
  - benchmarks: <CROSS_PROJECT_URL>
- **Disclosure, as the policy requires:** name the model and what it did.
  For example: "The C++/Java reference implementation and the benchmark
  harness were written with an AI coding assistant (state the model name,
  e.g. from the session) under my direction; I designed the algorithm and
  reviewed the code. A Rust port for yazi I would write and review myself."
- **Ask:** would maintainers accept the design (a bucket index next to
  `Entries::items`) before you write a port? The policy requires this
  approval before any PR.

## Follow-ups

1. Only after a maintainer approves the design: a Rust port, idiomatic and
   following `AGENTS.md`'s style rules, written and reviewed by you, with
   tests from the cross-language spec.
2. Measure the loading path (many catch-ups while a 100k-entry folder
   streams in) with and without the port.
