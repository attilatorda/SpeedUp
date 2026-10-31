#!/usr/bin/env python3
"""Writes the figures, tables and number macros of the sorting paper
(docs/papers/sorting/) from the CSVs in benchmarks/outputs/:

  sort_<machine>_<compiler>.csv                         numeric sorts, synthetic names
  namesort_real_<list>_<machine>_<compiler>.csv         real name lists
  namesort_ablation_<config>_<data>_<machine>_gcc.csv   one design choice changed
  sort_bitonic_<machine>_<compiler>.csv                 bitonic sort with and without AVX2
and from the name data in benchmarks/datasets/names/.

Every number in the paper's text is a macro \\so{key}; a key that is missing
prints as ?? in the PDF.
"""
import csv
import gzip
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ROOT / "benchmarks" / "outputs"
NAMES = ROOT / "benchmarks" / "datasets" / "names"
PAPER = ROOT / "docs" / "papers" / "sorting"
GEN = PAPER / "generated"
FIG = PAPER / "figures"
MACHINE = "ryzen2600x"
COMPILERS = ["gcc", "msvc"]
COMPILER_LABEL = {"gcc": "GCC", "msvc": "MSVC"}

REAL_LISTS = [
    ("census-shuffled", "Census surnames, shuffled"),
    ("census-rank", "Census surnames, popularity order"),
    ("census-sorted", "Census surnames, sorted"),
    ("census-reversed", "Census surnames, reversed"),
    ("census-upper-shuffled", "Census surnames, upper case"),
    ("intl-1m", "Multilingual names"),
]
ABLATION = [
    ("default", "Library choices (depth 3, sample $n/128$, prior weight 16)"),
    ("depth2", "Trie depth 2"),
    ("depth4", "Trie depth 4"),
    ("depth8", "Trie depth 8 (4{,}096 blocks)"),
    ("sample-only", "Prior weight 0 (sample only)"),
    ("tables-only", "Prior weight $10^9$ (tables only)"),
    ("sample16", "Sample $n/16$"),
    ("introsort-buckets", "Introsort for buckets up to 256"),
]
ABLATION_DATA = [("names-1m", "Synthetic"), ("census-shuffled", "Census"), ("intl-1m", "Multilingual")]

# Categorical slots in fixed order (the palette of the binary-search paper);
# a colour follows its algorithm through every figure.
SLOTS = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
ALGO_SLOT = {"std::sort": 0, "name_sort": 1, "msd_radix": 2, "radix": 3, "bucket": 4, "bitonic": 5,
             "std::stable_sort": 6, "qsort": 7, "bitonic_scalar": 5}
ALGO_LABEL = {"std::sort": "std::sort", "std::stable_sort": "std::stable_sort", "qsort": "qsort",
              "radix": "radix sort", "bucket": "bucket sort", "bitonic": "bitonic sort (AVX2)",
              "bitonic_scalar": "bitonic sort (scalar)", "name_sort": "name sort", "msd_radix": "MSD radix sort"}
INK, INK_2, GRID = "#0b0b0b", "#52514e", "#e4e3df"

macros = {}


def tex_int(v):
    return f"{int(round(v)):,}".replace(",", "{,}")


def ns(v):
    if v >= 100:
        return tex_int(v)
    if v >= 10:
        return f"{v:.1f}"
    return f"{v:.2f}"


def factor(v):
    return f"{v:.1f}" if v >= 10 else f"{v:.2f}"


def pct(v):
    return f"{v:.1f}" if v < 10 else f"{v:.0f}"


def put(key, value):
    macros[key] = value


def read_csv(path):
    if not path.exists():
        return []
    with open(path, newline="") as f:
        out = []
        for r in csv.DictReader(f):
            if not r.get("ns_per_element") or not r.get("ok"):
                continue  # a run still in progress ends in a partial line
            out.append({"kind": r["kind"], "type": r["type"], "dist": r["distribution"], "n": int(r["n"]),
                        "algo": r["algorithm"], "ns": float(r["ns_per_element"]), "ok": r["ok"] == "1",
                        "buckets": int(r["buckets"]) if r.get("buckets") else None,
                        "max_bucket": int(r["max_bucket"]) if r.get("max_bucket") else None,
                        "sd": float(r["stddev_bucket"]) if r.get("stddev_bucket") else None,
                        "q1": float(r["q1_ns"]) if r.get("q1_ns") else None,
                        "q3": float(r["q3_ns"]) if r.get("q3_ns") else None})
        return out


