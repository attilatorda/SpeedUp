#!/usr/bin/env python3
"""Turns the sorting benchmark CSVs into a results page and a summary.

Reads benchmarks/outputs/sort_<machine>_<compiler>.csv (written by
speedup_bench_sort) and writes:
  benchmarks/outputs/sort_benchmarks.html   charts with hover readouts and tables, self-contained
  docs/sorting/results.md                   the same numbers as Markdown tables

Usage:
    sort_report.py [--machine ryzen2600x] [--cpu "AMD Ryzen 5 2600X"]
"""

import argparse
import html
import csv
import json
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ROOT / "benchmarks" / "outputs"
HTML_OUT = OUTPUTS / "sort_benchmarks.html"
MD_OUT = ROOT / "docs" / "sorting" / "results.md"

# Fixed colour slot per algorithm (dataviz reference palette); a colour
# follows its algorithm on every chart.
SLOTS = {"std::sort": 1, "name_sort": 2, "msd_radix": 3, "radix": 4, "bucket": 5, "bitonic": 6,
         "std::stable_sort": 7, "qsort": 8}
NUMERIC_ORDER = ["std::sort", "radix", "bucket", "bitonic", "std::stable_sort", "qsort"]
NAMES_ORDER = ["std::sort", "name_sort", "msd_radix"]
DISTRIBUTIONS = ["uniform", "skewed", "sorted", "few-unique"]
TYPES = ["u32", "u64", "i64", "f64"]
MACHINE = "ryzen2600x"
LABELS = {"radix": "radix sort", "bucket": "bucket sort", "bitonic": "bitonic sort", "name_sort": "name sort",
          "msd_radix": "MSD radix sort"}


# Real name lists (benchmarks/datasets/census_name_lists.py): file key -> description
REAL_LISTS = [
    ("census-shuffled", "US Census 2010 surnames, shuffled"),
    ("census-rank", "US Census 2010 surnames, in popularity order (as published)"),
    ("census-sorted", "US Census 2010 surnames, already sorted"),
    ("census-reversed", "US Census 2010 surnames, reverse sorted"),
    ("census-upper-shuffled", "US Census 2010 surnames, ALL CAPS as the Census writes them, shuffled"),
    ("intl-1m", "multilingual names (capture_listing.py intl: 47 words in 15 scripts, 79 % non-ASCII)"),
]

REAL_NOTE = ("Every list was also sorted in caseless order and checked against qsort (all correct). "
             "On the Census surnames name sort is 1.6-3.2x faster than std::sort but 1-39 % slower "
             "than plain MSD radix sort: the 162,253 short, unique surnames are just above the 131,072-name "
             "planning threshold, which was tuned on longer 'Surname Firstname' strings, and MSD radix "
             "sort separates short names in few passes. On the 1M multilingual list the plan pays again.")


def load_real(machine):
    """Per real list: the timings at the list's full size."""
    out = []
    for key, text in REAL_LISTS:
        for path in sorted(OUTPUTS.glob(f"namesort_real_{key}_{machine}_*.csv")):
            with open(path, newline="") as f:
                rows = list(csv.DictReader(f))
            n = max(int(r["n"]) for r in rows)
            full = {r["algorithm"]: r for r in rows if int(r["n"]) == n}
            out.append({"text": text, "compiler": rows[0]["compiler"], "n": n,
                        "ns": {a: float(r["ns_per_element"]) for a, r in full.items()},
                        "ok": all(r["ok"] == "1" for r in rows),
                        "buckets": int(full["name_sort"]["buckets"] or 0)})
    return out


def real_table(real):
    """Header and rows of the real-list table, shared by Markdown and HTML."""
    header = ["list", "names", "std::sort", "MSD radix sort", "name sort", "vs std::sort", "vs MSD radix", "buckets"]
    rows = []
    for r in real:
        ns = r["ns"]
        rows.append([r["text"] + ("" if r["ok"] else " (WRONG)"), f"{r['n']:,}", f"{ns['std::sort']:.1f}",
                     f"{ns['msd_radix']:.1f}", f"{ns['name_sort']:.1f}",
                     f"{ns['std::sort'] / ns['name_sort']:.2f}×", f"{ns['msd_radix'] / ns['name_sort']:.2f}×",
                     f"{r['buckets']:,}" if r["buckets"] else "– (no plan)"])
    return header, rows


# 0.3.1: bitonic hybrids and the GPU, one run (sort031_<machine>_gcc.csv).
# Colours: seven contiguous palette slots in palette order (validated as a
# set); std::sort, radix, bucket and bitonic keep their page-wide colours.
SLOTS031 = {"std::sort": 1, "bitonic_gpu": 2, "bitonic_merge": 3, "radix": 4, "bucket": 5, "bitonic": 6,
            "bitonic_bucket": 7}
