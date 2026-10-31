#!/usr/bin/env python3
"""Synthetic person-name lists for the name sort benchmark (labelled synthetic).

Draws "Surname Firstname" pairs, both weighted by how common they are in
the US Census name data (benchmarks/datasets/names/, written by
codegen/name_tables/build_name_tables.py). One name per line, UTF-8.
Deterministic for a given seed.

Usage:
    capture_names.py COUNT [--seed S] [--order surname-first|first-surname] [-o OUT]
"""

import argparse
import gzip
import random
import sys
from pathlib import Path

NAMES = Path(__file__).resolve().parent / "names"


def read_weighted(file_name):
    names, weights = [], []
    with gzip.open(NAMES / file_name, "rt", encoding="ascii") as f:
        for line in f:
            name, weight = line.rstrip("\n").split("\t")
            names.append(name)
            weights.append(float(weight))
    return names, weights


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("count", type=int)
    parser.add_argument("--seed", type=int, default=2010)
    parser.add_argument("--order", choices=("surname-first", "first-surname"), default="surname-first")
    parser.add_argument("-o", "--out", help="output file (default: stdout)")
    args = parser.parse_args()

    surnames, surname_weights = read_weighted("surnames.tsv.gz")
    first, first_weights = read_weighted("first-names.tsv.gz")
    rnd = random.Random(args.seed)
    last = rnd.choices(surnames, weights=surname_weights, k=args.count)
    given = rnd.choices(first, weights=first_weights, k=args.count)
    if args.order == "surname-first":
        rows = (f"{s} {g}\n" for s, g in zip(last, given))
    else:
        rows = (f"{g} {s}\n" for s, g in zip(last, given))

    out = open(args.out, "w", encoding="utf-8", newline="\n") if args.out else sys.stdout
    with out:
        out.writelines(rows)


if __name__ == "__main__":
    main()
