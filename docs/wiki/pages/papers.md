---
title: Papers
section: Project
order: 31
---

Three studies, written for arXiv. Every figure, table and number in them is
generated from the benchmark CSVs by a script, so they can be rebuilt after
any re-run.

| Paper | Topic | Source |
|-------|-------|--------|
| What makes binary search fast, from L1 to DRAM | branchless search, prefetching, interleaved batches, a benchmark pitfall | `docs/papers/binary-search/` |
| Bucketization: comparison-free re-sorting of multi-column views | the Bucketization design, measured in ten file managers and the Godot editor | `docs/papers/bucketization/` |
| Name sort: bucketing person names by character frequency | name sort, its ablation and real name lists; radix, bucket and bitonic sort | `docs/papers/sorting/` |

## Building them

They need a LaTeX distribution with `latexmk`.

```bash
python docs/papers/build_papers.py                 # all PDFs
python docs/papers/build_papers.py --check-arxiv   # arXiv tarballs, rebuilt with pdflatex only
```

The inputs come from `run_bsearch_benchmarks.py analyze`,
`bucketization_paper_numbers.py` and `sorting_paper_numbers.py` in
`benchmarks/scripts/`.