ORDER031 = ["std::sort", "bitonic_gpu", "bitonic_merge", "radix", "bucket", "bitonic", "bitonic_bucket"]
LABELS.update({"bitonic_merge": "bitonic merge sort", "bitonic_bucket": "bitonic bucket sort",
               "bitonic_gpu": "bitonic sort, GPU"})


def load_031(machine):
    path = OUTPUTS / f"sort031_{machine}_gcc.csv"
    if not path.exists():
        return []
    out = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("ns_per_element"):
                continue
            out.append({"type": r["type"], "dist": r["distribution"], "n": int(r["n"]), "algo": r["algorithm"],
                        "ns": float(r["ns_per_element"]), "ok": r["ok"] == "1"})
    return out


def load(machine):
    rows = []
    for path in sorted(OUTPUTS.glob(f"sort_{machine}_*.csv")):
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                rows.append({
                    "compiler": r["compiler"], "kind": r["kind"], "type": r["type"],
                    "dist": r["distribution"], "n": int(r["n"]), "algo": r["algorithm"],
                    "ns": float(r["ns_per_element"]), "ok": r["ok"] == "1",
                    "buckets": int(r["buckets"]) if r["buckets"] else None,
                    "max_bucket": int(r["max_bucket"]) if r["max_bucket"] else None,
                    "sd": float(r["stddev_bucket"]) if r["stddev_bucket"] else None,
                })
    if not rows:
        raise SystemExit(f"no benchmarks/outputs/sort_{machine}_*.csv found")
    return rows


def fmt_n(n):
    for unit, size in (("M", 1 << 20), ("k", 1 << 10)):
        if n >= size and n % size == 0:
            return f"{n // size}{'Mi' if unit == 'M' else 'Ki'}"
    return f"{n:,}"


# ------------------------------------------------------------------ markdown

