# SpeedUp Bindings

SpeedUp exposes a stable C ABI intended for multi-language bindings.

## Maturity tiers
- Tier A (target): C, Python, C#, Java
- Experimental: Julia, Rust, Go, Swift, Kotlin, JS/TS, Scala, R, Zig

## Native implementations
Some components are implemented natively per language instead of through the
C ABI, because they call back into user code (comparators, key functions):
- Bucketization: C++ (`include/speedup/bucketization.hpp`) and Java
  (`bindings/java/bucketization`). Other ports follow the specification in
  `docs/bucketization/README.md`.

## Smoke-complete definition
- Binding can load SpeedUp shared library.
- Binding can call `speedup_binary_search_i64` and validate expected output.
