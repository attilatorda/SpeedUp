#!/usr/bin/env python3
"""Refresh the number tables in the launch kit from benchmarks/outputs/.

Run after re-running the benchmarks (for example against newer upstream
sources before launch day):

    python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --quick
    python3 docs/bucketization/launch/update_numbers.py

Tables live between markers in the kit files and in
docs/bucketization/cross-project-benchmarks.md:

    <!-- numbers:full PROJECT -->   every folder, ranges over columns and directions
    > <!-- numbers:post PROJECT --> four folders, for the quoted post text
    <!-- numbers:summary -->        one row per project (launch README)
    <!-- numbers:lf-normalize full|post -->   lf today vs. names normalized once

PROJECT is the project name in the CSV ("Thunar", "KIO dialogs", ...).
yazi and lf use their native re-sort times and the C++ ports' Bucketization
times. Text around the tables is not changed: re-read the posts for numbers
quoted in sentences.
"""

import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
OUTPUTS = os.path.join(ROOT, "benchmarks", "outputs")
FULL_SETS = ["usr-lib", "usr-bin", "node-modules-tree", "usr-tree", "linux-tree", "photos-100k"]
POST_SETS = [("usr-lib", "/usr/lib/x86_64-linux-gnu"), ("node-modules-tree", "node_modules, flattened"),
             ("usr-tree", "/usr, flattened"), ("photos-100k", "100,000-photo camera roll")]
LARGE = ["node-modules-tree", "usr-tree", "linux-tree", "photos-100k"]
NATIVE = {"yazi": "yazi (C++ port)", "lf": "lf (C++ port)"}


def read(name):
    path = os.path.join(OUTPUTS, name)
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f.read().split("\n\n")[0].strip().splitlines()))


def load():
    rows = []
    for family in ("gnome", "qt", "terminal"):
        rows += read(f"bucketization_{family}_family.csv")
    native = {}
    for name in NATIVE:
        for r in read(f"bucketization_{name}_native.csv"):
            native[(name, r["dataset"], r["role"], r["order"])] = r
    return rows, native


def fmt(v):
    if v >= 10:
        return f"{v:,.0f}"
    if v >= 1:
        return f"{v:.1f}"
    if v >= 0.1:
        return f"{v:.2f}"
    return f"{v:.3f}"


def span(values, unit=""):
    lo, hi = min(values), max(values)
    a, b = fmt(lo), fmt(hi)
    return (a if a == b else f"{a}–{b}") + unit


def ratio_span(values):
    lo, hi = max(1, min(values)), max(1, max(values))
    return (f"{lo:,}" if lo == hi else f"{lo:,}–{hi:,}") + "×"


def cases(rows, native, project, dataset):
    """(re-sort, switch, pre-sort) per case; yazi/lf re-sort from the native run."""
    out = []
    for name, port in NATIVE.items():
        if project == name:
            project = port
            break
    else:
        name = None
    for r in rows:
        if r["project"] != project or r["dataset"] != dataset:
            continue
        resort = float(r["fm_resort_ms"])
        if name:
            n = native.get((name, dataset, r["role"], r["order"]))
            if not n:
                continue
            resort = float(n["fm_resort_ms"])
        out.append((resort, float(r["bz_switch_ms"]), float(r["bz_presort_ms"]), int(r["n"])))
    return out


def full_table(rows, native, project):
    lines = ["| folder | entries | re-sort today (ms) | Bucketization switch (ms) | speed-up | pre-sort, once per column (ms) |",
             "|---|---:|---:|---:|---:|---:|"]
    for d in FULL_SETS:
        c = cases(rows, native, project, d)
        if not c:
            continue
        ratios = [round(a / b) for a, b, _, _ in c if b > 0]
        lines.append(f"| {d} | {c[0][3]:,} | {span([x[0] for x in c])} | {span([x[1] for x in c])} | "
                     f"{ratio_span(ratios)} | {span([x[2] for x in c])} |")
    return lines


def post_table(rows, native, project):
    lines = ["| folder | files | re-sort today | with Bucketization |", "|---|---:|---:|---:|"]
    for d, label in POST_SETS:
        c = cases(rows, native, project, d)
        if c:
            lines.append(f"| {label} | {c[0][3]:,} | {span([x[0] for x in c], ' ms')} | {span([x[1] for x in c], ' ms')} |")
    return lines


