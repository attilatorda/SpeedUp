#!/usr/bin/env python3
"""Runs the sorting study (docs/papers/sorting/) and writes its CSVs.

  datasets DIR                 name lists into DIR: names-1m.txt (synthetic
                               "Surname Firstname"), the Census surname lists
                               and intl-1m.txt (census_name_lists.py)
  run --build B --label L      numeric sorts and synthetic names
                               -> benchmarks/outputs/sort_<machine>_<L>.csv,
                               then every real name list
                               -> benchmarks/outputs/namesort_real_<list>_<machine>_<L>.csv
  tune031 --build B            0.3.1: block, chunk and bucket sizes of the
                               bitonic hybrids -> benchmarks/outputs/tune031_*.csv
  ablation --build B           name sort with one design choice changed at a
                               time (compile-time knobs of name_sort.c), built
                               with gcc against B's libspeedup.a
                               -> benchmarks/outputs/namesort_ablation_<config>_<data>_<machine>_gcc.csv

The study was produced with:
  run_sort_benchmarks.py datasets <data>
  run_sort_benchmarks.py run --build build-gcc --label gcc --data <data>
  run_sort_benchmarks.py run --build build-msvc/Release --label msvc --data <data>
  run_sort_benchmarks.py ablation --build build-gcc --data <data>
then sorting_paper_numbers.py (paper inputs) and sort_report.py (results page).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ROOT / "benchmarks" / "outputs"
MACHINE = "ryzen2600x"
REAL_LISTS = ["census-shuffled", "census-rank", "census-sorted", "census-reversed", "census-upper-shuffled",
              "intl-1m"]

# One change from the library's choices at a time (name_sort.c, "measured choices").
ABLATION = {
    "default": {},
    "depth2": {"NS_MAX_DEPTH": "2"},
    "depth4": {"NS_MAX_DEPTH": "4"},
    "depth8": {"NS_MAX_DEPTH": "8", "NS_MAX_BLOCKS": "4096"},
    "sample-only": {"NS_PRIOR_WEIGHT": "0"},
    "tables-only": {"NS_PRIOR_WEIGHT": "1e9"},
    "sample16": {"NS_SAMPLE_SHARE": "16"},
    "introsort-buckets": {"NS_RADIX_ABOVE": "256"},
    "always-plan": {"NS_PLAN_MIN": "0"},
}
ABLATION_DATA = ["names-1m", "census-shuffled", "intl-1m"]


def exe(build, name):
    path = Path(build) / (name + (".exe" if sys.platform == "win32" else ""))
    if not path.exists():
        sys.exit(f"missing {path}: build first")
    return path


def call(cmd, env=None):
    print(" ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, env=env)


def datasets(args):
    data = Path(args.dir)
    data.mkdir(parents=True, exist_ok=True)
    call([sys.executable, ROOT / "benchmarks/datasets/capture_names.py", "1000000", "-o", data / "names-1m.txt"])
    call([sys.executable, ROOT / "benchmarks/datasets/census_name_lists.py", data])


def run(args):
    bench = exe(args.build, "speedup_bench_sort")
    data = Path(args.data)
    call([bench, "--min-log2", "10", "--max-log2", "24", "--reps", str(args.reps), "--names", data / "names-1m.txt",
          "--out", OUTPUTS / f"sort_{MACHINE}_{args.label}.csv"])
    for name in REAL_LISTS:
        call([bench, "--only", "names", "--reps", str(args.reps), "--names", data / f"{name}.txt",
              "--out", OUTPUTS / f"namesort_real_{name}_{MACHINE}_{args.label}.csv"])


def ablation(args):
    gcc, gxx = shutil.which("gcc"), shutil.which("g++")
    if not gcc or not gxx:
        sys.exit("ablation needs gcc and g++ on PATH")
    env = dict(os.environ)
    # MinGW: the matching libstdc++ DLL must come first (see docs/sorting/README.md)
    env["PATH"] = str(Path(gxx).parent) + os.pathsep + env.get("PATH", "")
    work = Path(args.build) / "ablation"
    work.mkdir(parents=True, exist_ok=True)
    lib = Path(args.build) / "libspeedup.a"
    inc = ["-I", ROOT / "include", "-I", ROOT / "src/algorithms/sort"]
    data = Path(args.data)
    for config, defines in ABLATION.items():
        obj = work / f"name_sort_{config}.o"
        bench = work / f"bench_{config}.exe"
        flags = [f"-D{k}={v}" for k, v in defines.items()]
        call([gcc, "-std=c11", "-O3", "-DNDEBUG", *flags, *inc, "-c", ROOT / "src/algorithms/sort/name_sort.c",
              "-o", obj], env)
        # the object comes before the library, so its name sort replaces the library's
        call([gxx, "-std=c++17", "-O3", "-DNDEBUG", *inc, ROOT / "benchmarks/sort/bench_sort.cpp", obj, lib,
              "-o", bench], env)
        for name in ABLATION_DATA:
            # plain MSD radix sort is timed in the same interleaved run: the baseline of every config
            call([bench, "--only", "names", "--algos", "name_sort,msd_radix", "--reps", str(args.reps),
                  "--names", data / f"{name}.txt",
                  "--out", OUTPUTS / f"namesort_ablation_{config}_{name}_{MACHINE}_gcc.csv"], env)


# 0.3.1 tuning: block and chunk sizes of bitonic merge sort, the bucket
# size of bitonic bucket sort (compile-time knobs of their sources).
TUNE031 = {
    "bms-block16": ("bitonic_merge", {"SU_BMS_BLOCK": "16"}),
    "bms-block32": ("bitonic_merge", {"SU_BMS_BLOCK": "32"}),
    "bms-block64": ("bitonic_merge", {"SU_BMS_BLOCK": "64"}),
    "bms-block128": ("bitonic_merge", {"SU_BMS_BLOCK": "128"}),
    "bms-block256": ("bitonic_merge", {"SU_BMS_BLOCK": "256"}),
    "bms-chunk8k": ("bitonic_merge", {"SU_BMS_CHUNK": "8192"}),
    "bms-chunk128k": ("bitonic_merge", {"SU_BMS_CHUNK": "131072"}),
    "bbs-target32": ("bitonic_bucket", {"SU_BBS_TARGET": "32", "SU_BBS_NETWORK_MAX": "256"}),
    "bbs-target64": ("bitonic_bucket", {"SU_BBS_TARGET": "64", "SU_BBS_NETWORK_MAX": "512"}),
    "bbs-target128": ("bitonic_bucket", {"SU_BBS_TARGET": "128", "SU_BBS_NETWORK_MAX": "1024"}),
    "bbs-target256": ("bitonic_bucket", {"SU_BBS_TARGET": "256", "SU_BBS_NETWORK_MAX": "2048"}),
    "bbs-target512": ("bitonic_bucket", {"SU_BBS_TARGET": "512", "SU_BBS_NETWORK_MAX": "4096"}),
    "bbs-target1024": ("bitonic_bucket", {"SU_BBS_TARGET": "1024", "SU_BBS_NETWORK_MAX": "8192"}),
}


def tune031(args):
    gcc, gxx = shutil.which("gcc"), shutil.which("g++")
    if not gcc or not gxx:
        sys.exit("tune031 needs gcc and g++ on PATH")
    env = dict(os.environ)
    env["PATH"] = str(Path(gxx).parent) + os.pathsep + env.get("PATH", "")
    work = Path(args.build) / "tune031"
    work.mkdir(parents=True, exist_ok=True)
    lib = Path(args.build) / "libspeedup.a"
    inc = ["-I", ROOT / "include", "-I", ROOT / "src/algorithms/sort"]
    only = args.only.split(",") if args.only else list(TUNE031)
    for name in only:
        algo, defines = TUNE031[name]
        flags = [f"-D{k}={v}" for k, v in defines.items()]
        objs = []
        for src in ("bitonic_merge_sort", "bitonic_bucket_sort"):
            obj = work / f"{src}_{name}.o"
            call([gcc, "-std=c11", "-O3", "-DNDEBUG", "-DSPEEDUP_SORT_HAVE_AVX2", *flags, *inc, "-c",
                  ROOT / f"src/algorithms/sort/{src}.c", "-o", obj], env)
            objs.append(obj)
        bench = work / f"bench_{name}.exe"
        # the objects come before the library, so their sorts replace the library's
        call([gxx, "-std=c++17", "-O3", "-DNDEBUG", "-DSPEEDUP_SORT_BENCH_HYBRIDS", *inc,
              ROOT / "benchmarks/sort/bench_sort.cpp", *objs, lib, "-o", bench], env)
        call([bench, "--only", "numeric", "--min-log2", "12", "--max-log2", "24", "--steps", "1",
              "--types", "u32,u64", "--dists", "uniform,skewed", "--algos", f"std::sort,{algo}",
              "--reps", str(args.reps), "--out", OUTPUTS / f"tune031_{name}_{MACHINE}_gcc.csv"], env)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("datasets")
    d.add_argument("dir")
    r = sub.add_parser("run")
    r.add_argument("--build", required=True)
    r.add_argument("--label", required=True)
    r.add_argument("--data", required=True)
    r.add_argument("--reps", type=int, default=7)
    a = sub.add_parser("ablation")
    a.add_argument("--build", required=True)
    a.add_argument("--data", required=True)
    a.add_argument("--reps", type=int, default=9)
    t = sub.add_parser("tune031")
    t.add_argument("--build", required=True)
    t.add_argument("--only", help="comma-separated variant names")
    t.add_argument("--reps", type=int, default=5)
    args = p.parse_args()
    {"datasets": datasets, "run": run, "ablation": ablation, "tune031": tune031}[args.cmd](args)


if __name__ == "__main__":
    main()