def index(rows):
    return {(r["kind"], r["type"], r["dist"], r["n"], r["algo"]): r for r in rows}


# ---------------------------------------------------------------- the data

def name_data():
    surnames = []
    with gzip.open(NAMES / "surnames.tsv.gz", "rt", encoding="ascii") as f:
        for line in f:
            name, count = line.rstrip("\n").split("\t")
            surnames.append((name, int(count)))
    total = sum(c for _, c in surnames)
    first, prefix2, prefix3 = Counter(), Counter(), Counter()
    for name, c in surnames:
        first[name[:1]] += c
        prefix2[name[:2]] += c
        prefix3[name[:3]] += c
    put("data@surnames", tex_int(len(surnames)))
    put("data@people", f"{total / 1e6:.0f}")
    for letter in "MSBCHRQXUZ":
        share = 100 * first[letter] / total
        put(f"share@{letter}", f"{share:.2f}" if share < 1 else f"{share:.1f}")
    for p in ["Ma", "Mc", "Ha", "Wil", "Mar", "Smi", "Joh", "Sch"]:
        share = 100 * (prefix2 if len(p) == 2 else prefix3)[p] / total
        put(f"share@{p}", f"{share:.2f}" if share < 1 else f"{share:.1f}")
    with gzip.open(NAMES / "first-names.tsv.gz", "rt", encoding="ascii") as f:
        put("data@first", tex_int(sum(1 for _ in f)))
    # the 1990 lists, as counted by the table generator
    hdr = (ROOT / "src" / "algorithms" / "sort" / "name_freq_tables.h").read_text(encoding="ascii")
    words = hdr.split()
    put("data@female", tex_int(int(words[words.index("female") - 1])))
    put("data@male", tex_int(int(words[words.index("male") - 1])))


# ---------------------------------------------------------------- names

