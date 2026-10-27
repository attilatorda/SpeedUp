#!/usr/bin/env python3
"""Run the Bucketization file-manager benchmarks and write result tables.

Steps:
  1. unpack the real listings in benchmarks/datasets/real/ and generate the
     synthetic camera rolls into BUILD/bucketization-datasets/
  2. run speedup_benchmark_dolphin_stack (KDE) and speedup_benchmark_glib_stack
     (GNOME family, shared GLib stack) if they were built
  3. run the per-project benchmarks: speedup_benchmark_gnome_family (Thunar,
     Nemo, Caja, Nautilus), speedup_benchmark_qt_family (PCManFM-Qt, KDE file
     dialogs, Krusader), speedup_benchmark_terminal_family (nnn, yazi and lf
     ports), speedup_benchmark_godot (Godot's FileSystem dock), and the native
     yazi (Rust, cargo) and lf (Go) programs
  4. write CSV files and a Markdown summary to benchmarks/outputs/

Usage:
    run_bucketization_benchmarks.py --build build [--repeats 5] [--quick] [--extra MY_FOLDER.tsv ...]
    run_bucketization_benchmarks.py --report-only      # tables from existing CSVs
    run_bucketization_benchmarks.py --only-projects    # step 3 only

--tag NAME writes every CSV and the summary with a _NAME suffix (e.g.
--tag windows_ryzen2600x), so a run on another machine keeps the reference
results. --quick skips the 1,000,000-entry dataset. --extra adds your own listings
(made with benchmarks/datasets/capture_listing.py). The per-project benchmarks
skip datasets above --max-entries (default 200,000): the 1,000,000-entry case
is covered by the Dolphin and GLib benchmarks.
"""

import argparse
import csv
import gzip
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REAL = os.path.join(ROOT, "benchmarks", "datasets", "real")
CAPTURE = os.path.join(ROOT, "benchmarks", "datasets", "capture_listing.py")
OUTPUTS = os.path.join(ROOT, "benchmarks", "outputs")
DOLPHIN_CSV = os.path.join(OUTPUTS, "bucketization_dolphin_stack.csv")
GLIB_CSV = os.path.join(OUTPUTS, "bucketization_glib_stack.csv")
REPORT = os.path.join(OUTPUTS, "bucketization_file_manager_stacks.md")
FAMILY_CSV = {
    "gnome": os.path.join(OUTPUTS, "bucketization_gnome_family.csv"),
    "qt": os.path.join(OUTPUTS, "bucketization_qt_family.csv"),
    "terminal": os.path.join(OUTPUTS, "bucketization_terminal_family.csv"),
    "godot": os.path.join(OUTPUTS, "bucketization_godot.csv"),
}
ON_HOLD = {"Godot"}  # measured, but not part of the launch (docs/bucketization/launch/README.md)
NATIVE_CSV = {
    "yazi": os.path.join(OUTPUTS, "bucketization_yazi_native.csv"),
    "lf": os.path.join(OUTPUTS, "bucketization_lf_native.csv"),
}
TERMINAL = os.path.join(ROOT, "benchmarks", "terminal_stack")
LARGE = ["node-modules-tree", "usr-tree", "linux-tree", "photos-100k"]
ORDER = ["usr-bin", "usr-lib", "node-modules-top", "node-modules-tree", "usr-tree", "linux-tree",
         "photos-100k", "photos-1m"]


def apply_tag(tag):
    """Gives every output file a _TAG suffix."""
    global DOLPHIN_CSV, GLIB_CSV, REPORT

    def tagged(path):
        stem, ext = os.path.splitext(path)
        return f"{stem}_{tag}{ext}"

    DOLPHIN_CSV, GLIB_CSV, REPORT = tagged(DOLPHIN_CSV), tagged(GLIB_CSV), tagged(REPORT)
    for table in (FAMILY_CSV, NATIVE_CSV):
        for key in table:
            table[key] = tagged(table[key])


def executable(path):
    """The path itself, or with .exe on Windows."""
    if not os.path.exists(path) and os.path.exists(path + ".exe"):
        return path + ".exe"
    return path


