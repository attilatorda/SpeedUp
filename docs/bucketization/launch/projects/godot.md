# Godot (editor FileSystem dock): on hold

**Not posted on launch day.** Godot is benchmarked and kept in the plan, but
it fails both of the plan's drop rules (see [../README.md](../README.md#drop-rules)):

1. **Policy.** Godot's contribution rules forbid AI-written code. They also
   forbid AI-generated text in issues, pull requests and other human
   communication. The Bucketization implementation and these drafts were
   written with an AI assistant.
2. **Speed.** The first click on a column is slower than Godot's own sort in
   6 of 24 big-folder cases and in every small-folder case. Godot's file list
   is rebuilt in the editor's cached name order, so a name sort is a single
   pass. So is a type sort when a folder holds only one kind of file.
   Bucketization's one-time pre-sort costs more than that pass.

| | |
|---|---|
| Where (later) | proposals: <https://github.com/godotengine/godot-proposals> (feature proposals are not accepted on the main issue tracker) |
| License | MIT. The [Bucketization exception](../../../../LICENSE-EXCEPTION-Bucketization.md) lets it use the code under its own license |
| AI policy (checked 2026-10-24, contributing docs `5170f49`) | "Do not use AI to generate substantial pieces of code"; "Do not use AI-generated text in human-to-human communication"; "Do not use AI to generate user-facing media (e.g. documentation…)"; AI use that is allowed (for example code completion) must be disclosed. New contributors need maintainer approval before larger features |
| What runs on a sort change | `FileSystemDock::_update_file_list()` rebuilds a `List<FileInfo>` of the folder's files from `EditorFileSystem` (already in name order), calls `sort_file_info_list()` (a linked-list merge sort with a sorted-prefix check), then refills the `ItemList` |

## Numbers

Godot's sort path extracted from `c8256a8` (`benchmarks/godot_stack`).
Folders are listed separately by the dock, so only files are sorted. Types
come from the extension. The re-sort is `sort_file_info_list()` alone;
rebuilding the list and refilling the `ItemList` are not included. Name and
type use Bucketization's comparator mode (Godot's natural, case-folded
comparison has no 64-bit key here). Modified time is keyed.

<!-- numbers:full Godot -->
| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,487 | 0.10–9.2 | 0.034–0.92 | 2–40× | 1.9–34 |
| usr-bin | 1,171 | 0.056–6.2 | 0.022–0.34 | 2–20× | 0.73–18 |
| node-modules-tree | 32,181 | 11–382 | 0.089–1.2 | 97–419× | 5.7–217 |
| usr-tree | 88,676 | 42–1,052 | 0.19–3.0 | 144–598× | 11–478 |
| linux-tree | 97,005 | 4.3–1,165 | 0.15–2.0 | 13–871× | 22–589 |
| photos-100k | 100,000 | 22–177 | 0.094–0.38 | 148–486× | 1.2–1,261 |
<!-- /numbers -->

The same cases with the pre-sort counted, as the first click on a column
pays it:

<!-- numbers:first-click Godot -->
| folder | entries | re-sort today (ms) | first click: pre-sort + switch (ms) | later clicks (ms) | first click slower |
|---|---:|---:|---:|---:|---:|
| usr-lib | 1,487 | 0.10–9.2 | 2.0–34 | 0.034–0.92 | 6 of 6 |
| usr-bin | 1,171 | 0.056–6.2 | 0.76–18 | 0.022–0.34 | 6 of 6 |
| node-modules-tree | 32,181 | 11–382 | 5.8–218 | 0.089–1.2 | 0 of 6 |
| usr-tree | 88,676 | 42–1,052 | 12–480 | 0.19–3.0 | 0 of 6 |
| linux-tree | 97,005 | 4.3–1,165 | 22–590 | 0.15–2.0 | 2 of 6 |
| photos-100k | 100,000 | 22–177 | 1.3–1,261 | 0.094–0.38 | 4 of 6 |
<!-- /numbers -->

## What would have to change before contributing

- **Policy:** the code would have to be your own. That means a port of the
  design written by you from the specification, not the AI-written
  implementation. Every proposal, PR text and comment would also have to be
  written by you. Disclose any AI use the policy allows (for example
  completion). As a new contributor, ask for maintainer approval first, in a
  proposal.
- **Speed:** the first click must not be slower. Two changes would likely be
  needed:
  - order-preserving 64-bit keys for Godot's name and type comparisons
    instead of comparator mode;
  - a pre-sort that notices input already in order and builds buckets in one
    pass.

  Both are untested ideas until measured with this benchmark.
- **Benefit check:** the dock's type sort is the slow one today (up to about
  1 s at 100,000 files, because each comparison builds two new strings).
  Name and date sorts are already fast. Game project folders are usually far
  smaller than these datasets.
