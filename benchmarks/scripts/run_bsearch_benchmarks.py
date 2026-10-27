#!/usr/bin/env python3
"""Runs the binary search study and turns its CSVs into the paper's inputs.

  run      runs speedup_bench_bsearch from one build directory
  analyze  reads benchmarks/outputs/bsearch_*.csv and writes the figures,
           tables and number macros under docs/papers/binary-search/

The full study (both compilers) was produced with:
  run --build <gcc build dir>  --label gcc
  run --build <msvc build dir> --label msvc
  run --build <...> --label gcc  --legacy-rand
  run --build <...> --label msvc --legacy-rand
  run --build <...> --label gcc  --library    (after the library's dispatch changed)
  run --build <...> --label msvc --library
  analyze
"""
import argparse
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ROOT / "benchmarks" / "outputs"
PAPER = ROOT / "docs" / "papers" / "binary-search"
PREFIX = "bsearch_ryzen2600x"

LEGACY_VARIANTS = ("reference,asm_branching,asm_branchless,asm_optimized,simd_avx2,"
                   "simd_avx2_optimized,branchless_prefetch,batch16")


def run(args):
    exe = Path(args.build) / ("speedup_bench_bsearch.exe" if sys.platform == "win32" else "speedup_bench_bsearch")
    if args.legacy_rand:
        out = OUTPUTS / f"{PREFIX}_{args.label}_legacy_rand.csv"
        cmd = [exe, "--legacy-rand", "--min-log2", "13", "--max-log2", "24", "--steps", "1",
               "--keys-log2", "19", "--reps", "9", "--modes", "throughput", "--only", LEGACY_VARIANTS]
    elif args.library:
        out = OUTPUTS / f"{PREFIX}_{args.label}_library.csv"
        cmd = [exe, "--min-log2", "10", "--max-log2", "26", "--steps", "2", "--keys-log2", "19", "--reps", "9",
               "--only", "reference,branchless,branchless_prefetch,batch16," + ",".join(LIBRARY_VARIANTS)]
    else:
        out = OUTPUTS / f"{PREFIX}_{args.label}.csv"
        cmd = [exe, "--min-log2", "10", "--max-log2", "26", "--steps", "2", "--keys-log2", "19", "--reps", "9"]
    cmd += ["--out", out]
    print(" ".join(str(c) for c in cmd))
    return subprocess.call([str(c) for c in cmd])


# ---------------------------------------------------------------- analysis

# Categorical slots in fixed order (dataviz reference palette, light mode).
SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
MARKERS = ["o", "s", "^", "D", "v", "P", "X", "*"]
DASHES = ["-", "--", "-.", ":", "-", "--", "-.", ":"]
INK, INK_2, GRID = "#0b0b0b", "#52514e", "#e4e3df"

LABELS = {
    "reference": "Reference (branchy, early exit)",
    "std_lower_bound": "std::lower_bound",
    "asm_branching": "Prototype asm, branching",
    "asm_branchless": "Prototype asm, cmov",
    "asm_optimized": "Prototype asm, \"pipelined\"",
    "simd_avx2": "Prototype AVX2",
    "simd_avx2_optimized": "Prototype AVX2 + prefetch",
    "branchless": "Branchless",
    "branchless_prefetch": "Branchless + prefetch",
    "eytzinger_prefetch": "Eytzinger + prefetch",
    "library": "Library (single key)",
    "batch4": "Interleaved, 4 keys",
    "batch8": "Interleaved, 8 keys",
    "batch16": "Interleaved, 16 keys",
    "batch32": "Interleaved, 32 keys",
    "batch16_prefetch": "Interleaved 16 + prefetch",
    "batch32_prefetch": "Interleaved 32 + prefetch",
    "batch16_select": "Interleaved 16, select",
    "batch32_select": "Interleaved 32, select",
    "library_batch": "Library (batch)",
}

# Cache levels one thread sees on Zen+: 32 KiB L1D, 512 KiB L2, and the 8 MiB
# L3 of its own core complex (the chip's 16 MiB is two 8 MiB halves).
CACHES = [(32 * 1024, "L1"), (512 * 1024, "L2"), (8 * 1024 * 1024, "L3")]


