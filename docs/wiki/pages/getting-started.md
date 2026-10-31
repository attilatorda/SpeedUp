---
title: Getting started
section: Overview
order: 2
---

## Build and test

SpeedUp builds with CMake 3.20 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The test suite has four programs: binary search, Bucketization, the sorts
(C) and the sorting C++ layer. Optional dependencies (NASM, Qt 6, ICU, GLib,
GTK 4) only add benchmarks; when they are missing, CMake skips those targets.

For the language bindings, build a shared library:

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared --config Release
```

## Use it from CMake

```cmake
add_subdirectory(SpeedUp)
target_link_libraries(my_app PRIVATE speedup)                 # C ABI: search, sorts
target_link_libraries(my_app PRIVATE speedup::bucketization)  # header-only C++17
```

## A first program

```c
#include <stdint.h>
#include "speedup/api.h"

int main(void) {
    speedup_init();
    int64_t a[5] = {2, 4, 6, 8, 10};
    int64_t idx = speedup_binary_search_i64(a, 6, 5);   /* 2 */

    double v[4] = {3.5, -1.0, 2.0, 0.0};
    speedup_radix_sort_f64(v, 4, NULL);                  /* -1, 0, 2, 3.5 */
    return idx == 2 ? 0 : 1;
}
```

## MinGW on Windows

If a test program exits with code `0xc0000139` (entry point not found), the
loader picked up another toolchain's `libstdc++` DLL. Put the bin folder of the
GCC that built SpeedUp first in `PATH` before running the tests.
