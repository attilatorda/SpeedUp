#!/usr/bin/env python3
"""Builds the papers in docs/papers/ and packs them for arXiv.

  build_papers.py                 PDFs of every paper (latexmk -pdf)
  build_papers.py --arxiv         also <paper>/arxiv/<paper>.tar.gz: the sources,
                                  generated inputs, figures and the .bbl, which
                                  arXiv needs because it does not run BibTeX
  build_papers.py --check-arxiv   unpack each tarball into a temporary
                                  directory and build it there with pdflatex
                                  only, as arXiv would
  build_papers.py binary-search   one paper only

Regenerate the inputs first:
  benchmarks/scripts/run_bsearch_benchmarks.py analyze
  benchmarks/scripts/bucketization_paper_numbers.py
  benchmarks/scripts/sorting_paper_numbers.py
"""
import argparse
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PAPERS = ["binary-search", "bucketization", "sorting"]
SOURCES = ["main.tex", "main.bbl", "refs.bib"]
DIRS = ["generated", "figures"]


def undefined(log_path):
    """LaTeX warnings about undefined citations or references."""
    log = log_path.read_text(encoding="latin-1")
    return [line for line in log.splitlines()
            if "undefined" in line.lower() and ("citation" in line.lower() or "reference" in line.lower())]


def build(paper):
    d = HERE / paper
    subprocess.run(["latexmk", "-pdf", "-interaction=nonstopmode", "-halt-on-error", "main.tex"], cwd=d, check=True)
    problems = undefined(d / "main.log")
    if problems:
        print("\n".join(problems))
        sys.exit(f"{paper}: undefined references or citations")
    print(f"{paper}: {d / 'main.pdf'}")


def pack(paper):
    d = HERE / paper
    out = d / "arxiv"
    out.mkdir(exist_ok=True)
    tar_path = out / f"{paper}.tar.gz"
    with tarfile.open(tar_path, "w:gz") as tar:
        for name in SOURCES:
            if (d / name).exists():
                tar.add(d / name, arcname=name)
        for sub in DIRS:
            if (d / sub).exists():
                for f in sorted((d / sub).iterdir()):
                    if f.suffix in (".tex", ".pdf", ".png"):
                        tar.add(f, arcname=f"{sub}/{f.name}")
    print(f"{paper}: {tar_path}")
    return tar_path


def check(paper, tar_path):
    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(tar_path) as tar:
            tar.extractall(tmp)
        for _ in range(2):
            subprocess.run(["pdflatex", "-interaction=nonstopmode", "-halt-on-error", "main.tex"], cwd=tmp,
                           check=True, stdout=subprocess.DEVNULL)
        if undefined(Path(tmp, "main.log")):
            sys.exit(f"{paper}: the arXiv package leaves undefined references")
        shutil.copy(Path(tmp, "main.pdf"), HERE / paper / "arxiv" / "check.pdf")
    print(f"{paper}: arXiv package builds cleanly")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("papers", nargs="*", default=PAPERS)
    p.add_argument("--arxiv", action="store_true")
    p.add_argument("--check-arxiv", action="store_true")
    args = p.parse_args()
    for paper in args.papers:
        build(paper)
        if args.arxiv or args.check_arxiv:
            tar_path = pack(paper)
            if args.check_arxiv:
                check(paper, tar_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
