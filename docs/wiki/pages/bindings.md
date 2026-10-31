---
title: Language bindings
nav: Bindings
section: Project
order: 32
---

SpeedUp exposes a stable C ABI for bindings. Build it as a shared library
(`-DBUILD_SHARED_LIBS=ON`) and load it with your language's FFI.

## Status

| Tier | Languages |
|------|-----------|
| A (target) | C, Python, C#, Java |
| Experimental | Julia, Rust, Go, Swift, Kotlin, JavaScript/TypeScript, Scala, R, Zig |

Most bindings are stubs today. A binding counts as smoke-complete when it
loads the shared library and gets the expected result from
`speedup_binary_search_i64`.

## Python, through ctypes

```python
import ctypes
from ctypes import c_int64, POINTER

lib = ctypes.CDLL(path_to_speedup_dll)
lib.speedup_binary_search_i64.argtypes = [POINTER(c_int64), c_int64, c_int64]
lib.speedup_binary_search_i64.restype = c_int64

arr = (c_int64 * 5)(2, 4, 6, 8, 10)
assert lib.speedup_binary_search_i64(arr, 6, 5) == 2
```

The full smoke test is `bindings/python/smoke_test.py`.

## Native ports

Components that call back into user code are ported natively instead:
Bucketization exists in C++ and in Java 17 (`bindings/java/bucketization/`,
built with Maven). Other ports follow the cross-language specification in
`docs/bucketization/README.md`, which includes golden values every port must
reproduce.
