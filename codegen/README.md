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