def prepare_datasets(build, quick):
    target = os.path.join(build, "bucketization-datasets")
    os.makedirs(target, exist_ok=True)
    files = []
    for name in ORDER[:6]:
        out = os.path.join(target, name + ".tsv")
        if not os.path.exists(out):
            with gzip.open(os.path.join(REAL, name + ".tsv.gz"), "rb") as src, open(out, "wb") as dst:
                shutil.copyfileobj(src, dst)
        files.append(out)
    for name, count in (("photos-100k", 100_000), ("photos-1m", 1_000_000)):
        if quick and count > 100_000:
            continue
        out = os.path.join(target, name + ".tsv")
        if not os.path.exists(out):
            subprocess.run([sys.executable, CAPTURE, "photos", str(count), "-o", out], check=True)
        files.append(out)
    return files


def run(binary, csv_path, repeats, datasets, extra_args=()):
    binary = executable(binary)
    if not os.path.exists(binary):
        print(f"skipping {os.path.basename(binary)} (not built)")
        return False
    cmd = [binary, "--repeats", str(repeats), "--csv", csv_path, *extra_args, *datasets]
    print("running", " ".join(os.path.basename(c) if c.startswith("/") else c for c in cmd))
    # Exit status 1 means some order failed its check; the CSV records which,
    # so the remaining benchmarks still run.
    if subprocess.run(cmd).returncode != 0:
        print(f"warning: {os.path.basename(binary)} reported invalid orders; see {os.path.relpath(csv_path, ROOT)}")
    return True


def build_native(build):
    """Builds the Rust and Go programs if their toolchains are installed."""
    built = {}
    yazi_dir = os.path.join(TERMINAL, "yazi_bench")
    target = os.path.join(build, "yazi_bench_target")
    if shutil.which("cargo"):
        subprocess.run(["cargo", "build", "--release", "--offline", "--quiet", "--target-dir", target],
                       cwd=yazi_dir, check=True)
        built["yazi"] = executable(os.path.join(target, "release", "yazi_bench"))
    else:
        print("skipping yazi_bench (cargo not found)")
    if shutil.which("go"):
        out = os.path.join(build, "lf_bench")
        try:
            subprocess.run(["go", "build", "-o", out, "."], cwd=os.path.join(TERMINAL, "lf_bench"), check=True)
            built["lf"] = executable(out)
        except subprocess.CalledProcessError:
            print("skipping lf_bench (go build failed; lf_bench/go.mod names the Go version it needs)")
    else:
        print("skipping lf_bench (go not found)")
    return built


def entry_count(path):
    with open(path, "rb") as f:
        return sum(1 for _ in f) - 1


def run_projects(build, repeats, datasets, max_entries):
    small = [d for d in datasets if entry_count(d) <= max_entries]
    limit = ["--max-entries", str(max_entries)]
    run(os.path.join(build, "speedup_benchmark_gnome_family"), FAMILY_CSV["gnome"], repeats, small, limit)
    run(os.path.join(build, "speedup_benchmark_qt_family"), FAMILY_CSV["qt"], repeats, small, limit)
    run(os.path.join(build, "speedup_benchmark_terminal_family"), FAMILY_CSV["terminal"], repeats, small, limit)
    run(os.path.join(build, "speedup_benchmark_godot"), FAMILY_CSV["godot"], repeats, small, limit)
    for name, binary in build_native(build).items():
        if name == "lf":  # Go's flag package
            cmd = [binary, "-repeats", str(repeats), "-csv", NATIVE_CSV[name], *small]
        else:
            cmd = [binary, "--repeats", str(repeats), "--csv", NATIVE_CSV[name], *small]
        print("running", os.path.basename(binary))
        subprocess.run(cmd, check=True)


def read_csv(path):
    """First table only (the Dolphin CSV has a second 'open' table after a blank line)."""
    with open(path) as f:  # universal newlines: the checkout may have CRLF
        text = f.read().split("\n\n")
    tables = [list(csv.DictReader(chunk.strip().splitlines())) for chunk in text if chunk.strip()]
    return tables


