# Contributing to SpeedUp

## Development flow
1. Create a feature branch.
2. Keep commits focused and descriptive.
3. Run build + tests before opening a PR.

## Build and test
```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Performance changes
- Include benchmark evidence (`BENCHMARKS.md` references).
- Document hardware and backend mode.

## Style
- C API remains stable and backward-compatible.
- Keep fallback paths working when optional accelerators are unavailable.