def markdown(rows, cpu):
    compilers = sorted({r["compiler"] for r in rows})
    out = [
        "# Sorting results",
        "",
        f"Generated by `benchmarks/scripts/sort_report.py` on {date.today().isoformat()} from "
        "`benchmarks/outputs/sort_*.csv`. Interactive charts: `benchmarks/outputs/sort_benchmarks.html`.",
        "",
        f"Machine: {cpu}, Windows 10. Compilers: {', '.join(compilers)} (Release). "
        "Median of 5 runs; ns per element, lower is better. Every result was checked "
        "against `std::sort` before timing"
        + (" (all correct)." if all(r["ok"] for r in rows) else " (**some were wrong: see the `ok` column of the CSV**)."),
        "Run-to-run noise on this machine is about 10–15 %.",
        "",
    ]
    for comp in compilers:
        cr = [r for r in rows if r["compiler"] == comp]
        num = [r for r in cr if r["kind"] == "numeric"]
        if num:
            nmax = max(r["n"] for r in num)
            out += [f"## {comp}: numeric sorts", ""]
            out += [f"Speed-up over `std::sort` at n = {fmt_n(nmax)} (bitonic sort: at its largest size, "
                    f"{fmt_n(max(r['n'] for r in num if r['algo'] == 'bitonic'))}):", ""]
            algos = [a for a in ["radix", "bucket", "bitonic", "std::stable_sort", "qsort"]
                     if any(r["algo"] == a for r in num)]
            out.append("| type | distribution | std::sort ns | " + " | ".join(LABELS.get(a, a) for a in algos) + " |")
            out.append("|---|---|---:|" + "---:|" * len(algos))
            for t in TYPES:
                for d in DISTRIBUTIONS:
                    sel = {(r["algo"], r["n"]): r["ns"] for r in num if r["type"] == t and r["dist"] == d}
                    if not sel:
                        continue
                    base = sel.get(("std::sort", nmax))
                    cells = []
                    for a in algos:
                        sizes = [n for (al, n) in sel if al == a]
                        if not sizes:
                            cells.append("–")
                            continue
                        m = max(sizes)
                        b = sel.get(("std::sort", m))
                        cells.append(f"{b / sel[(a, m)]:.2f}×" if b else "–")
                    out.append(f"| {t} | {d} | {base:.2f} | " + " | ".join(cells) + " |")
            out += ["", "ns per element by size (uniform data):", ""]
            sizes = sorted({r["n"] for r in num})
            pick = [n for n in sizes if n in (1 << 10, 1 << 14, 1 << 17, 1 << 20, 1 << 22, 1 << 24)]
            for t in TYPES:
                out += [f"**{t}**", "", "| algorithm | " + " | ".join(fmt_n(n) for n in pick) + " |",
                        "|---|" + "---:|" * len(pick)]
                for a in NUMERIC_ORDER:
                    vals = {r["n"]: r["ns"] for r in num if r["type"] == t and r["dist"] == "uniform" and r["algo"] == a}
                    if vals:
                        out.append(f"| {LABELS.get(a, a)} | " + " | ".join(f"{vals[n]:.2f}" if n in vals else "–"
                                                                          for n in pick) + " |")
                out.append("")
        names = [r for r in cr if r["kind"] == "names"]
        if names:
            out += [f"## {comp}: name sort", "",
                    "Synthetic \"Surname Firstname\" lists drawn by US Census frequencies "
                    "(`benchmarks/datasets/capture_names.py`), ns per name. Name sort plans buckets "
                    "from 131,072 names on; below that it is its MSD radix sort alone.", "",
                    "| names | std::sort | MSD radix sort | name sort | vs std::sort | buckets | largest | sd |",
                    "|---:|---:|---:|---:|---:|---:|---:|---:|"]
            for n in sorted({r["n"] for r in names}):
                v = {r["algo"]: r for r in names if r["n"] == n}
                ns = v["name_sort"]
                b = f"{ns['buckets']:,}" if ns["buckets"] else "–"
                mx = f"{ns['max_bucket']:,}" if ns["buckets"] else "–"
                sd = f"{ns['sd']:.0f}" if ns["buckets"] else "–"
                out.append(f"| {n:,} | {v['std::sort']['ns']:.1f} | {v['msd_radix']['ns']:.1f} | {ns['ns']:.1f} | "
                           f"{v['std::sort']['ns'] / ns['ns']:.2f}× | {b} | {mx} | {sd} |")
            out.append("")
    r031 = load_031(MACHINE)
    if r031:
        by = {(r["type"], r["dist"], r["n"], r["algo"]): r["ns"] for r in r031}
        sizes = [1 << 16, 1 << 20, 1 << 24]
        algos = ["radix", "bitonic_merge", "bitonic_bucket", "bitonic_gpu"]
        out += ["## 0.3.1: bitonic hybrids and the GPU (gcc)", "",
                "One run of `std::sort`, radix, bucket and bitonic sort against bitonic merge sort, bitonic "
                "bucket sort and bitonic sort on the GPU (OpenCL, NVIDIA GeForce GTX 1660 SUPER; copies to "
                "and from the GPU included). Speed-up over `std::sort` in the same run (above 1: faster); "
                "median of 7" + (", all results correct." if all(r["ok"] for r in r031) else
                                 "; **some results were wrong**."), "",
                "| type | data | " + " | ".join(f"{LABELS.get(a, a)} {s >> 20 and str(s >> 20) + ' Mi' or str(s >> 10) + ' Ki'}"
                                                for a in algos for s in sizes) + " |",
                "|---|---|" + "---:|" * (len(algos) * len(sizes))]
        for t in ("u32", "u64", "f64"):
            for d in ("uniform", "skewed", "sorted", "few-unique"):
                cells = []
                for a in algos:
                    for n in sizes:
                        base, v = by.get((t, d, n, "std::sort")), by.get((t, d, n, a))
                        cells.append(f"{base / v:.2f}×" if base and v else "–")
                out.append(f"| {t} | {d} | " + " | ".join(cells) + " |")
        out.append("")
    real = load_real(MACHINE)
    if real:
        header, body = real_table(real)
        out += ["## Name sort on real name lists", "",
                f"Compiler: {real[0]['compiler']}; ns per name at the full list, median of 7 runs. " + REAL_NOTE, "",
                "| " + " | ".join(header) + " |", "|---|" + "---:|" * (len(header) - 1)]
        out += ["| " + " | ".join(r) + " |" for r in body]
        out.append("")
    MD_OUT.write_text("\n".join(out), encoding="utf-8", newline="\n")
    print(f"wrote {MD_OUT.relative_to(ROOT)}")


# ------------------------------------------------------------------ html

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sorting Benchmarks</title>
<style>
:root {
  color-scheme: light;
  --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --ring: rgba(11,11,11,0.10);
  --s1: #2a78d6; --s2: #eb6834; --s3: #1baf7a; --s4: #eda100;
  --s5: #e87ba4; --s6: #008300; --s7: #6250d6; --s8: #e34948;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    color-scheme: dark;
    --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --ring: rgba(255,255,255,0.10);
    --s1: #3987e5; --s2: #d95926; --s3: #199e70; --s4: #c98500;
    --s5: #d55181; --s6: #008300; --s7: #9085e9; --s8: #e66767;
  }
}
:root[data-theme="dark"] {
  color-scheme: dark;
  --page: #0d0d0d; --surface: #1a1a19; --ink: #f0efec; --ink-2: #c3c2b7; --muted: #898781;
  --grid: #2c2c2a; --axis: #383835; --ring: rgba(255,255,255,0.10);
  --s1: #3987e5; --s2: #d95926; --s3: #199e70; --s4: #c98500;
  --s5: #d55181; --s6: #008300; --s7: #9085e9; --s8: #e66767;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--page); color: var(--ink);
       font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 1180px; margin: 0 auto; padding: 32px 16px 64px; }