def ms(value):
    v = float(value)
    if v < 0:
        return "–"
    if v < 0.01:
        return f"{v:.4f}"
    if v < 10:
        return f"{v:.2f}"
    return f"{v:.0f}"


def speedup(slow, fast):
    s, f = float(slow), float(fast)
    if s <= 0 or f <= 0:
        return "–"
    return f"{s / f:,.0f}×"


def same_order(r):
    if r["order_ok"] == "1":
        return "yes"
    if r.get("dolphin_comparator_consistent", "1") == "0":
        return "n/a¹"
    return "NO"


def dolphin_tables(path):
    rows, opens = read_csv(path)
    out = ["### KDE Dolphin stack", "",
           "Time from changing the sort column (or direction) until the first 64 rows are in their final",
           "order. Dolphin = `KFileItemModel::resortAllItems()` (sort + URL hash + moved-item list).",
           "Bucketization = keyed mode (ICU prefix keys / numbers, Dolphin's comparison as tie-breaker),",
           "folders first as a group. Pre-sort is paid once per column, for both directions.", "",
           "| dataset | entries | column | order | Dolphin re-sort | of which sort | partial_sort | Bucketization switch | cached switch | pre-sort (once) | speed-up | same order |",
           "|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|:---:|"]
    for r in sorted(rows, key=lambda r: (ORDER.index(r["dataset"]) if r["dataset"] in ORDER else 99,)):
        out.append(f"| {r['dataset']} | {int(r['n']):,} | {r['role']} | {r['order']} | {ms(r['dolphin_resort_ms'])} | "
                   f"{ms(r['dolphin_sort_ms'])} | {ms(r['partial_sort_ms'])} | {ms(r['keyed_switch_ms'])} | "
                   f"{ms(r['keyed_cached_ms'])} | {ms(r['keyed_presort_ms'])} | "
                   f"{speedup(r['dolphin_resort_ms'], r['keyed_switch_ms'])} | {same_order(r)} |")
    if any(same_order(r) == "n/a¹" for r in rows):
        out += ["", "¹ Dolphin's comparison has cycles on this data (e.g. `kernel-6.10` < `kernel-6.9` < `kernel-6.9.1` <",
                "`kernel-6.10`), so even `std::stable_sort` with Dolphin's own comparison gives a different order than",
                "Dolphin's merge sort. No exact match is possible; see docs/bucketization/benchmarks.md."]
    out += ["", "Comparator mode (Dolphin's comparison only, no key):", "",
            "| dataset | column | order | pre-sort | switch | cached |", "|---|---|---|---:|---:|---:|"]
    for r in rows:
        if float(r["cmp_switch_ms"]) >= 0:
            out.append(f"| {r['dataset']} | {r['role']} | {r['order']} | {ms(r['cmp_presort_ms'])} | "
                       f"{ms(r['cmp_switch_ms'])} | {ms(r['cmp_cached_ms'])} |")
    out += ["", "Opening a folder (name order, until the first page is sorted):", "",
            "| dataset | entries | Dolphin insertItems | Bucketization, name key | all 3 keys pre-sorted | comparator mode |",
            "|---|---:|---:|---:|---:|---:|"]
    for o in opens:
        out.append(f"| {o['dataset']} | {int(o['n']):,} | {ms(o['dolphin_open_ms'])} | {ms(o['keyed_open_ms'])} | "
                   f"{ms(o['keyed_open_all_keys_ms'])} | {ms(o['cmp_open_ms'])} |")
    return out


def glib_tables(path):
    rows = read_csv(path)[0]
    out = ["### GNOME-family stack (GLib collation keys)", "",
           "Names compare through cached `g_utf8_collate_key_for_filename()` keys (strcmp), folders first.",
           "`g_sequence_sort` is what Thunar, Nemo and Caja run; `stable_sort` stands in for GTK 4's",
           "GtkSortListModel (Nautilus). Bucketization keys are the first 8 bytes of the same collation keys.", "",
           "| dataset | entries | column | order | collation keys (load) | g_sequence_sort | stable_sort | partial_sort | Bucketization switch | cached | pre-sort | same order |",
           "|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|:---:|"]
    for r in rows:
        out.append(f"| {r['dataset']} | {int(r['n']):,} | {r['role']} | {r['order']} | {ms(r['collate_keys_ms'])} | "
                   f"{ms(r['g_sequence_sort_ms'])} | {ms(r['stable_sort_ms'])} | {ms(r['partial_sort_ms'])} | "
                   f"{ms(r['keyed_switch_ms'])} | {ms(r['keyed_cached_ms'])} | {ms(r['keyed_presort_ms'])} | "
                   f"{'yes' if r['order_ok'] == '1' else 'NO'} |")
    return out


