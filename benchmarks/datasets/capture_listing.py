#!/usr/bin/env python3
"""Capture real directory listings for the bucketization benchmarks.

Writes a TSV file with one entry per line:

    path  name  is_dir  is_hidden  size  mtime_ns  atime_ns

`path` is relative to the captured root (unique), `name` is the basename
(what a file manager sorts by). Tabs, newlines and backslashes in names are
escaped as \\t, \\n and \\\\. Only names and metadata are recorded, never file
contents, so you can capture your own folders and keep the files private.

Usage:
    capture_listing.py dir PATH [--recursive] [-o OUT]
    capture_listing.py git REPO [-o OUT]           # every file in HEAD, sizes from git, no times
    capture_listing.py photos COUNT [--seed S] [-o OUT]   # synthetic camera roll (labelled synthetic)
    capture_listing.py intl COUNT [--seed S] [-o OUT]     # synthetic multilingual names (collation checks)
"""

import argparse
import os
import random
import subprocess
import sys
import time

HEADER = "path\tname\tis_dir\tis_hidden\tsize\tmtime_ns\tatime_ns\n"


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def row(path, name, is_dir, size, mtime_ns, atime_ns):
    hidden = 1 if name.startswith(".") else 0
    return f"{escape(path)}\t{escape(name)}\t{int(is_dir)}\t{hidden}\t{size}\t{mtime_ns}\t{atime_ns}\n"


def capture_dir(root, recursive, out):
    count = 0
    stack = [""]
    while stack:
        rel = stack.pop()
        try:
            entries = list(os.scandir(os.path.join(root, rel)))
        except OSError as error:
            print(f"skipping {rel or root}: {error}", file=sys.stderr)
            continue
        for entry in entries:
            path = os.path.join(rel, entry.name)
            try:
                st = entry.stat(follow_symlinks=True)
            except OSError:
                st = entry.stat(follow_symlinks=False)  # broken symlink
            try:
                is_dir = entry.is_dir()
            except OSError:
                is_dir = False
            out.write(row(path, entry.name, is_dir, 0 if is_dir else st.st_size, st.st_mtime_ns, st.st_atime_ns))
            count += 1
            if recursive and is_dir and not entry.is_symlink():
                stack.append(path)
    return count


def capture_git(repo, out):
    listing = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "-l", "-z", "HEAD"],
                             check=True, capture_output=True).stdout
    count = 0
    for record in listing.split(b"\0"):
        if not record:
            continue
        meta, path = record.split(b"\t", 1)
        size = meta.split()[3]
        path = path.decode("utf-8", "surrogateescape")
        out.write(row(path, os.path.basename(path), False, int(size) if size != b"-" else 0, 0, 0))
        count += 1
    return count


def capture_photos(count, seed, out):
    """Synthetic camera roll: phone-style names, increasing timestamps, JPEG-like sizes."""
    rng = random.Random(seed)
    t = 1_600_000_000  # seconds
    for i in range(count):
        t += rng.choice([1, 1, 2, 3, 5, 30, 120, 600, 3600])  # bursts and gaps; ~15 years per million
        stamp = t
        tm = time.gmtime(stamp)
        style = rng.random()
        if style < 0.6:
            name = "IMG_%04d%02d%02d_%02d%02d%02d.jpg" % tm[:6]
        elif style < 0.8:
            name = "PXL_%04d%02d%02d_%02d%02d%02d%03d.jpg" % (tm[:6] + (rng.randrange(1000),))
        elif style < 0.95:
            name = "DSC%05d.JPG" % (i % 100000)
        else:
            name = "VID_%04d%02d%02d_%02d%02d%02d.mp4" % tm[:6]
        path = f"{i:07d}_{name}"  # unique even when DSC numbers wrap
        size = int(rng.lognormvariate(14.8, 0.4))
        out.write(row(path, name, False, size, stamp * 1_000_000_000, stamp * 1_000_000_000))
    return count


INTL_WORDS = [
    # Czech/Slovak (ch sorts after h), German, Swedish, French, Spanish, Polish, Turkish, Greek,
    # Cyrillic, Japanese, Chinese, Korean, Arabic, Hebrew, Thai, and symbols people put in names.
    "chata", "hrad", "Chrudim", "cizí", "čaj", "řeka", "Šumava", "žába", "Äpfel", "Österreich", "Straße",
    "über", "ångström", "Ärlig", "öl", "zebra", "élève", "Évian", "cœur", "niño", "Ñandú", "łódź", "Łukasz",
    "ılık", "İstanbul", "Ωmega", "αλφα", "Москва", "ёлка", "Ёж", "ファイル", "写真", "文件", "사진", "ملف",
    "קובץ", "ไฟล์", "file", "File", "FILE", "_draft", "-old", "(copy)", "#1", "~backup", "Report", "report",
]


def capture_intl(count, seed, out):
    """Synthetic multilingual names: words, numbers, separators, extensions, some folders."""
    rng = random.Random(seed)
    extensions = ["", ".txt", ".pdf", ".jpg", ".tar.gz", ".1", ".v2.3"]
    for i in range(count):
        parts = [rng.choice(INTL_WORDS)]
        for _ in range(rng.randrange(3)):
            parts.append(rng.choice([" ", "_", "-", "."]))
            parts.append(str(rng.randrange(0, 1200)) if rng.random() < 0.5 else rng.choice(INTL_WORDS))
        if rng.random() < 0.2:
            parts.insert(0, str(rng.randrange(0, 100)).zfill(rng.choice([1, 2, 3])) + " ")
        is_dir = rng.random() < 0.1
        name = "".join(parts) + ("" if is_dir else rng.choice(extensions))
        t = 1_600_000_000 + rng.randrange(0, 200_000_000)
        out.write(row(f"{i:07d}/{name}", name, is_dir, 0 if is_dir else rng.randrange(0, 10**7),
                      t * 1_000_000_000, t * 1_000_000_000))
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)
    d = sub.add_parser("dir")
    d.add_argument("path")
    d.add_argument("--recursive", action="store_true")
    g = sub.add_parser("git")
    g.add_argument("repo")
    p = sub.add_parser("photos")
    p.add_argument("count", type=int)
    p.add_argument("--seed", type=int, default=1)
    i = sub.add_parser("intl")
    i.add_argument("count", type=int)
    i.add_argument("--seed", type=int, default=1)
    for s in (d, g, p, i):
        s.add_argument("-o", "--out", help="output file (default: stdout)")
    args = parser.parse_args()

    out = open(args.out, "w", encoding="utf-8", errors="surrogateescape", newline="\n") if args.out else sys.stdout
    out.write(HEADER)
    if args.mode == "dir":
        count = capture_dir(args.path, args.recursive, out)
    elif args.mode == "git":
        count = capture_git(args.repo, out)
    elif args.mode == "intl":
        count = capture_intl(args.count, args.seed, out)
    else:
        count = capture_photos(args.count, args.seed, out)
    if args.out:
        out.close()
    print(f"{count} entries", file=sys.stderr)


if __name__ == "__main__":
    main()