h1 { font-size: 28px; margin: 0 0 4px; font-weight: 650; }
h2 { font-size: 19px; margin: 40px 0 4px; font-weight: 600; }
p { margin: 4px 0 12px; color: var(--ink-2); max-width: 75ch; }
.meta { color: var(--muted); font-size: 13px; }
.tiles { display: grid; grid-template-columns: repeat(auto-fit, minmax(210px, 1fr)); gap: 12px; margin: 24px 0 8px; }
.tile { background: var(--surface); border: 1px solid var(--ring); border-radius: 10px; padding: 14px 16px; }
.tile .label { color: var(--ink-2); font-size: 13px; }
.tile .value { font-size: 30px; font-weight: 600; margin: 2px 0; }
.tile .sub { color: var(--muted); font-size: 12.5px; }
.filters { display: flex; flex-wrap: wrap; gap: 16px; align-items: center; margin: 12px 0 8px; }
.filters label { color: var(--ink-2); font-size: 13px; display: flex; gap: 8px; align-items: center; }
select { font: inherit; font-size: 14px; color: var(--ink); background: var(--surface);
         border: 1px solid var(--axis); border-radius: 6px; padding: 4px 8px; }
.legend { display: flex; flex-wrap: wrap; gap: 6px 18px; margin: 8px 0 4px; font-size: 13px; color: var(--ink-2); }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
.legend i { display: inline-block; width: 16px; height: 2px; border-radius: 1px; }
.grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 480px), 1fr)); gap: 12px; }
.card { background: var(--surface); border: 1px solid var(--ring); border-radius: 10px; padding: 12px 12px 6px; position: relative; }
.card h3 { font-size: 14px; font-weight: 600; margin: 0 0 2px 4px; }
.card .note { font-size: 12px; color: var(--muted); margin: 0 0 4px 4px; }
svg { display: block; width: 100%; height: auto; overflow: visible; }
svg text { fill: var(--muted); font-size: 11px; font-variant-numeric: tabular-nums; }
.tip { position: absolute; pointer-events: none; background: var(--surface); border: 1px solid var(--ring);
       border-radius: 8px; padding: 8px 10px; font-size: 12.5px; box-shadow: 0 4px 16px rgba(0,0,0,0.12);
       display: none; min-width: 170px; z-index: 2; }
.tip .t { color: var(--ink-2); margin-bottom: 4px; }
.tip .row { display: flex; align-items: center; gap: 8px; }
.tip .row i { width: 12px; height: 2px; border-radius: 1px; display: inline-block; }
.tip .row b { font-weight: 600; font-variant-numeric: tabular-nums; min-width: 52px; }
.tip .row span { color: var(--ink-2); }
details { margin-top: 12px; }
summary { cursor: pointer; color: var(--ink-2); font-size: 14px; }
.tablewrap { overflow-x: auto; }
table { border-collapse: collapse; font-size: 13px; margin-top: 8px; font-variant-numeric: tabular-nums; }
th, td { padding: 4px 10px; text-align: right; border-bottom: 1px solid var(--grid); white-space: nowrap; }
th { color: var(--ink-2); font-weight: 600; }
th:first-child, td:first-child { text-align: left; }
code { font-size: 13px; }
</style>
</head>
<body>
<main>
<h1>Sorting benchmarks</h1>
<div class="meta" id="meta"></div>
<p>Radix sort, bucket sort, bitonic sort and name sort from SpeedUp, against the standard library.
Time per element: lower is better, both axes logarithmic. Every result was checked against
<code>std::sort</code> before it was timed. Run-to-run noise on this machine is about 10&ndash;15&nbsp;%.</p>
<div class="tiles" id="tiles"></div>

<h2>Name sort</h2>
<p>Synthetic &ldquo;Surname Firstname&rdquo; lists drawn by US Census name frequencies. Name sort cuts buckets
from frequency tables of positions 1&ndash;3 from 131,072 names on; below that it runs its MSD radix sort alone.</p>
<div class="filters"><label>Compiler <select id="names-compiler"></select></label></div>
<div class="legend" id="names-legend"></div>
<div class="grid"><div class="card" id="names-chart"><h3>ns per name</h3></div>
<div class="card" id="names-buckets"><h3>Name sort's buckets</h3><p class="note">Planned runs only: buckets cut, and the largest one</p></div></div>
<details><summary>Table: name sort</summary><div class="tablewrap" id="names-table"></div></details>
__REAL__

<h2>Numeric sorts</h2>
<p>Bitonic sort does O(n log&sup2; n) work, so it is measured up to 4&nbsp;Mi elements only. Bucket sort takes 64-bit types.</p>
<div class="filters">
  <label>Compiler <select id="num-compiler"></select></label>
  <label>Element type <select id="num-type"></select></label>