LIBRARY_VARIANTS = ("library", "library_batch")


def with_library_rerun(d, label):
    """The library's entry points changed after the main run (they now pick
    the kernel by the L2 hint); their rows come from <prefix>_<label>_library.csv,
    measured later with the same settings."""
    import pandas as pd
    path = OUTPUTS / f"{PREFIX}_{label}_library.csv"
    if not path.exists():
        return d
    rerun = pd.read_csv(path)
    rerun["toolchain"] = label
    rerun = rerun[rerun["variant"].isin(LIBRARY_VARIANTS)]
    return pd.concat([d[~d["variant"].isin(LIBRARY_VARIANTS)], rerun], ignore_index=True)


def load():
    import pandas as pd
    frames = []
    for label in ("gcc", "msvc"):
        for suffix in ("", "_legacy_rand"):
            path = OUTPUTS / f"{PREFIX}_{label}{suffix}.csv"
            if path.exists():
                d = pd.read_csv(path)
                d["toolchain"] = label
                if not suffix:
                    d = with_library_rerun(d, label)
                frames.append(d)
    if not frames:
        sys.exit("no bsearch CSVs in benchmarks/outputs")
    d = pd.concat(frames, ignore_index=True)
    bad = d[d["checksum_ok"] != 1]
    if len(bad):
        sys.exit(f"{len(bad)} rows failed their checksum; refusing to plot them")
    d["log2n"] = d["n"].map(lambda n: math.log2(n))
    return d


def size_label(nbytes):
    for unit, scale in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if nbytes >= scale:
            v = nbytes / scale
            return f"{v:g} {unit}"
    return f"{nbytes} B"


def style_axes(ax, ylabel):
    import matplotlib.ticker as mt
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ticks = [2 ** k for k in range(13, 30, 2)]  # 8 KiB .. 512 MiB in bytes
    ax.set_xticks(ticks)
    ax.set_xticklabels([size_label(t) for t in ticks])
    ax.yaxis.set_major_formatter(mt.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.yaxis.set_minor_formatter(mt.NullFormatter())
    ax.set_xlabel("Array size (8-byte keys)", color=INK_2)
    ax.set_ylabel(ylabel, color=INK_2)
    ax.grid(True, which="major", color=GRID, linewidth=0.6)
    ax.tick_params(colors=INK_2, labelsize=8)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(INK_2)
    lo, hi = ax.get_ylim()
    for nbytes, name in CACHES:
        ax.axvline(nbytes, color=INK_2, linewidth=0.6, linestyle=(0, (2, 3)))
        ax.text(nbytes * 1.06, lo * 1.08, name, color=INK_2, fontsize=7, va="bottom")


def legend_below(ax, ncol=3):
    """Legend under the x axis, so it never covers data."""
    ax.legend(fontsize=7, frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=ncol)


def line_chart(d, variants, path, ylabel, title=None):
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    for i, v in enumerate(variants):
        s = d[d["variant"] == v].sort_values("n")
        if s.empty:
            continue
        ax.plot(s["bytes"], s["median_ns"], color=SLOTS[i], linestyle=DASHES[i], linewidth=1.6,
                marker=MARKERS[i], markersize=3.2, markevery=2, label=LABELS.get(v, v))
        ax.fill_between(s["bytes"], s["q1_ns"], s["q3_ns"], color=SLOTS[i], alpha=0.15, linewidth=0)
    style_axes(ax, ylabel)
    if title:
        ax.set_title(title, fontsize=9, color=INK, loc="left")
    legend_below(ax)
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)