def synthetic_names():
    rows_by = {}
    for comp in COMPILERS:
        rows = [r for r in read_csv(OUTPUTS / f"sort_{MACHINE}_{comp}.csv") if r["kind"] == "names"]
        rows_by[comp] = rows
        by = {(r["n"], r["algo"]): r for r in rows}
        for n in sorted({r["n"] for r in rows}):
            if (n, "name_sort") not in by:
                continue
            s, m, x = by[(n, "std::sort")]["ns"], by[(n, "msd_radix")]["ns"], by[(n, "name_sort")]
            put(f"syn@{comp}@{n}@std", ns(s))
            put(f"syn@{comp}@{n}@msd", ns(m))
            put(f"syn@{comp}@{n}@ns", ns(x["ns"]))
            put(f"syn@{comp}@{n}@vsstd", factor(s / x["ns"]))
            put(f"syn@{comp}@{n}@vsmsd", factor(m / x["ns"]))
            if x["buckets"]:
                put(f"syn@{comp}@{n}@buckets", tex_int(x["buckets"]))
                put(f"syn@{comp}@{n}@largest", tex_int(x["max_bucket"]))
                put(f"syn@{comp}@{n}@mean", tex_int(n / x["buckets"]))
                put(f"syn@{comp}@{n}@sd", tex_int(x["sd"]))
    sizes = [1000, 10000, 100000, 200000, 500000, 1000000]
    lines = [r"\begin{tabular}{r" + "rrrr" * len(COMPILERS) + "}", r"\toprule",
             " & " + " & ".join(rf"\multicolumn{{4}}{{c}}{{{COMPILER_LABEL[c]}}}" for c in COMPILERS) + r"\\",
             " ".join(rf"\cmidrule(lr){{{2 + 4 * i}-{5 + 4 * i}}}" for i in range(len(COMPILERS))),
             "Names & " + " & ".join([r"\texttt{std::sort} & MSD radix & Name sort & vs.\ \texttt{std::sort}"] *
                                     len(COMPILERS)) + r"\\", r"\midrule"]
    for n in sizes:
        cells = [tex_int(n)]
        for comp in COMPILERS:
            k = f"syn@{comp}@{n}"
            cells += [macros.get(f"{k}@std", "--"), macros.get(f"{k}@msd", "--"), macros.get(f"{k}@ns", "--"),
                      (macros[f"{k}@vsstd"] + r"$\times$") if f"{k}@vsstd" in macros else "--"]
        lines.append(" & ".join(cells) + r"\\")
    lines += [r"\bottomrule", r"\end{tabular}"]
    (GEN / "table_names.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return rows_by


def real_names():
    lines = [r"\begin{tabular}{lr" + "rrr" * len(COMPILERS) + "}", r"\toprule",
             " & & " + " & ".join(rf"\multicolumn{{3}}{{c}}{{{COMPILER_LABEL[c]}}}" for c in COMPILERS) + r"\\",
             " ".join(rf"\cmidrule(lr){{{3 + 3 * i}-{5 + 3 * i}}}" for i in range(len(COMPILERS))),
             "List & Names & " + " & ".join([r"\texttt{std::sort} & MSD radix & Name sort"] * len(COMPILERS)) +
             r"\\", r"\midrule"]
    all_ok = True
    for key, text in REAL_LISTS:
        cells, n_full = [text], None
        for comp in COMPILERS:
            rows = read_csv(OUTPUTS / f"namesort_real_{key}_{MACHINE}_{comp}.csv")
            if not rows:
                cells += ["--"] * 3
                continue
            all_ok &= all(r["ok"] for r in rows)
            n_full = max(r["n"] for r in rows)
            by = {r["algo"]: r for r in rows if r["n"] == n_full}
            s, m, x = by["std::sort"]["ns"], by["msd_radix"]["ns"], by["name_sort"]["ns"]
            put(f"real@{comp}@{key}@std", ns(s))
            put(f"real@{comp}@{key}@msd", ns(m))
            put(f"real@{comp}@{key}@ns", ns(x))
            put(f"real@{comp}@{key}@vsstd", factor(s / x))
            put(f"real@{comp}@{key}@vsmsd", factor(m / x))
            put(f"real@{comp}@{key}@slower", pct(100 * (x / m - 1)))
            # bold: the faster of name sort and MSD radix sort
            cells += [ns(s), (r"\textbf{%s}" % ns(m)) if m < x else ns(m), (r"\textbf{%s}" % ns(x)) if x <= m else ns(x)]
        cells.insert(1, tex_int(n_full) if n_full else "--")
        lines.append(" & ".join(cells) + r"\\")
    lines += [r"\bottomrule", r"\end{tabular}"]
    (GEN / "table_real.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    put("real@allok", "all" if all_ok else "\\textbf{not all}")
    # slower-than-MSD range on the Census lists, per compiler
    for comp in COMPILERS:
        vals = [float(macros[f"real@{comp}@{k}@slower"]) for k, _ in REAL_LISTS
                if k.startswith("census") and f"real@{comp}@{k}@slower" in macros]
        if vals:
            put(f"real@{comp}@census@slowermin", pct(min(vals)))
            put(f"real@{comp}@census@slowermax", pct(max(vals)))
            vs = [float(macros[f"real@{comp}@{k}@vsstd"]) for k, _ in REAL_LISTS
                  if k.startswith("census") and f"real@{comp}@{k}@vsstd" in macros]
            put(f"real@{comp}@census@vsstdmin", factor(min(vs)))
            put(f"real@{comp}@census@vsstdmax", factor(max(vs)))


def ablation():
    res = {}
    for config, _ in ABLATION + [("always-plan", "")]:
        for data, _ in ABLATION_DATA:
            rows = read_csv(OUTPUTS / f"namesort_ablation_{config}_{data}_{MACHINE}_gcc.csv")
            if rows:
                res[(config, data)] = rows
    if not res:
        return
    lines = [r"\begin{tabular}{l" + "rr" * len(ABLATION_DATA) + "}", r"\toprule",
             " & " + " & ".join(rf"\multicolumn{{2}}{{c}}{{{t}}}" for _, t in ABLATION_DATA) + r"\\",
             " ".join(rf"\cmidrule(lr){{{2 + 2 * i}-{3 + 2 * i}}}" for i in range(len(ABLATION_DATA))),
             "Configuration & " + " & ".join(["ns/name & vs.\\ MSD"] * len(ABLATION_DATA)) + r"\\", r"\midrule"]
    for config, text in ABLATION:
        cells = [text]
        for data, _ in ABLATION_DATA:
            rows = res.get((config, data))
            if not rows:
                cells += ["--", "--"]
                continue
            n = max(r["n"] for r in rows)
            by = {r["algo"]: r for r in rows if r["n"] == n}
            x, m = by["name_sort"], by["msd_radix"]["ns"]
            put(f"abl@{config}@{data}@ns", ns(x["ns"]))
            put(f"abl@{config}@{data}@vsmsd", factor(m / x["ns"]))
            if x["buckets"]:
                put(f"abl@{config}@{data}@buckets", tex_int(x["buckets"]))
                put(f"abl@{config}@{data}@largest", tex_int(x["max_bucket"]))
            cells += [ns(x["ns"]), factor(m / x["ns"]) + r"$\times$"]
        lines.append(" & ".join(cells) + r"\\")
        if config == "default":
            lines.append(r"\midrule")
    lines += [r"\bottomrule", r"\end{tabular}"]
    (GEN / "table_ablation.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    # where planning starts to pay: the smallest n from which name sort,
    # always planning, stays faster than MSD radix sort in the same run
    for data, _ in ABLATION_DATA:
        rows = res.get(("always-plan", data))
        if not rows:
            continue
        by = {(r["n"], r["algo"]): r["ns"] for r in rows}
        sizes = sorted({r["n"] for r in rows})
        cross = None
        for n in reversed(sizes):
            if by[(n, "name_sort")] < by[(n, "msd_radix")]:
                cross = n
            else:
                break
        put(f"abl@cross@{data}", tex_int(cross) if cross else "none")
        put(f"abl@cross@{data}@lastslower",
            tex_int(max([n for n in sizes if cross is None or n < cross], default=0)))


# ---------------------------------------------------------------- numeric

def numeric():
    idx = {}
    for comp in COMPILERS:
        rows = [r for r in read_csv(OUTPUTS / f"sort_{MACHINE}_{comp}.csv") if r["kind"] == "numeric"]
        idx[comp] = {(r["type"], r["dist"], r["n"], r["algo"]): r for r in rows}
        for (t, d, n, a), r in idx[comp].items():
            put(f"num@{comp}@{t}@{d}@{n}@{a}", ns(r["ns"]))
            base = idx[comp].get((t, d, n, "std::sort"))
            if base and a != "std::sort":
                put(f"num@{comp}@{t}@{d}@{n}@{a}@x", factor(base["ns"] / r["ns"]))
                put(f"num@{comp}@{t}@{d}@{n}@{a}@inv", factor(r["ns"] / base["ns"]))
        put(f"num@{comp}@allok", "all" if all(r["ok"] for r in rows) else "\\textbf{not all}")
        # bitonic sort (AVX2) against std::sort: up to which size it wins
        for t in ("u32", "u64"):
            wins = [n for (tt, d, n, a) in idx[comp] if tt == t and d == "uniform" and a == "bitonic"
                    and idx[comp][(t, d, n, a)]["ns"] < idx[comp][(t, d, n, "std::sort")]["ns"]]
            put(f"num@{comp}@{t}@bitonic@winsupto", size_label(max(wins)) if wins else "never")
    big = 1 << 24
    lines = [r"\begin{tabular}{ll" + "rrr" * len(COMPILERS) + "}", r"\toprule",
             " & & " + " & ".join(rf"\multicolumn{{3}}{{c}}{{{COMPILER_LABEL[c]}}}" for c in COMPILERS) + r"\\",
             " ".join(rf"\cmidrule(lr){{{3 + 3 * i}-{5 + 3 * i}}}" for i in range(len(COMPILERS))),
             "Type & Data & \\texttt{std::sort} & Radix & Bucket" * 1 +
             (" & \\texttt{std::sort} & Radix & Bucket" * (len(COMPILERS) - 1)) + r"\\",
             " & & ns & speed-up & speed-up" + " & ns & speed-up & speed-up" * (len(COMPILERS) - 1) + r"\\",
             r"\midrule"]
    for t in ("u32", "u64", "f64"):
        for i, d in enumerate(("uniform", "skewed", "sorted", "few-unique")):
            cells = [t if i == 0 else "", d]
            for comp in COMPILERS:
                base = idx[comp].get((t, d, big, "std::sort"))
                if not base:
                    cells += ["--"] * 3
                    continue
                cells.append(ns(base["ns"]))
                for a in ("radix", "bucket"):
                    r = idx[comp].get((t, d, big, a))
                    cells.append((factor(base["ns"] / r["ns"]) + r"$\times$") if r else "--")
            lines.append(" & ".join(cells) + r"\\")
        if t != "f64":
            lines.append(r"\midrule")
    lines += [r"\bottomrule", r"\end{tabular}"]
    (GEN / "table_numeric.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return idx


def size_label(n):
    for unit, scale in (("Mi", 1 << 20), ("Ki", 1 << 10)):
        if n >= scale and n % scale == 0:
            return f"{n // scale}\\,{unit}"
    return tex_int(n)


def bitonic():
    sizes = [1 << 10, 1 << 14, 1 << 17, 1 << 20, 1 << 22]
    have = False
    lines = [r"\begin{tabular}{ll" + "r" * len(sizes) + "}", r"\toprule",
             "Compiler & Sort & " + " & ".join(size_label(n) for n in sizes) + r"\\", r"\midrule"]
    for comp in COMPILERS:
        rows = read_csv(OUTPUTS / f"sort_bitonic_{MACHINE}_{comp}.csv")
        if not rows:
            continue
        have = True
        by = {(r["type"], r["n"], r["algo"]): r["ns"] for r in rows if r["dist"] == "uniform"}
        for t in ("u32", "u64"):
            for a in ("std::sort", "bitonic", "bitonic_scalar"):
                label = {"std::sort": r"\texttt{std::sort}", "bitonic": "bitonic, AVX2",
                         "bitonic_scalar": "bitonic, scalar"}[a]
                cells = [f"{COMPILER_LABEL[comp]}, {t}" if a == "std::sort" else "", label]
                cells += [ns(by[(t, n, a)]) if (t, n, a) in by else "--" for n in sizes]
                lines.append(" & ".join(cells) + r"\\")
            for n in sizes:
                if (t, n, "bitonic") in by and (t, n, "bitonic_scalar") in by:
                    put(f"bit@{comp}@{t}@{n}@simdx", factor(by[(t, n, "bitonic_scalar")] / by[(t, n, "bitonic")]))
            if not (comp == COMPILERS[-1] and t == "u64"):
                lines.append(r"\midrule")
    lines += [r"\bottomrule", r"\end{tabular}"]
    if have:
        (GEN / "table_bitonic.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


def hybrids():
    """0.3.1: bitonic merge sort, bitonic bucket sort and bitonic sort on the GPU (one GCC run)."""
    rows = read_csv(OUTPUTS / f"sort031_{MACHINE}_gcc.csv")
    if not rows:
        return
    by = {(r["type"], r["dist"], r["n"], r["algo"]): r["ns"] for r in rows}
    put("h031@allok", "all" if all(r["ok"] for r in rows) else "\\textbf{not all}")
    for (t, d, n, a), v in by.items():
        put(f"h031@{t}@{d}@{n}@{a}", ns(v))
        base = by.get((t, d, n, "std::sort"))
        if base and a != "std::sort":
            put(f"h031@{t}@{d}@{n}@{a}@x", factor(base / v))
    # the smallest size from which the GPU stays ahead of the fastest CPU sort
    for t in ("u32", "u64"):
        sizes = sorted({n for (tt, d, n, a) in by if tt == t and d == "uniform" and a == "bitonic_gpu"})
        cross = None
        for n in reversed(sizes):
            cpu = min(v for (tt, d, nn, a), v in by.items()
                      if tt == t and d == "uniform" and nn == n and a not in ("bitonic_gpu",))
            if by[(t, "uniform", n, "bitonic_gpu")] < cpu:
                cross = n
            else:
                break
        put(f"h031@{t}@gpu@from", size_label(cross) if cross else "never")
    # The GPU run back to back (sort_gpuwarm_*): no CPU sorts in between, so
    # the GPU never idles and clocks down. Compared with the CPU sorts of the
    # interleaved run, whose times do not depend on the GPU.
    warm = {(r["type"], r["n"]): r["ns"] for r in read_csv(OUTPUTS / f"sort_gpuwarm_{MACHINE}_gcc.csv")
            if r["dist"] == "uniform"}
    for (t, n), v in warm.items():
        put(f"warm@{t}@{n}", ns(v))
        base = by.get((t, "uniform", n, "std::sort"))
        if base:
            put(f"warm@{t}@{n}@x", factor(base / v))
    for t in ("u32", "u64", "f64"):
        sizes_w = sorted(n for (tt, n) in warm if tt == t)
        cross = None
        for n in reversed(sizes_w):
            cpu = [v for (tt, d, nn, a), v in by.items()
                   if tt == t and d == "uniform" and nn == n and a != "bitonic_gpu"]
            if cpu and warm[(t, n)] < min(cpu):
                cross = n
            else:
                break
        put(f"warm@{t}@from", size_label(cross) if cross else "never")
    sizes = [1 << 16, 1 << 20, 1 << 24]
    algos = [("radix", "Radix"), ("bitonic_merge", "Bitonic merge"), ("bitonic_bucket", "Bitonic bucket"),
             ("bitonic_gpu", "Bitonic, GPU")]
    lines = [r"\begin{tabular}{ll" + "rrr" * len(algos) + "}", r"\toprule",
             " & & " + " & ".join(rf"\multicolumn{{3}}{{c}}{{{t}}}" for _, t in algos) + r"\\",
             " ".join(rf"\cmidrule(lr){{{3 + 3 * i}-{5 + 3 * i}}}" for i in range(len(algos))),
             "Type & Data & " + " & ".join([" & ".join(size_label(n) for n in sizes)] * len(algos)) + r"\\",
             r"\midrule"]
    for t in ("u32", "u64", "f64"):
        for i, d in enumerate(("uniform", "skewed", "sorted", "few-unique")):
            cells = [t if i == 0 else "", d]
            for a, _ in algos:
                for n in sizes:
                    base, v = by.get((t, d, n, "std::sort")), by.get((t, d, n, a))
                    cells.append(factor(base / v) if base and v else "--")
            lines.append(" & ".join(cells) + r"\\")
        if t != "f64":
            lines.append(r"\midrule")
    lines += [r"\bottomrule", r"\end{tabular}"]
    (GEN / "table_031.tex").write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------- figures

def style(ax, xlabel, ylabel, xs, xfmt):
    import matplotlib.ticker as mt
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xticks(xs)
    ax.set_xticklabels([xfmt(x) for x in xs])
    ax.xaxis.set_minor_locator(mt.NullLocator())
    ax.yaxis.set_major_formatter(mt.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.yaxis.set_minor_formatter(mt.FuncFormatter(
        lambda v, _: f"{v:g}" if str(f"{v:g}")[0] in "25" else ""))
    ax.set_xlabel(xlabel, color=INK_2)
    ax.set_ylabel(ylabel, color=INK_2)
    ax.grid(True, which="both", color=GRID, linewidth=0.5)
    ax.tick_params(colors=INK_2, labelsize=7, which="both")
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(INK_2)


def line(ax, rows, algo, label=None, dashed=False):
    rows = sorted(rows, key=lambda r: r["n"])
    if not rows:
        return
    xs, ys = [r["n"] for r in rows], [r["ns"] for r in rows]
    c = SLOTS[ALGO_SLOT[algo]]
    ax.plot(xs, ys, color=c, linewidth=1.5, linestyle="--" if dashed else "-", marker="o", markersize=2.6,
            label=label or ALGO_LABEL[algo])
    if rows[0]["q1"] is not None:
        ax.fill_between(xs, [r["q1"] for r in rows], [r["q3"] for r in rows], color=c, alpha=0.15, linewidth=0)


def figures(names_rows, idx):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    plt.rcParams.update({"font.family": "serif", "pdf.fonttype": 42, "font.size": 8})
    FIG.mkdir(parents=True, exist_ok=True)

    def names_fmt(n):
        return f"{n // 1000000}M" if n >= 1000000 else f"{n // 1000}k"

    # Name sort on synthetic names (GCC), and name sort when it always plans.
    rows = names_rows.get("gcc", [])
    if rows:
        fig, ax = plt.subplots(figsize=(6.4, 3.6))
        for a in ("std::sort", "msd_radix", "name_sort"):
            line(ax, [r for r in rows if r["algo"] == a], a)
        ap = read_csv(OUTPUTS / f"namesort_ablation_always-plan_names-1m_{MACHINE}_gcc.csv")
        if ap:
            line(ax, [r for r in ap if r["algo"] == "name_sort"], "name_sort", "name sort, planning at every size",
                 dashed=True)
        ax.axvline(131072, color=INK_2, linewidth=0.6, linestyle=(0, (2, 3)))
        ax.text(131072 * 1.05, ax.get_ylim()[1] * 0.92, "plan from 131,072", color=INK_2, fontsize=7, va="top")
        xs = sorted({r["n"] for r in rows if r["n"] in (1000, 10000, 100000, 1000000)} |
                    {r["n"] for r in rows if r["n"] in (2000, 5000, 20000, 50000, 200000, 500000)})
        style(ax, "Names (synthetic “Surname Firstname”)", "ns per name (median, IQR band)", xs, names_fmt)
        ax.legend(fontsize=7, frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=4)
        fig.tight_layout()
        fig.savefig(FIG / "names_gcc.pdf")
        plt.close(fig)

    # Numeric sorts (GCC): four panels.
    panels = [("u32", "uniform"), ("u64", "uniform"), ("f64", "skewed"), ("u64", "sorted")]
    if idx.get("gcc"):
        fig, axes = plt.subplots(2, 2, figsize=(6.6, 5.2))
        for ax, (t, d) in zip(axes.flat, panels):
            for a in ("std::sort", "std::stable_sort", "qsort", "radix", "bucket", "bitonic"):
                line(ax, [r for (tt, dd, n, aa), r in idx["gcc"].items() if tt == t and dd == d and aa == a], a)
            xs = [1 << k for k in range(10, 25, 2)]
            style(ax, "Elements", "ns per element", xs,
                  lambda n: f"{n >> 20}Mi" if n >= 1 << 20 else f"{n >> 10}Ki")
            ax.set_title(f"{t}, {d}", fontsize=8, color=INK, loc="left")
        handles, labels = axes.flat[1].get_legend_handles_labels()
        fig.legend(handles, labels, fontsize=7, frameon=False, loc="lower center", ncol=6)
        fig.tight_layout(rect=(0, 0.05, 1, 1))
        fig.savefig(FIG / "numeric_gcc.pdf")
        plt.close(fig)


# ---------------------------------------------------------------- output

def write_macros():
    out = ["% Generated by benchmarks/scripts/sorting_paper_numbers.py. Do not edit.", r"\makeatletter"]
    for k in sorted(macros):
        out.append(rf"\expandafter\def\csname so@{k}\endcsname{{{macros[k]}}}")
    out.append(r"\newcommand{\so}[1]{\@ifundefined{so@#1}{\textbf{??}}{\csname so@#1\endcsname}}")
    out.append(r"\makeatother")
    (GEN / "numbers.tex").write_text("\n".join(out) + "\n", encoding="utf-8")


def main():
    GEN.mkdir(parents=True, exist_ok=True)
    name_data()
    names_rows = synthetic_names()
    real_names()
    ablation()
    idx = numeric()
    bitonic()
    hybrids()
    figures(names_rows, idx)
    write_macros()
    print(f"wrote {len(macros)} numbers, tables and figures to {PAPER.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