</div>
<div class="legend" id="num-legend"></div>
<div class="grid" id="num-charts"></div>
<details><summary>Table: numeric sorts for the selected compiler and type</summary><div class="tablewrap" id="num-table"></div></details>

<section id="s031" hidden>
<h2>0.3.1: bitonic hybrids and the GPU</h2>
<p>One GCC run: bitonic merge sort, bitonic bucket sort and bitonic sort on the GPU (OpenCL, NVIDIA GeForce GTX 1660 SUPER,
copies to and from the GPU included) against <code>std::sort</code>, radix, bucket and CPU bitonic sort.</p>
<div class="filters"><label>Element type <select id="s031-type"></select></label></div>
<div class="legend" id="s031-legend"></div>
<div class="grid" id="s031-charts"></div>
<details><summary>Table: 0.3.1 run for the selected type</summary><div class="tablewrap" id="s031-table"></div></details>
</section>
</main>
<script>
const DATA = __DATA__;
const SLOTS = __SLOTS__;
const LABELS = __LABELS__;
const NUMERIC_ORDER = __NUMERIC_ORDER__;
const NAMES_ORDER = __NAMES_ORDER__;
const DISTS = __DISTS__;
const NS = "http://www.w3.org/2000/svg";

const label = a => LABELS[a] || a;
const color = a => `var(--s${SLOTS[a]})`;
const fmtN = n => n >= 1048576 && n % 1048576 === 0 ? (n / 1048576) + " Mi" :
                  n >= 1024 && n % 1024 === 0 ? (n / 1024) + " Ki" : n.toLocaleString("en-US");
const fmtNames = n => n >= 1e6 ? (n / 1e6) + "M" : n >= 1000 ? (n / 1000) + "k" : String(n);
const fmtNs = v => v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2);

function el(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}

function legend(target, algos, paint) {
  paint = paint || color;
  target.replaceChildren();
  for (const a of algos) {
    const s = document.createElement("span");
    const i = document.createElement("i");
    i.style.background = paint(a);
    s.append(i, document.createTextNode(label(a)));
    target.appendChild(s);
  }
}