def write_figures(d, figdir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.family": "serif", "pdf.fonttype": 42, "font.size": 8})
    figdir.mkdir(parents=True, exist_ok=True)
    u = d[d["keys"] == "uniform"]
    single = ["reference", "std_lower_bound", "asm_branching", "branchless", "branchless_prefetch",
              "eytzinger_prefetch", "batch16"]
    for tc in ("gcc", "msvc"):
        t = u[(u["toolchain"] == tc) & (u["mode"] == "throughput")]
        line_chart(t, single, figdir / f"throughput_{tc}.pdf", "ns per search (median, IQR band)")
        lat = u[(u["toolchain"] == tc) & (u["mode"] == "latency")]
        line_chart(lat, ["reference", "std_lower_bound", "asm_branching", "asm_branchless", "branchless",
                         "branchless_prefetch", "eytzinger_prefetch"],
                   figdir / f"latency_{tc}.pdf", "ns per search (median, IQR band)")
        b = u[(u["toolchain"] == tc) & (u["mode"] == "throughput")]
        line_chart(b, ["batch4", "batch8", "batch16", "batch32", "batch16_prefetch", "batch32_select",
                       "branchless_prefetch"],
                   figdir / f"batch_{tc}.pdf", "ns per search (median, IQR band)")

    # Compiler sensitivity: MSVC time / GCC time, one axis, 1.0 = same.
    import matplotlib.ticker as mt
    fig, ax = plt.subplots(figsize=(6.4, 3.8))
    t = u[u["mode"] == "throughput"]
    for i, v in enumerate(["reference", "std_lower_bound", "branchless", "branchless_prefetch",
                           "eytzinger_prefetch", "batch16", "batch32_select"]):
        g = t[(t["toolchain"] == "gcc") & (t["variant"] == v)].set_index("n")["median_ns"]
        m = t[(t["toolchain"] == "msvc") & (t["variant"] == v)].set_index("n")["median_ns"]
        r = (m / g).dropna()
        if r.empty:
            continue
        ax.plot(r.index * 8, r.values, color=SLOTS[i], linestyle=DASHES[i], linewidth=1.6, marker=MARKERS[i],
                markersize=3.2, markevery=2, label=LABELS[v])
    ax.axhline(1.0, color=INK_2, linewidth=0.8)
    style_axes(ax, "MSVC time / GCC time")
    ax.yaxis.set_major_locator(mt.FixedLocator([0.25, 0.5, 1, 2, 4]))
    legend_below(ax)
    fig.tight_layout()
    fig.savefig(figdir / "compilers.pdf")
    plt.close(fig)

    # The rand() pitfall: the same variants with the old and the new keys.
    leg = d[(d["keys"] == "legacy_rand") & (d["toolchain"] == "gcc")]
    new = u[(u["toolchain"] == "gcc") & (u["mode"] == "throughput")]
    fig, ax = plt.subplots(figsize=(6.4, 4.0))
    for i, v in enumerate(["reference", "asm_branching", "asm_branchless", "branchless_prefetch"]):
        for keys, frame, dash, suffix in (("uniform", new, "-", "uniform keys"),
                                          ("legacy", leg, ":", "rand() keys")):
            s = frame[(frame["variant"] == v) & (frame["n"] >= 8192) & (frame["n"] <= 1 << 24)].sort_values("n")
            ax.plot(s["bytes"], s["median_ns"], color=SLOTS[i], linestyle=dash, linewidth=1.6,
                    marker=MARKERS[i], markersize=3.2, label=f"{LABELS[v]}, {suffix}")
    style_axes(ax, "ns per search (median)")
    legend_below(ax, ncol=2)
    fig.tight_layout()
    fig.savefig(figdir / "legacy_rand.pdf")
    plt.close(fig)


def fmt(v):
    return f"{v:.1f}" if v < 100 else f"{v:.0f}"


def write_macros(d, gen):
    """Number macros for the paper: \\bsns{toolchain}{mode}{variant}{log2 n}
    is a median in ns, for every power-of-two size; \\bsleg the same for the
    rand() keys; \\bsx, \\bscc and \\bslegx are ratios (see below)."""
    gen.mkdir(parents=True, exist_ok=True)
    lines = ["% Generated by benchmarks/scripts/run_bsearch_benchmarks.py analyze. Do not edit.",
             "\\makeatletter"]
    u = d[d["keys"] == "uniform"]
    for row in u.itertuples():
        if abs(row.log2n - round(row.log2n)) > 1e-9:
            continue
        key = f"bs@{row.toolchain}@{row.mode}@{row.variant}@{int(round(row.log2n))}"
        lines.append(f"\\expandafter\\def\\csname {key}\\endcsname{{{fmt(row.median_ns)}}}")
        lines.append(f"\\expandafter\\def\\csname {key}@raw\\endcsname{{{row.median_ns:.4f}}}")
    leg = d[d["keys"] == "legacy_rand"]
    for row in leg.itertuples():
        key = f"bsleg@{row.toolchain}@{row.variant}@{int(round(row.log2n))}"
        lines.append(f"\\expandafter\\def\\csname {key}\\endcsname{{{fmt(row.median_ns)}}}")
    lines += [
        "\\newcommand{\\bsns}[4]{\\@ifundefined{bs@#1@#2@#3@#4}{\\textbf{??}}{\\csname bs@#1@#2@#3@#4\\endcsname}}",
        "\\newcommand{\\bsleg}[3]{\\@ifundefined{bsleg@#1@#2@#3}{\\textbf{??}}{\\csname bsleg@#1@#2@#3\\endcsname}}",
        "\\makeatother",
    ]
    # Ratios used in the text, computed here so the paper never divides.
    ratios = []

    def cell(tc, mode, v, k, frame=u):
        s = frame[(frame["toolchain"] == tc) & (frame["mode"] == mode) & (frame["variant"] == v)
                  & ((frame["log2n"] - k).abs() < 1e-9)]
        return float(s["median_ns"].iloc[0]) if len(s) else float("nan")

    for tc in ("gcc", "msvc"):
        for mode in ("throughput", "latency"):
            for k in range(10, 27):
                for a, b in (("reference", "batch16"), ("reference", "eytzinger_prefetch"),
                             ("reference", "branchless_prefetch"), ("branchless", "branchless_prefetch"),
                             ("reference", "branchless"), ("reference", "library"),
                             ("reference", "library_batch"), ("branchless_prefetch", "batch16"),
                             ("std_lower_bound", "batch16"), ("std_lower_bound", "eytzinger_prefetch"),
                             ("eytzinger_prefetch", "batch16"),
                             ("asm_branching", "reference"), ("batch32_select", "batch32"),
                             ("batch16_prefetch", "batch16"), ("std_lower_bound", "branchless_prefetch")):
                    x, y = cell(tc, mode, a, k), cell(tc, mode, b, k)
                    if x == x and y == y and y > 0:
                        ratios.append((f"bsx@{tc}@{mode}@{a}@{b}@{k}", x / y))
            if mode == "throughput":
                for k in range(10, 27):
                    for v in ("reference", "std_lower_bound", "branchless", "branchless_prefetch",
                              "eytzinger_prefetch", "batch16", "batch32_select", "library", "library_batch"):
                        g, m = cell("gcc", mode, v, k), cell("msvc", mode, v, k)
                        if g == g and m == m and g > 0:
                            ratios.append((f"bscc@{mode}@{v}@{k}", m / g))
    for tc in ("gcc", "msvc"):
        for k in range(13, 25):
            for v in ("reference", "asm_branching", "asm_branchless", "branchless_prefetch"):
                x = cell(tc, "throughput", v, k)
                s = leg[(leg["toolchain"] == tc) & (leg["variant"] == v) & ((leg["log2n"] - k).abs() < 1e-9)]
                if len(s) and x == x:
                    ratios.append((f"bslegx@{tc}@{v}@{k}", x / float(s["median_ns"].iloc[0])))
    # Run-to-run spread: the second run of 2^17..2^21 (<prefix>_<tc>_rerun.csv)
    # against the main run, and the spread within the main run's passes.
    import pandas as pd
    for tc in ("gcc", "msvc"):
        path = OUTPUTS / f"{PREFIX}_{tc}_rerun.csv"
        if not path.exists():
            continue
        rerun = pd.read_csv(path)
        rerun = rerun[(rerun["mode"] == "throughput") & ~rerun["variant"].isin(LIBRARY_VARIANTS)]
        main = u[(u["toolchain"] == tc) & (u["mode"] == "throughput")]
        m = main.merge(rerun, on=["variant", "n"], suffixes=("", "_re"))
        r = m["median_ns"] / m["median_ns_re"]
        spread = r.where(r >= 1, 1 / r)
        ratios.append((f"bsrun@{tc}@between", spread.max()))
        ratios.append((f"bsrun@{tc}@betweenmedian", spread.median()))
        ratios.append((f"bsrun@{tc}@within", ((m["q3_ns"] - m["q1_ns"]) / m["median_ns"]).median() * 100))
        ends = m["n"].isin([m["n"].min(), m["n"].max()])
        ratios.append((f"bsrun@{tc}@ends", (spread[ends].max() - 1) * 100))
    # Worst and best case of one variant against another over every size.
    for tc in ("gcc", "msvc"):
        for mode in ("throughput", "latency"):
            t = u[(u["toolchain"] == tc) & (u["mode"] == mode)].pivot_table(index="n", columns="variant",
                                                                             values="median_ns")
            for a, b in (("library", "reference"), ("branchless_prefetch", "branchless"),
                         ("simd_avx2", "reference")):
                if a in t and b in t:
                    r = (t[a] / t[b]).dropna()
                    ratios.append((f"bsrange@{tc}@{mode}@{a}@{b}@max", r.max()))
                    ratios.append((f"bsrange@{tc}@{mode}@{a}@{b}@min", r.min()))
    # Power-of-two penalty: time at 2^k over the geometric mean of its two
    # half-octave neighbours; the largest on large sizes, the smallest on small.
    for tc in ("gcc", "msvc"):
        t = u[(u["toolchain"] == tc) & (u["mode"] == "throughput")].copy()
        t["half_step"] = (t["log2n"] * 2).round() / 2  # sizes between octaves are rounded integers
        for v in t["variant"].unique():
            s = t[t["variant"] == v].set_index("half_step")["median_ns"]

            def penalty(k):
                a, b, c = s.get(k - 0.5), s.get(float(k)), s.get(k + 0.5)
                return b / math.sqrt(a * c) if a and b and c else float("nan")

            for name, lo, hi, pick in (("large", 20, 25, max), ("small", 11, 16, min)):
                vals = [x for x in (penalty(k) for k in range(lo, hi + 1)) if x == x]
                if vals:
                    ratios.append((f"bsalias@{tc}@{v}@{name}", pick(vals)))
    lines.append("\\makeatletter")
    for key, val in ratios:
        text = f"{val:.0f}" if key.endswith(("@within", "@ends")) else f"{val:.2f}"  # percentages
        lines.append(f"\\expandafter\\def\\csname {key}\\endcsname{{{text}}}")
    lines += [
        # \bsx{toolchain}{mode}{variant A}{variant B}{log2 n}: A's time / B's
        "\\newcommand{\\bsx}[5]{\\@ifundefined{bsx@#1@#2@#3@#4@#5}{\\textbf{??}}{\\csname bsx@#1@#2@#3@#4@#5\\endcsname}}",
        # \bscc{mode}{variant}{log2 n}: MSVC time / GCC time
        "\\newcommand{\\bscc}[3]{\\@ifundefined{bscc@#1@#2@#3}{\\textbf{??}}{\\csname bscc@#1@#2@#3\\endcsname}}",
        # \bsrange{toolchain}{mode}{variant A}{variant B}{max|min}: extreme of A's time / B's over all sizes
        "\\newcommand{\\bsrange}[5]{\\@ifundefined{bsrange@#1@#2@#3@#4@#5}{\\textbf{??}}{\\csname bsrange@#1@#2@#3@#4@#5\\endcsname}}",
        # \bsrun{toolchain}{between|betweenmedian|within}: run-to-run spread (x), within-run IQR (% of median)
        "\\newcommand{\\bsrun}[2]{\\@ifundefined{bsrun@#1@#2}{\\textbf{??}}{\\csname bsrun@#1@#2\\endcsname}}",
        # \bsalias{toolchain}{variant}{large|small}: power-of-two penalty
        "\\newcommand{\\bsalias}[3]{\\@ifundefined{bsalias@#1@#2@#3}{\\textbf{??}}{\\csname bsalias@#1@#2@#3\\endcsname}}",
        # \bslegx{toolchain}{variant}{log2 n}: uniform-key time / rand()-key time
        "\\newcommand{\\bslegx}[3]{\\@ifundefined{bslegx@#1@#2@#3}{\\textbf{??}}{\\csname bslegx@#1@#2@#3\\endcsname}}",
        "\\makeatother",
    ]
    (gen / "numbers.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_tables(d, gen):
    u = d[d["keys"] == "uniform"]
    sizes = [12, 16, 20, 23, 26]
    order = ["reference", "std_lower_bound", "asm_branching", "asm_branchless", "asm_optimized", "simd_avx2",
             "simd_avx2_optimized", "branchless", "branchless_prefetch", "eytzinger_prefetch", "batch8",
             "batch16", "batch32", "library", "library_batch"]
    for tc in ("gcc", "msvc"):
        rows = []
        head = " & ".join(f"\\multicolumn{{1}}{{c}}{{{size_label(8 * 2 ** k)}}}" for k in sizes)
        rows.append("\\begin{tabular}{l" + "r" * len(sizes) + "@{\\hskip 1.2em}" + "r" * len(sizes) + "}")
        rows.append("\\toprule")
        rows.append(f" & \\multicolumn{{{len(sizes)}}}{{c}}{{Throughput (ns/search)}} & "
                    f"\\multicolumn{{{len(sizes)}}}{{c}}{{Latency (ns/search)}}\\\\")
        rows.append(f"\\cmidrule(lr){{2-{len(sizes) + 1}}}\\cmidrule(lr){{{len(sizes) + 2}-{2 * len(sizes) + 1}}}")
        rows.append("Variant & " + head + " & " + head + "\\\\")
        rows.append("\\midrule")
        best = {}
        for mode in ("throughput", "latency"):
            for k in sizes:
                s = u[(u["toolchain"] == tc) & (u["mode"] == mode) & ((u["log2n"] - k).abs() < 1e-9)
                      & u["variant"].isin(order)]
                best[(mode, k)] = s["median_ns"].min() if len(s) else None
        for v in order:
            cells = []
            for mode in ("throughput", "latency"):
                for k in sizes:
                    s = u[(u["toolchain"] == tc) & (u["mode"] == mode) & (u["variant"] == v)
                          & ((u["log2n"] - k).abs() < 1e-9)]
                    if not len(s):
                        cells.append("--")
                        continue
                    val = float(s["median_ns"].iloc[0])
                    txt = fmt(val)
                    cells.append(f"\\textbf{{{txt}}}" if best[(mode, k)] == val else txt)
            name = (LABELS[v].replace("std::lower_bound", "\\texttt{std::lower\\_bound}")
                    .replace("\"", "``", 1).replace("\"", "''", 1))
            rows.append(f"{name} & " + " & ".join(cells) + "\\\\")
        rows += ["\\bottomrule", "\\end{tabular}"]
        (gen / f"table_{tc}.tex").write_text("\n".join(rows) + "\n", encoding="utf-8")


def analyze(_args):
    d = load()
    write_figures(d, PAPER / "figures")
    write_macros(d, PAPER / "generated")
    write_tables(d, PAPER / "generated")
    print(f"wrote figures and tables under {PAPER}")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--build", required=True, help="build directory holding speedup_bench_bsearch")
    r.add_argument("--label", required=True, help="toolchain label used in the CSV name (gcc, msvc, ...)")
    r.add_argument("--legacy-rand", action="store_true", help="reproduce the prototype's rand() keys")
    r.add_argument("--library", action="store_true", help="re-measure only the library's entry points")
    r.set_defaults(func=run)
    a = sub.add_parser("analyze")
    a.set_defaults(func=analyze)
    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