def flag(r):
    """Bucketization's order: valid under the project's comparison; identical or tied."""
    if r["bz_valid"] != "1":
        return "NO"
    return "yes" if r["identical"] == "1" else "yes²"


def family_table(rows, title, intro):
    out = [f"### {title}", ""] + intro + ["",
           "| project | dataset | entries | column | order | project's re-sort | partial_sort | Bucketization switch | cached | pre-sort (once) | speed-up | valid order |",
           "|---|---|---:|---|---|---:|---:|---:|---:|---:|---:|:---:|"]
    for r in sorted(rows, key=lambda r: (r["project"], ORDER.index(r["dataset"]) if r["dataset"] in ORDER else 99)):
        out.append(f"| {r['project']} | {r['dataset']} | {int(r['n']):,} | {r['role']} | {r['order']} | {ms(r['fm_resort_ms'])} | "
                   f"{ms(r['partial_sort_ms'])} | {ms(r['bz_switch_ms'])} | {ms(r['bz_cached_ms'])} | {ms(r['bz_presort_ms'])} | "
                   f"{speedup(r['fm_resort_ms'], r['bz_switch_ms'])} | {flag(r)} |")
    return out


def native_table(rows, project, port_rows):
    port = {(r["dataset"], r["role"], r["order"]): r for r in port_rows}
    has_default = "fm_resort_default_ms" in rows[0] if rows else False
    has_cached = "fm_resort_cached_ms" in rows[0] if rows else False
    head = "| dataset | entries | column | order | native re-sort | C++ port re-sort |"
    sep = "|---|---:|---|---|---:|---:|"
    if has_default:
        head += " native, lf defaults¹ |"
        sep += "---:|"
    if has_cached:
        head += " native, names normalized once⁴ |"
        sep += "---:|"
    head += " Bucketization switch (port) | speed-up vs native | same order as native³ |"
    sep += "---:|---:|:---:|"
    out = [head, sep]
    for r in sorted(rows, key=lambda r: ORDER.index(r["dataset"]) if r["dataset"] in ORDER else 99):
        p = port.get((r["dataset"], r["role"], r["order"]))
        if not p:
            continue
        same = "yes" if p["order_hash"] == r["order_hash"] else ("ties" if int(r["ties"]) else "NO")
        line = f"| {r['dataset']} | {int(r['n']):,} | {r['role']} | {r['order']} | {ms(r['fm_resort_ms'])} | {ms(p['fm_resort_ms'])} |"
        if has_default:
            line += f" {ms(r['fm_resort_default_ms'])} |"
        if has_cached:
            line += f" {ms(r['fm_resort_cached_ms'])} |"
        line += f" {ms(p['bz_switch_ms'])} | {speedup(r['fm_resort_ms'], p['bz_switch_ms'])} | {same} |"
        out.append(line)
    return out


def ranges(values, fmt):
    values = [v for v in values if v > 0]
    if not values:
        return "–"
    lo, hi = min(values), max(values)
    return fmt(lo) if fmt(lo) == fmt(hi) else f"{fmt(lo)}–{fmt(hi)}"


