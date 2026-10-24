#!/usr/bin/env python3
"""Fill the <PLACEHOLDERS> in the launch kit and write ready-to-paste copies.

Run this right after the SpeedUp release is public:

    python3 docs/bucketization/launch/fill_placeholders.py \
        --name "Given Family" --repo https://github.com/attilatorda/SpeedUp \
        --tag v0.2.0 --date 2026-11-01 [--doi 10.5281/zenodo.NNNNNNN] [--swh swh:1:rev:...]

Output: docs/bucketization/launch/out/ (git-ignored), one file per input file,
plus CITATION.cff ready for the repository root. Unfilled placeholders are
listed at the end, so nothing goes out with a <...> in it.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--name", required=True, help='your name as it should appear, e.g. "Attila Torda"')
    p.add_argument("--repo", required=True, help="public repository URL, without a trailing slash")
    p.add_argument("--tag", required=True, help="release tag, e.g. v0.2.0")
    p.add_argument("--date", required=True, help="release date, YYYY-MM-DD")
    p.add_argument("--branch", default=None, help="branch or tag for links (default: the tag)")
    p.add_argument("--doi", default="", help="Zenodo DOI of the release, if you made one")
    p.add_argument("--swh", default="", help="Software Heritage ID of the archived release, if any")
    args = p.parse_args()

    ref = args.branch or args.tag
    blob = f"{args.repo}/blob/{ref}"
    given, _, family = args.name.rpartition(" ")
    values = {
        "YOUR_NAME": args.name,
        "GIVEN_NAMES": given or args.name,
        "FAMILY_NAMES": family if given else "",
        "REPO_URL": args.repo,
        "RELEASE_TAG": args.tag,
        "RELEASE_DATE": args.date,
        "RELEASE_URL": f"{args.repo}/releases/tag/{args.tag}",
        "DESIGN_DOC_URL": f"{blob}/docs/bucketization/README.md",
        "BENCHMARKS_URL": f"{blob}/docs/bucketization/benchmarks.md",
        "CROSS_PROJECT_URL": f"{blob}/docs/bucketization/cross-project-benchmarks.md",
        "RESULTS_URL": f"{blob}/benchmarks/outputs/bucketization_file_manager_stacks.md",
        "CODE_URL": f"{blob}/include/speedup/bucketization",
        "CYCLES_URL": f"{blob}/benchmarks/kde_stack/find_comparator_cycles.cpp",
        "DOI": args.doi,
        "DOI_LINE": f"DOI: https://doi.org/{args.doi}" if args.doi else "",
        "SWH_ID": args.swh,
    }
    out_dir = os.path.join(HERE, "out")
    os.makedirs(os.path.join(out_dir, "projects"), exist_ok=True)
    pattern = re.compile(r"<([A-Z][A-Z_]+)>")
    missing = set()

    marker = re.compile(r"^(?:> )?<!-- (?:numbers:[a-z]+(?: [^>]*)?|/numbers|summary-table[^>]*) -->\n", re.M)

    def fill(text):
        text = marker.sub("", text)

        def sub(m):
            key = m.group(1)
            if key in values:
                return values[key]
            missing.add(key)
            return m.group(0)
        return pattern.sub(sub, text)

    sources = [f for f in os.listdir(HERE) if f.endswith(".md")]
    sources += [os.path.join("projects", f) for f in os.listdir(os.path.join(HERE, "projects")) if f.endswith(".md")]
    for rel in sorted(sources):
        with open(os.path.join(HERE, rel), encoding="utf-8") as f:
            text = fill(f.read())
        with open(os.path.join(out_dir, rel), "w", encoding="utf-8") as f:
            f.write(text)
    with open(os.path.join(HERE, "CITATION.cff.template"), encoding="utf-8") as f:
        citation = fill(f.read())
    citation = "\n".join(line for line in citation.splitlines() if not line.startswith("# ")) + "\n"
    with open(os.path.join(out_dir, "CITATION.cff"), "w", encoding="utf-8") as f:
        f.write(citation)
    print(f"wrote {len(sources) + 1} files to {os.path.relpath(out_dir)}")
    if missing:
        print("placeholders still unfilled:", ", ".join(sorted(missing)), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
