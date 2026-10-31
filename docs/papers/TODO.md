# Papers and benchmarks: to do

State on 2026-10-27, branch `papers-and-benchmarks` (not pushed). Done so far:
the prototype folder `D:\Developer\SpeedUp` is merged (see `legacy/README.md`),
the binary-search study and library change are in, the Bucketization
benchmarks were re-run on Windows, and both papers build and pass the arXiv
check.

## Manual steps

- [ ] Read the PDFs once more yourself:
      `docs/papers/binary-search/main.pdf`, `docs/papers/bucketization/main.pdf`,
      `docs/papers/sorting/main.pdf` (added 2026-10-31: name sort and the
      numeric sorts).
- [ ] Push the branch and open a pull request.
- [ ] Submit to arXiv. Build the packages first:
      `python docs/papers/build_papers.py --check-arxiv`
      (writes `docs/papers/<paper>/arxiv/<paper>.tar.gz`, not tracked by git).
  - [ ] Binary search: primary cs.DS, cross-list cs.PF.
  - [ ] Bucketization: primary cs.DS, cross-list cs.DB (cs.HC optional).
  - [ ] Sorting (name sort): primary cs.DS, cross-list cs.PF.
  - [ ] A first cs.DS submission may need an endorsement from an arXiv author.
- [ ] After arXiv: add the arXiv IDs to `CITATION.cff`, `README.md` and the
      papers' Availability sections.
- [ ] Release 0.3.0 (CHANGELOG entry is "unreleased"): date it, tag it.

## Optional measurements

- [ ] lf's native Go benchmark on Windows: needs Go 1.22 or newer (Go 1.15 is
      installed), then
      `run_bucketization_benchmarks.py --build <build> --only-projects --quick --tag windows_ryzen2600x`.
- [ ] Binary search, 1–16 MiB: separate runs differ by up to 1.48× (GCC) and
      2.68× (MSVC), probably physical page placement. A firmer result needs
      several runs with fresh allocations, or large pages.
- [ ] Binary search on another CPU (Intel, ARM64) and with large pages.
- [ ] Bucketization on a bare-metal Linux machine (the reference runs are from
      a 4-vCPU cloud container); the Qt/GLib stacks need Linux.

## Possible follow-ups

- [ ] nnn's name column on Windows: use a comparator-only sort key there,
      since `strxfrm()` does not order like `strcoll()` in the Windows C
      runtime (`speedup_check_strxfrm`).
- [ ] Report the UCRT `strxfrm`/`strcoll` disagreement to Microsoft
      (example: `x-terminal-emulator` vs `xterm` in `en_US.UTF-8`).
- [ ] Bucketization: the first bucket cannot be smaller than about n/2048
      elements (sample granularity); a second sampling round inside it would
      make the first page cheaper above ~130,000 elements.
- [ ] Library: an Eytzinger-layout search for callers that can store their
      data in that order (fastest single search beyond L2).

## Rebuilding everything

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
python benchmarks/scripts/run_bsearch_benchmarks.py run --build build --label gcc   # ~1 h per compiler
python benchmarks/scripts/run_bsearch_benchmarks.py analyze
python benchmarks/scripts/bucketization_paper_numbers.py
python benchmarks/scripts/run_sort_benchmarks.py datasets <data>          # sorting study, ~2 h in all
python benchmarks/scripts/run_sort_benchmarks.py run --build build --label gcc --data <data>
python benchmarks/scripts/run_sort_benchmarks.py ablation --build build --data <data>
python benchmarks/scripts/sorting_paper_numbers.py
python docs/papers/build_papers.py --check-arxiv
```