def summary_rows():
    """One row per project on the large real datasets (30k-100k entries)."""
    rows = []
    for path in FAMILY_CSV.values():
        if os.path.exists(path):
            rows += read_csv(path)[0]
    native = {}
    for name, path in NATIVE_CSV.items():
        if os.path.exists(path):
            native[name] = {(r["dataset"], r["role"], r["order"]): r for r in read_csv(path)[0]}
    projects = []
    for r in rows:
        if r["project"] not in projects:
            projects.append(r["project"])
    out = []
    for project in projects:
        # Timings only of orders that passed their check; the count of
        # valid orders below covers all of them.
        large = [r for r in rows if r["project"] == project and r["dataset"] in LARGE and r["bz_valid"] == "1"]
        if not large:
            continue
        name = project.split(" ")[0]
        resort = [float(r["fm_resort_ms"]) for r in large]
        label = project
        if name in native and project.endswith("(C++ port)"):
            matched = [(native[name].get((r["dataset"], r["role"], r["order"])), r) for r in large]
            resort = [float(n["fm_resort_ms"]) for n, _ in matched if n]
            label = f"{name} (native {'Rust' if name == 'yazi' else 'Go'})"
        switch = [float(r["bz_switch_ms"]) for r in large]
        ratio = [a / b for a, b in zip(resort, switch) if b > 0]
        presort = [float(r["bz_presort_ms"]) for r in large]
        valid = sum(r["bz_valid"] == "1" for r in rows if r["project"] == project)
        total = sum(1 for r in rows if r["project"] == project)
        first = [float(r["bz_presort_ms"]) + float(r["bz_switch_ms"]) for r in large]
        slower = sum(f > t for f, t in zip(first, resort))
        if project in ON_HOLD:
            label += " (on hold)"
        out.append((label, resort, switch, ratio, presort, valid, total, slower, len(first)))
    return out


def summary_table():
    def f(v):
        return f"{v:,.0f}" if v >= 10 else f"{v:.1f}" if v >= 1 else f"{v:.2f}" if v >= 0.1 else f"{v:.3f}"
    out = ["### All projects at a glance", "",
           "Real listings with 36,000–98,000 entries plus the 100,000-photo camera roll; ranges over",
           "name, size and modified, ascending and descending. Re-sort = the project's own code path",
           "for a column click; switch = Bucketization's `sort_by` + first page (64 rows), grouping not",
           "cached. Pre-sort is paid once per column (both directions).", "",
           "| project | re-sort (ms) | Bucketization switch (ms) | speed-up | pre-sort, once (ms) | first click slower | valid orders |",
           "|---|---:|---:|---:|---:|---:|---:|"]
    for label, resort, switch, ratio, presort, valid, total, slower, cases in summary_rows():
        out.append(f"| {label} | {ranges(resort, f)} | {ranges(switch, f)} | "
                   f"{ranges(ratio, lambda v: f'{max(v, 1):,.0f}')}× | {ranges(presort, f)} | {slower}/{cases} | {valid}/{total} |")
    out += ["", "First click slower: cases where pre-sort + switch (the first click on a column) took longer",
            "than the project's own re-sort. Later clicks on that column are never slower."]
    return out


