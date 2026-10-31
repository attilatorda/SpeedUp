---
title: Contributing
section: Project
order: 33
---

## Workflow

1. Create a feature branch.
2. Keep commits focused and descriptive.
3. Build and run the tests before opening a pull request.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Rules

- The C API stays stable and backward-compatible.
- Every accelerated path keeps a working fallback when the accelerator is
  missing.
- Tests compare against a reference implementation (`qsort`, a linear scan),
  not against hand-picked expected values.
- Performance changes come with benchmark evidence: the harness, the
  hardware, the compiler, and the CSV in `benchmarks/outputs/`.

## Editing this wiki

The pages are Markdown files in `docs/wiki/pages/`. Each starts with a small
header naming its title, sidebar section and order. Rebuild the site with:

```bash
python docs/wiki/build_wiki.py
```

This writes `docs/wiki/index.html`, a single self-contained page. The
changelog page is read from `CHANGELOG.md`.
