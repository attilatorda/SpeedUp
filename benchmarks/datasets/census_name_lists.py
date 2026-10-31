#!/usr/bin/env python3
"""Real-name test lists for name sort, one name per line, UTF-8.

From the US Census 2010 surname list (benchmarks/datasets/names/surnames.tsv.gz,
162,253 surnames), in the orders real exports come in:
  census-shuffled.txt         random order (fixed seed)
  census-rank.txt             popularity order, as the Census publishes it
  census-sorted.txt           already sorted (byte order)
  census-reversed.txt         reverse sorted
  census-upper-shuffled.txt   ALL CAPS, as the Census writes them, shuffled
and, from capture_listing.py's multilingual generator:
  intl-1m.txt                 1,000,000 names from 47 words in 15 scripts

Usage:
    census_name_lists.py OUT_DIR
    speedup_bench_sort --only names --names OUT_DIR/census-shuffled.txt \\
        --out benchmarks/outputs/namesort_real_census-shuffled_<machine>_<compiler>.csv
"""

import gzip
import random
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def write(out_dir, name, rows):
    (out_dir / name).write_text("".join(r + "\n" for r in rows), encoding="utf-8", newline="\n")
    print(f"{name}: {len(rows)} names")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)

    names = []
    with gzip.open(HERE / "names" / "surnames.tsv.gz", "rt", encoding="ascii") as f:
        for line in f:
            names.append(line.split("\t")[0])  # file order is the Census popularity rank

    shuffled = names[:]
    random.Random(2010).shuffle(shuffled)
    by_bytes = sorted(names, key=lambda s: s.encode("utf-8"))
    write(out_dir, "census-shuffled.txt", shuffled)
    write(out_dir, "census-rank.txt", names)
    write(out_dir, "census-sorted.txt", by_bytes)
    write(out_dir, "census-reversed.txt", by_bytes[::-1])
    write(out_dir, "census-upper-shuffled.txt", [s.upper() for s in shuffled])

    tsv = out_dir / "intl-1m.tsv"
    subprocess.run([sys.executable, str(HERE / "capture_listing.py"), "intl", "1000000", "-o", str(tsv)], check=True)
    rows = []
    with open(tsv, encoding="utf-8", errors="surrogateescape") as f:
        next(f)  # header
        for line in f:
            name = line.split("\t")[1]
            rows.append(name.replace("\\t", "\t").replace("\\\\", "\\"))
    tsv.unlink()
    write(out_dir, "intl-1m.txt", rows)


if __name__ == "__main__":
    main()
