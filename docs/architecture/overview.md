# SpeedUp Architecture

Dispatch order: CUDA -> OpenCL -> CPU optimized -> C fallback.

Tuning surfaces:
- compile-time defaults via CMake
- runtime hints via C API (threads/cache/backend preference)