// Line chart: x and y logarithmic, crosshair snaps to the nearest size,
// one tooltip lists every series there.
function lineChart(card, series, xfmt, unit, paint) {
  paint = paint || color;
  card.querySelectorAll("svg, .tip").forEach(e => e.remove());
  const W = 520, H = 290, m = {l: 44, r: 12, t: 24, b: 30};
  const xs = [...new Set(series.flatMap(s => s.points.map(p => p[0])))].sort((a, b) => a - b);
  const ys = series.flatMap(s => s.points.map(p => p[1]));
  if (!xs.length) return;
  const lx = Math.log2, ly = Math.log10;
  const x0 = lx(xs[0]), x1 = lx(xs[xs.length - 1]) || x0 + 1;
  let y0 = Math.floor(ly(Math.min(...ys)) * 2) / 2, y1 = Math.ceil(ly(Math.max(...ys)) * 2) / 2;
  if (y1 - y0 < 0.5) y1 = y0 + 0.5;
  const X = v => m.l + (lx(v) - x0) / (x1 - x0 || 1) * (W - m.l - m.r);
  const Y = v => H - m.b - (ly(v) - y0) / (y1 - y0) * (H - m.t - m.b);
  const svg = el("svg", {viewBox: `0 0 ${W} ${H}`, role: "img"}, card);
  const title = el("title", {}, svg);
  title.textContent = card.querySelector("h3").textContent;
  for (let e = Math.floor(y0); e <= Math.ceil(y1); e++) {
    for (const k of [1, 2, 5]) {
      const v = k * Math.pow(10, e);
      if (ly(v) < y0 - 1e-9 || ly(v) > y1 + 1e-9) continue;
      el("line", {x1: m.l, x2: W - m.r, y1: Y(v), y2: Y(v), stroke: "var(--grid)", "stroke-width": 1}, svg);
      const t = el("text", {x: m.l - 6, y: Y(v) + 4, "text-anchor": "end"}, svg);
      t.textContent = v >= 1 ? String(v) : v.toFixed(1);
    }
  }
  el("line", {x1: m.l, x2: W - m.r, y1: H - m.b, y2: H - m.b, stroke: "var(--axis)", "stroke-width": 1}, svg);
  const step = Math.max(1, Math.ceil(xs.length / 7));
  xs.forEach((v, i) => {
    if (i % step && i !== xs.length - 1) return;
    const t = el("text", {x: X(v), y: H - m.b + 16, "text-anchor": "middle"}, svg);
    t.textContent = xfmt(v);
  });
  const yl = el("text", {x: m.l - 6, y: 12, "text-anchor": "end"}, svg);
  yl.textContent = unit;
  for (const s of series) {
    const d = s.points.map((p, i) => (i ? "L" : "M") + X(p[0]).toFixed(1) + "," + Y(p[1]).toFixed(1)).join("");
    el("path", {d, fill: "none", stroke: paint(s.algo), "stroke-width": 2, "stroke-linejoin": "round",
                "stroke-linecap": "round"}, svg);
  }
  const cross = el("line", {y1: m.t, y2: H - m.b, stroke: "var(--axis)", "stroke-width": 1, visibility: "hidden"}, svg);
  const dots = series.map(s => el("circle", {r: 4, fill: paint(s.algo), stroke: "var(--surface)",
                                             "stroke-width": 2, visibility: "hidden"}, svg));
  const tip = document.createElement("div");
  tip.className = "tip";
  card.appendChild(tip);
  const hit = el("rect", {x: m.l, y: m.t, width: W - m.l - m.r, height: H - m.t - m.b, fill: "transparent",
                          tabindex: 0}, svg);
  function show(xi) {
    const xv = xs[xi], px = X(xv);
    cross.setAttribute("x1", px); cross.setAttribute("x2", px); cross.setAttribute("visibility", "visible");
    tip.replaceChildren();
    const head = document.createElement("div");
    head.className = "t";
    head.textContent = "n = " + xfmt(xv);
    tip.appendChild(head);
    const rows = [];
    series.forEach((s, k) => {
      const p = s.points.find(q => q[0] === xv);
      if (!p) { dots[k].setAttribute("visibility", "hidden"); return; }
      dots[k].setAttribute("cx", px); dots[k].setAttribute("cy", Y(p[1])); dots[k].setAttribute("visibility", "visible");
      rows.push([p[1], s.algo]);
    });
    rows.sort((a, b) => a[0] - b[0]);
    for (const [v, a] of rows) {
      const r = document.createElement("div");
      r.className = "row";
      const i = document.createElement("i");
      i.style.background = paint(a);
      const b = document.createElement("b");
      b.textContent = fmtNs(v) + " ns";
      const sp = document.createElement("span");
      sp.textContent = label(a);
      r.append(i, b, sp);
      tip.appendChild(r);
    }
    tip.style.display = "block";
    const box = svg.getBoundingClientRect(), cb = card.getBoundingClientRect();
    const left = box.left - cb.left + px / W * box.width;
    tip.style.top = (box.top - cb.top + 12) + "px";
    tip.style.left = (left > cb.width / 2 ? left - tip.offsetWidth - 12 : left + 12) + "px";
  }
  function hide() {
    tip.style.display = "none"; cross.setAttribute("visibility", "hidden");
    dots.forEach(d => d.setAttribute("visibility", "hidden"));
  }
  let current = xs.length - 1;
  hit.addEventListener("pointermove", ev => {
    const box = svg.getBoundingClientRect();
    const vx = (ev.clientX - box.left) / box.width * W;
    let best = 0;
    xs.forEach((v, i) => { if (Math.abs(X(v) - vx) < Math.abs(X(xs[best]) - vx)) best = i; });
    current = best;
    show(best);
  });
  hit.addEventListener("pointerleave", hide);
  hit.addEventListener("focus", () => show(current));
  hit.addEventListener("blur", hide);
  hit.addEventListener("keydown", ev => {
    if (ev.key === "ArrowRight") { current = Math.min(xs.length - 1, current + 1); show(current); }
    if (ev.key === "ArrowLeft") { current = Math.max(0, current - 1); show(current); }
  });
}

function table(target, header, rows) {
  const t = document.createElement("table");
  const tr = document.createElement("tr");
  for (const h of header) { const th = document.createElement("th"); th.textContent = h; tr.appendChild(th); }
  t.appendChild(tr);
  for (const r of rows) {
    const row = document.createElement("tr");
    for (const c of r) { const td = document.createElement("td"); td.textContent = c; row.appendChild(td); }
    t.appendChild(row);
  }
  target.replaceChildren(t);
}

function fill(select, values, fmt) {
  select.replaceChildren();
  for (const v of values) { const o = document.createElement("option"); o.value = v; o.textContent = fmt ? fmt(v) : v; select.appendChild(o); }
}

const compilers = [...new Set(DATA.map(r => r.compiler))];
document.getElementById("meta").textContent = __META__;

