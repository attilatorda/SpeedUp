# SpeedUp Bindings

SpeedUp exposes a stable C ABI intended for multi-language bindings.

## Maturity tiers
- Tier A (target): C, Python, C#, Java
- Experimental: Julia, Rust, Go, Swift, Kotlin, JS/TS, Scala, R, Zig

## Smoke-complete definition
- Binding can load SpeedUp shared library.
- Binding can call `speedup_binary_search_i64` and validate expected output.
