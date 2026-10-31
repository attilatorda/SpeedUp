# Code Generation Pipeline

SpeedUp uses simple type-driven generation for algorithm specializations.

## Inputs
- `codegen/types.yaml` – list of scalar types

## Generator
```bash
python codegen/generate_specializations.py
```

## Output
- `src/algorithms/binary_search/generated/`

Next step: include generated units in CMake and CI validation.

## Name sort frequency tables
```bash
python codegen/name_tables/build_name_tables.py   # --offline: use cache/ only
```
Downloads US Census name data once into `codegen/name_tables/cache/`
(git-ignored) and writes `src/algorithms/sort/name_freq_tables.h` and
`benchmarks/datasets/names/*.tsv.gz`. The output is the same byte for byte on
every run.
