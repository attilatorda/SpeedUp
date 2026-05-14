cmake_minimum_required(VERSION 3.20)
project(SpeedUp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(SPEEDUP_ENABLE_CUDA "Enable CUDA backend" OFF)
option(SPEEDUP_ENABLE_OPENCL "Enable OpenCL backend" ON)
option(SPEEDUP_ENABLE_ASM "Enable ASM optimized paths" ON)
option(SPEEDUP_BUILD_BENCHMARKS "Build benchmark targets" ON)

set(SPEEDUP_DEFAULT_THREADS 0 CACHE STRING "0 means auto-detect")
set(SPEEDUP_DEFAULT_L1_BYTES 32768 CACHE STRING "Default L1 cache bytes hint")
set(SPEEDUP_DEFAULT_L2_BYTES 262144 CACHE STRING "Default L2 cache bytes hint")
set(SPEEDUP_DEFAULT_L3_BYTES 8388608 CACHE STRING "Default L3 cache bytes hint")

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/speedup/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/speedup/config.h
    @ONLY
)

add_library(speedup
    src/core/init.c
    src/core/dispatch.c
    src/core/cpu_features.c
    src/algorithms/binary_search/binary_search_ref.c
    src/algorithms/binary_search/binary_search_dispatch.c
    src/backends/gpu/common/gpu_backend_common.c
    src/backends/gpu/cuda/cuda_runtime_check.c
    src/backends/gpu/opencl/opencl_runtime_check.c
)

target_include_directories(speedup
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

add_executable(speedup_smoke tests/unit/test_binary_search.c)
target_link_libraries(speedup_smoke PRIVATE speedup)

if(WIN32 AND SPEEDUP_BUILD_BENCHMARKS AND SPEEDUP_ENABLE_ASM)
    enable_language(ASM_NASM)

    add_library(speedup_bsearch_win64_asm OBJECT
        src/backends/cpu/x86_64/binary_search_win64.asm
    )

    add_executable(speedup_benchmark_win64
        benchmarks/core/benchmark_win.c
        $<TARGET_OBJECTS:speedup_bsearch_win64_asm>
    )
endif()

enable_testing()
add_test(NAME speedup_smoke COMMAND speedup_smoke)