def summary_table(rows, native):
    lines = ["| project | re-sort today (ms) | Bucketization (ms) | speed-up | pre-sort, once (ms) |",
             "|---|---:|---:|---:|---:|"]
    dolphin = [r for r in read("bucketization_dolphin_stack.csv") if r["dataset"] in LARGE]
    if dolphin:
        c = [(float(r["dolphin_resort_ms"]), float(r["keyed_switch_ms"]), float(r["keyed_presort_ms"])) for r in dolphin]
        ratios = [round(a / b) for a, b, _ in c if b > 0]
        lines.append(f"| Dolphin | {span([x[0] for x in c])} | {span([x[1] for x in c])} | {ratio_span(ratios)} | "
                     f"{span([x[2] for x in c])} |")
    projects = []
    for r in rows:
        name = next((n for n, port in NATIVE.items() if r["project"] == port), r["project"])
        if name not in projects:
            projects.append(name)
    for project in projects:
        c = [x for d in LARGE for x in cases(rows, native, project, d)]
        if not c:
            continue
        ratios = [round(a / b) for a, b, _, _ in c if b > 0]
        label = {"yazi": "yazi (native Rust)", "lf": "lf (native Go)"}.get(project, project)
        lines.append(f"| {label} | {span([x[0] for x in c])} | {span([x[1] for x in c])} | "
                     f"{ratio_span(ratios)} | {span([x[2] for x in c])} |")
    return lines


def lf_normalize_table(variant):
    """lf's name sort today vs. names normalized once per sort (native Go)."""
    rows = [r for r in read("bucketization_lf_native.csv") if r["role"] == "name"]
    if variant == "post":
        lines = ["| folder | files | name sort today | names normalized once |", "|---|---:|---:|---:|"]
        sets = POST_SETS
    else:
        lines = ["| folder | entries | lf today (ms) | names normalized once (ms) | faster by |", "|---|---:|---:|---:|---:|"]
        sets = [(d, d) for d in FULL_SETS]
    for d, label in sets:
        rs = [r for r in rows if r["dataset"] == d]
        if not rs:
            continue
        today = [float(r["fm_resort_ms"]) for r in rs]
        once = [float(r["fm_resort_cached_ms"]) for r in rs]
        if variant == "post":
            unit = lambda v: [x / 1000 for x in v] if max(v) >= 1000 else v
            suffix = lambda v: " s" if max(v) >= 1000 else " ms"
            lines.append(f"| {label} | {int(rs[0]['n']):,} | {span(unit(today), suffix(today))} | {span(unit(once), suffix(once))} |")
        else:
            ratio = [round(a / b) for a, b in zip(today, once)]
            lines.append(f"| {d} | {int(rs[0]['n']):,} | {span(today)} | {span(once)} | {ratio_span(ratio)} |")
    return lines


MARKER = re.compile(r"^(?P<prefix>(?:> )?)<!-- numbers:(?P<kind>full|post|summary|lf-normalize)(?: (?P<project>[^>]+?))? -->$")
END = re.compile(r"^(?:> )?<!-- /numbers -->$")


def refresh(path, rows, native):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    out, i, changed = [], 0, False
    while i < len(lines):
        m = MARKER.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        out.append(lines[i])
        j = i + 1
        while j < len(lines) and not END.match(lines[j]):
            j += 1
        if j == len(lines):
            sys.exit(f"{path}: unterminated numbers block at line {i + 1}")
        kind, project, prefix = m.group("kind"), m.group("project"), m.group("prefix")
        if kind == "full":
            table = full_table(rows, native, project)
        elif kind == "post":
            table = post_table(rows, native, project)
        elif kind == "lf-normalize":
            table = lf_normalize_table(project)
        else:
            table = summary_table(rows, native)
        if len(table) <= 2:
            sys.exit(f"{path}: no results for {project!r}")
        out += [prefix + t for t in table]
        out.append(lines[j])
        changed = True
        i = j + 1
    if changed:
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(out))
    return changed


def main():
    rows, native = load()
    if not rows:
        sys.exit("no benchmark CSVs in benchmarks/outputs: run the benchmarks first")
    files = [os.path.join(HERE, f) for f in os.listdir(HERE) if f.endswith(".md")]
    files.append(os.path.join(os.path.dirname(HERE), "cross-project-benchmarks.md"))
    files += [os.path.join(HERE, "projects", f) for f in os.listdir(os.path.join(HERE, "projects")) if f.endswith(".md")]
    for path in sorted(files):
        if refresh(path, rows, native):
            print("updated", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