def project_tables():
    out = []
    if any(os.path.exists(p) for p in list(FAMILY_CSV.values()) + list(NATIVE_CSV.values())):
        out += summary_table() + [""]
    intros = {
        "gnome": ("GNOME family: Thunar, Nemo, Caja, Nautilus",
                  ["Thunar, Nemo and Caja: their list models' `g_sequence_sort()` with their own comparisons and the",
                   "`new_order` bookkeeping for rows-reordered (the view's handling of that signal is not included).",
                   "Nautilus: GTK 4 `GtkSortListModel` over `GtkTreeListModel`, sorted by a `GtkTreeListRowSorter`",
                   "around a `GtkMultiSorter` (folders first, then the column), as in nautilus-view-model.c."]),
        "qt": ("Qt family: PCManFM-Qt, KDE file dialogs, Krusader",
               ["PCManFM-Qt and the KDE file dialogs (KIO's `KDirSortFilterProxyModel`): `QSortFilterProxyModel::sort()`",
                "with the project's `lessThan()`. Krusader: `ListModel::sort()` (KrSort::Sorter's `std::stable_sort`,",
                "then the item list and three lookup hashes are rebuilt)."]),
        "terminal": ("Terminal managers: nnn, and C++ ports of yazi and lf",
                     ["nnn: glibc `qsort()` of its entry array with `entrycmp()`. For yazi and lf the re-sort here is",
                      "the C++ port; their native times are in the next tables."]),
        "godot": ("Godot editor: FileSystem dock (on hold)",
                  ["`sort_file_info_list()` on the folder's files, which the dock rebuilds in the editor's cached",
                   "name order on every change (folders are listed separately). Name and type use comparator mode.",
                   "On hold: Godot's contribution policy forbids AI-written code and text, and the first click on a",
                   "column is often slower than Godot's own sort; see docs/bucketization/cross-project-benchmarks.md."]),
    }
    for key, path in FAMILY_CSV.items():
        if os.path.exists(path):
            title, intro = intros[key]
            out += family_table(read_csv(path)[0], title, intro) + [""]
    terminal = read_csv(FAMILY_CSV["terminal"])[0] if os.path.exists(FAMILY_CSV["terminal"]) else []
    for name, path in NATIVE_CSV.items():
        if os.path.exists(path):
            lang = "Rust" if name == "yazi" else "Go"
            out += [f"### {name}: native {lang} re-sort vs. Bucketization", ""]
            if name == "yazi":
                out += ["yazi's `FilesSorter::sort` (`sort_unstable_by`) compiled with rustc, on yazi's `File` reduced to the",
                        "fields it reads. Bucketization: the C++ port of the same comparison (a Rust port is a prerequisite",
                        "for contributing).", ""]
            else:
                out += ["lf's `dir.sort()` compiled with Go. lf normalizes both names (lowercase, strip diacritics) on",
                        "every comparison, so most of its time is allocation. Bucketization: the C++ port of the",
                        "comparison (a Go port is a prerequisite for contributing).", ""]
            out += native_table(read_csv(path)[0], name, [r for r in terminal if r["project"].startswith(name)]) + [""]
    if out:
        out += ["¹ lf's default hides dotfiles: `dir.sort()` then also runs a stable partition with a glob match",
                "per comparison, over all entries.",
                "² Valid, but tied items (equal names in the flattened-tree datasets, equal sizes or times where",
                "the project has no tie-breaker) are in a different order than the project's sort left them.",
                "Ties are allowed by the project's comparison; real folders have no equal names.",
                "³ The C++ port's own sort produced the same order as the native program (same order hash).",
                "\"ties\": equal items are ordered differently (unstable sorts), the order is otherwise the same.",
                "⁴ Not lf's code: the same `dir.sort()` with each name normalized once per sort instead of on",
                "every comparison. It produces the same order; it is an improvement lf could make on its own."]
    return out


def write_report():
    lines = ["# Bucketization vs. file manager sorting stacks", "",
             "Generated by `benchmarks/scripts/run_bucketization_benchmarks.py`. All times in milliseconds,",
             "medians. See `docs/bucketization/benchmarks.md` for the method and the analysis.", ""]
    if os.path.exists(DOLPHIN_CSV):
        lines += dolphin_tables(DOLPHIN_CSV) + [""]
    if os.path.exists(GLIB_CSV):
        lines += glib_tables(GLIB_CSV) + [""]
    lines += project_tables()
    with open(REPORT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"wrote {os.path.relpath(REPORT, ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=os.path.join(ROOT, "build"))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--only-projects", action="store_true", help="run only the per-project benchmarks")
    parser.add_argument("--max-entries", type=int, default=200_000)
    parser.add_argument("--extra", nargs="*", default=[])
    parser.add_argument("--tag", help="suffix for every output file, for runs on other machines")
    args = parser.parse_args()
    if args.tag:
        apply_tag(args.tag)

    os.makedirs(OUTPUTS, exist_ok=True)
    if not args.report_only:
        datasets = prepare_datasets(args.build, args.quick) + args.extra
        if not args.only_projects:
            run(os.path.join(args.build, "speedup_benchmark_dolphin_stack"), DOLPHIN_CSV, args.repeats, datasets)
            run(os.path.join(args.build, "speedup_benchmark_glib_stack"), GLIB_CSV, args.repeats, datasets)
        run_projects(args.build, args.repeats, datasets, args.max_entries)
    write_report()


if __name__ == "__main__":
    main()