function tiles() {
  const box = document.getElementById("tiles");
  const comp = compilers.includes("gcc") ? "gcc" : compilers[0];
  const pick = (kind, type, dist, algo, n) => DATA.find(r => r.compiler === comp && r.kind === kind &&
      r.type === type && r.dist === dist && r.algo === algo && (n === undefined || r.n === n));
  const largest = (kind, type, dist, algo) => DATA.filter(r => r.compiler === comp && r.kind === kind &&
      r.type === type && r.dist === dist && r.algo === algo).sort((a, b) => b.n - a.n)[0];
  const items = [];
  const ns = largest("names", "str", "names", "name_sort");
  if (ns) {
    const base = pick("names", "str", "names", "std::sort", ns.n);
    items.push(["Name sort vs std::sort", (base.ns / ns.ns).toFixed(1) + "×", fmtNames(ns.n) + " names, " + comp]);
  }
  const ratio = k => k >= 1 ? k.toFixed(1) + "×" : (1 / k).toFixed(1) + "× slower";
  // [type, algorithm, title, size (undefined: the largest measured)]
  for (const [type, algo, txt, size] of [["u32", "radix", "Radix sort vs std::sort"],
                                         ["f64", "bucket", "Bucket sort vs std::sort"],
                                         ["u32", "bitonic", "Bitonic sort (AVX2) vs std::sort", 1048576]]) {
    const r = size ? pick("numeric", type, "uniform", algo, size) : largest("numeric", type, "uniform", algo);
    if (!r) continue;
    const base = pick("numeric", type, "uniform", "std::sort", r.n);
    let sub = fmtN(r.n) + " uniform " + type + ", " + comp;
    if (size) {
      // a sorting network wins while the data fits the caches; say where it stops
      const big = largest("numeric", type, "uniform", algo);
      const bigBase = pick("numeric", type, "uniform", "std::sort", big.n);
      sub += "; " + ratio(bigBase.ns / big.ns) + " at " + fmtN(big.n);
    }
    items.push([txt, ratio(base.ns / r.ns), sub]);
  }
  box.replaceChildren();
  for (const [l, v, s] of items) {
    const d = document.createElement("div"); d.className = "tile";
    const a = document.createElement("div"); a.className = "label"; a.textContent = l;
    const b = document.createElement("div"); b.className = "value"; b.textContent = v;
    const c = document.createElement("div"); c.className = "sub"; c.textContent = s;
    d.append(a, b, c); box.appendChild(d);
  }
}

function renderNames() {
  const comp = document.getElementById("names-compiler").value;
  const rows = DATA.filter(r => r.compiler === comp && r.kind === "names");
  const algos = NAMES_ORDER.filter(a => rows.some(r => r.algo === a));
  legend(document.getElementById("names-legend"), algos);
  lineChart(document.getElementById("names-chart"),
            algos.map(a => ({algo: a, points: rows.filter(r => r.algo === a).map(r => [r.n, r.ns]).sort((p, q) => p[0] - q[0])})),
            fmtNames, "ns");
  const planned = rows.filter(r => r.algo === "name_sort" && r.buckets).sort((a, b) => a.n - b.n);
  const card = document.getElementById("names-buckets");
  card.querySelectorAll(".tablewrap").forEach(e => e.remove());
  const wrap = document.createElement("div"); wrap.className = "tablewrap"; card.appendChild(wrap);
  table(wrap, ["names", "buckets", "mean size", "largest", "std. dev."],
        planned.map(r => [r.n.toLocaleString("en-US"), r.buckets.toLocaleString("en-US"),
                          (r.n / r.buckets).toFixed(0), r.max_bucket.toLocaleString("en-US"), r.sd.toFixed(0)]));
  const sizes = [...new Set(rows.map(r => r.n))].sort((a, b) => a - b);
  table(document.getElementById("names-table"), ["names", ...algos.map(label), "name sort vs std::sort"],
        sizes.map(n => {
          const v = a => rows.find(r => r.n === n && r.algo === a);
          return [n.toLocaleString("en-US"), ...algos.map(a => v(a) ? fmtNs(v(a).ns) : "–"),
                  (v("std::sort").ns / v("name_sort").ns).toFixed(2) + "×"];
        }));
}

function renderNumeric() {
  const comp = document.getElementById("num-compiler").value;
  const type = document.getElementById("num-type").value;
  const rows = DATA.filter(r => r.compiler === comp && r.kind === "numeric" && r.type === type);
  const algos = NUMERIC_ORDER.filter(a => rows.some(r => r.algo === a));
  legend(document.getElementById("num-legend"), algos);
  const grid = document.getElementById("num-charts");
  grid.replaceChildren();
  for (const d of DISTS) {
    const card = document.createElement("div"); card.className = "card";
    const h = document.createElement("h3"); h.textContent = d + " " + type + ": ns per element";
    card.appendChild(h); grid.appendChild(card);
    lineChart(card, algos.map(a => ({algo: a, points: rows.filter(r => r.dist === d && r.algo === a)
                                       .map(r => [r.n, r.ns]).sort((p, q) => p[0] - q[0])})), fmtN, "ns");
  }
  const sizes = [...new Set(rows.map(r => r.n))].sort((a, b) => a - b);
  const out = [];
  for (const d of DISTS) for (const n of sizes) {
    out.push([d, fmtN(n), ...algos.map(a => { const r = rows.find(q => q.dist === d && q.n === n && q.algo === a); return r ? fmtNs(r.ns) : "–"; })]);
  }
  table(document.getElementById("num-table"), ["distribution", "n", ...algos.map(label)], out);
}

