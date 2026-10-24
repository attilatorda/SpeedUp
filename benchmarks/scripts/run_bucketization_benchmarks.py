#!/usr/bin/env python3
"""Run the Bucketization file-manager benchmarks and write result tables.

Steps:
  1. unpack the real listings in benchmarks/datasets/real/ and generate the
     synthetic camera rolls into BUILD/bucketization-datasets/
  2. run speedup_benchmark_dolphin_stack (KDE) and speedup_benchmark_glib_stack
     (GNOME family) if they were built
  3. write CSV files and a Markdown summary to benchmarks/outputs/

Usage:
    run_bucketization_benchmarks.py --build build [--repeats 5] [--quick] [--extra MY_FOLDER.tsv ...]
    run_bucketization_benchmarks.py --report-only      # tables from existing CSVs

--quick skips the 1,000,000-entry dataset. --extra adds your own listings
(made with benchmarks/datasets/capture_listing.py).
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
ORDER = ["usr-bin", "usr-lib", "node-modules-top", "node-modules-tree", "usr-tree", "linux-tree",
         "photos-100k", "photos-1m"]


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
    if not os.path.exists(binary):
        print(f"skipping {os.path.basename(binary)} (not built)")
        return False
    cmd = [binary, "--repeats", str(repeats), "--csv", csv_path, *extra_args, *datasets]
    print("running", " ".join(os.path.basename(c) if c.startswith("/") else c for c in cmd))
    subprocess.run(cmd, check=True)
    return True


def read_csv(path):
    """First table only (the Dolphin CSV has a second 'open' table after a blank line)."""
    with open(path, newline="") as f:
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


def write_report():
    lines = ["# Bucketization vs. file manager sorting stacks", "",
             "Generated by `benchmarks/scripts/run_bucketization_benchmarks.py`. All times in milliseconds,",
             "medians. See `docs/bucketization/benchmarks.md` for the method and the analysis.", ""]
    if os.path.exists(DOLPHIN_CSV):
        lines += dolphin_tables(DOLPHIN_CSV) + [""]
    if os.path.exists(GLIB_CSV):
        lines += glib_tables(GLIB_CSV) + [""]
    with open(REPORT, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {os.path.relpath(REPORT, ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=os.path.join(ROOT, "build"))
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--extra", nargs="*", default=[])
    args = parser.parse_args()

    os.makedirs(OUTPUTS, exist_ok=True)
    if not args.report_only:
        datasets = prepare_datasets(args.build, args.quick) + args.extra
        run(os.path.join(args.build, "speedup_benchmark_dolphin_stack"), DOLPHIN_CSV, args.repeats, datasets)
        run(os.path.join(args.build, "speedup_benchmark_glib_stack"), GLIB_CSV, args.repeats, datasets)
    write_report()


if __name__ == "__main__":
    main()