const DATA031 = __DATA031__;
const SLOTS031 = __SLOTS031__;
const ORDER031 = __ORDER031__;
const paint031 = a => `var(--s${SLOTS031[a]})`;
function render031() {
  const type = document.getElementById("s031-type").value;
  const rows = DATA031.filter(r => r.type === type);
  const algos = ORDER031.filter(a => rows.some(r => r.algo === a));
  legend(document.getElementById("s031-legend"), algos, paint031);
  const grid = document.getElementById("s031-charts");
  grid.replaceChildren();
  for (const d of DISTS) {
    const card = document.createElement("div"); card.className = "card";
    const h = document.createElement("h3"); h.textContent = d + " " + type + ": ns per element";
    card.appendChild(h); grid.appendChild(card);
    lineChart(card, algos.map(a => ({algo: a, points: rows.filter(r => r.dist === d && r.algo === a)
                                       .map(r => [r.n, r.ns]).sort((p, q) => p[0] - q[0])})), fmtN, "ns", paint031);
  }
  const sizes = [...new Set(rows.map(r => r.n))].sort((a, b) => a - b);
  const out = [];
  for (const d of DISTS) for (const n of sizes) {
    out.push([d, fmtN(n), ...algos.map(a => { const r = rows.find(q => q.dist === d && q.n === n && q.algo === a); return r ? fmtNs(r.ns) : "–"; })]);
  }
  table(document.getElementById("s031-table"), ["distribution", "n", ...algos.map(label)], out);
}
if (DATA031.length) {
  document.getElementById("s031").hidden = false;
  fill(document.getElementById("s031-type"), [...new Set(DATA031.map(r => r.type))]);
  document.getElementById("s031-type").addEventListener("change", render031);
}

const types = [...new Set(DATA.filter(r => r.kind === "numeric").map(r => r.type))];
fill(document.getElementById("names-compiler"), compilers);
fill(document.getElementById("num-compiler"), compilers);
fill(document.getElementById("num-type"), types);
document.getElementById("names-compiler").addEventListener("change", renderNames);
document.getElementById("num-compiler").addEventListener("change", renderNumeric);
document.getElementById("num-type").addEventListener("change", renderNumeric);
tiles();
renderNames();
renderNumeric();
if (DATA031.length) render031();
</script>
</body>
</html>
"""


def write_html(rows, cpu):
    compilers = sorted({r["compiler"] for r in rows})
    meta = f"{cpu} · Windows 10 · {', '.join(compilers)} · median of 5 runs · generated {date.today().isoformat()}"
    data = [{k: r[k] for k in ("compiler", "kind", "type", "dist", "n", "algo", "ns", "buckets", "max_bucket", "sd")}
            for r in rows]
    page = PAGE
    real = load_real(MACHINE)
    section = ""
    if real:
        header, body = real_table(real)

        def cells(row, tag):
            return "".join(f"<{tag}>{html.escape(c)}</{tag}>" for c in row)

        section = ('<h3 style="font-size:16px;margin:28px 0 4px">Name sort on real name lists</h3>'
                   f"<p>Compiler: {html.escape(real[0]['compiler'])}; ns per name at the full list, median of 7 runs. "
                   f"{html.escape(REAL_NOTE)}</p><div class=\"tablewrap\"><table><tr>{cells(header, 'th')}</tr>"
                   + "".join(f"<tr>{cells(r, 'td')}</tr>" for r in body) + "</table></div>")
    page = page.replace("__REAL__", section)
    d031 = [{k: r[k] for k in ("type", "dist", "n", "algo", "ns")} for r in load_031(MACHINE)]
    for key, value in (("__DATA031__", d031), ("__SLOTS031__", SLOTS031), ("__ORDER031__", ORDER031),
                       ("__DATA__", data), ("__SLOTS__", SLOTS), ("__LABELS__", LABELS),
                       ("__NUMERIC_ORDER__", NUMERIC_ORDER), ("__NAMES_ORDER__", NAMES_ORDER),
                       ("__DISTS__", DISTRIBUTIONS), ("__META__", meta)):
        page = page.replace(key, json.dumps(value, separators=(",", ":")).replace("</", "<\\/"))
    HTML_OUT.write_text(page, encoding="utf-8", newline="\n")
    print(f"wrote {HTML_OUT.relative_to(ROOT)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--machine", default="ryzen2600x")
    parser.add_argument("--cpu", default="AMD Ryzen 5 2600X")
    args = parser.parse_args()
    global MACHINE
    MACHINE = args.machine
    rows = load(args.machine)
    markdown(rows, args.cpu)
    write_html(rows, args.cpu)


if __name__ == "__main__":
    main()
