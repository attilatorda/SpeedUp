cmake_minimum_required(VERSION 3.20)
project(SpeedUp VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(SPEEDUP_ENABLE_CUDA "Enable CUDA backend" OFF)
option(SPEEDUP_ENABLE_OPENCL "Enable OpenCL backend" ON)
option(SPEEDUP_ENABLE_ASM "Enable ASM optimized paths" ON)
option(SPEEDUP_BUILD_BENCHMARKS "Build benchmark targets" ON)
option(SPEEDUP_BUILD_EXAMPLES "Build example programs" ON)

if(SPEEDUP_ENABLE_ASM)
    find_program(NASM_EXECUTABLE nasm)
    if(NOT NASM_EXECUTABLE)
        message(WARNING "NASM not found. Disabling SPEEDUP_ENABLE_ASM optimized paths.")
        set(SPEEDUP_ENABLE_ASM OFF)
    endif()
endif()

if(SPEEDUP_ENABLE_ASM)
    message(STATUS "SPEEDUP_ENABLE_ASM=ON (NASM detected)")
else()
    message(STATUS "SPEEDUP_ENABLE_ASM=OFF (fallback to C/C++)")
endif()

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
    src/algorithms/binary_search/binary_search_cpu.c
    src/algorithms/binary_search/binary_search_dispatch.c
    src/backends/gpu/common/gpu_backend_common.c
    src/backends/gpu/cuda/cuda_runtime_check.c
    src/backends/gpu/opencl/opencl_runtime_check.c
)

target_include_directories(speedup
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/algorithms/binary_search
)

add_executable(speedup_binary_search_test tests/unit/test_binary_search.c)
target_link_libraries(speedup_binary_search_test PRIVATE speedup)

# Bucketization: header-only C++17 interface (include/speedup/bucketization.hpp)
add_library(speedup_bucketization INTERFACE)
add_library(speedup::bucketization ALIAS speedup_bucketization)
target_include_directories(speedup_bucketization INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(speedup_bucketization INTERFACE cxx_std_17)

add_executable(speedup_bucketization_test tests/unit/test_bucketization.cpp)
target_link_libraries(speedup_bucketization_test PRIVATE speedup_bucketization)

if(SPEEDUP_BUILD_BENCHMARKS)
    add_executable(speedup_benchmark_bucketization benchmarks/core/benchmark_bucketization.cpp)
    target_link_libraries(speedup_benchmark_bucketization PRIVATE speedup_bucketization)

    # Bucketization against real file manager stacks (optional dependencies).
    # KDE Dolphin: Qt 6 QCollator on ICU, Dolphin's comparison and parallel merge sort.
    find_package(Qt6 QUIET COMPONENTS Core Concurrent)
    find_package(ICU QUIET COMPONENTS uc i18n)
    if(Qt6_FOUND AND ICU_FOUND)
        add_executable(speedup_benchmark_dolphin_stack benchmarks/kde_stack/benchmark_dolphin_stack.cpp)
        target_link_libraries(speedup_benchmark_dolphin_stack PRIVATE
            speedup_bucketization Qt6::Core Qt6::Concurrent ICU::uc ICU::i18n)
        add_executable(speedup_verify_icu_key benchmarks/kde_stack/verify_icu_key.cpp)
        target_link_libraries(speedup_verify_icu_key PRIVATE Qt6::Core Qt6::Concurrent ICU::uc ICU::i18n)
        add_executable(speedup_find_comparator_cycles benchmarks/kde_stack/find_comparator_cycles.cpp)
        target_link_libraries(speedup_find_comparator_cycles PRIVATE Qt6::Core Qt6::Concurrent)
        # PCManFM-Qt, KDE file dialogs (KIO) and Krusader on their own sort paths.
        add_executable(speedup_benchmark_qt_family benchmarks/qt_stack/benchmark_qt_family.cpp)
        target_link_libraries(speedup_benchmark_qt_family PRIVATE speedup_bucketization Qt6::Core ICU::uc ICU::i18n)
        add_executable(speedup_krusader_descending_check benchmarks/qt_stack/krusader_descending_check.cpp)
        target_compile_definitions(speedup_krusader_descending_check PRIVATE _GLIBCXX_DEBUG)
        target_link_libraries(speedup_krusader_descending_check PRIVATE Qt6::Core)
    else()
        message(STATUS "Qt 6 or ICU not found: skipping speedup_benchmark_dolphin_stack")
    endif()
    # GNOME family (Nautilus, Thunar, Nemo, Caja): GLib collation keys and GSequence.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLIB QUIET IMPORTED_TARGET glib-2.0)
    endif()
    if(GLIB_FOUND)
        add_executable(speedup_benchmark_glib_stack benchmarks/gnome_stack/benchmark_glib_stack.cpp)
        target_link_libraries(speedup_benchmark_glib_stack PRIVATE speedup_bucketization PkgConfig::GLIB)
        # Thunar, Nemo, Caja on their g_sequence_sort paths; Nautilus on GTK 4 when available.
        add_executable(speedup_benchmark_gnome_family benchmarks/gnome_stack/benchmark_gnome_family.cpp)
        target_link_libraries(speedup_benchmark_gnome_family PRIVATE speedup_bucketization PkgConfig::GLIB)
        pkg_check_modules(GTK4 QUIET IMPORTED_TARGET gtk4)
        if(GTK4_FOUND)
            target_compile_definitions(speedup_benchmark_gnome_family PRIVATE SPEEDUP_HAVE_GTK4)
            target_link_libraries(speedup_benchmark_gnome_family PRIVATE PkgConfig::GTK4)
        else()
            message(STATUS "GTK 4 not found: speedup_benchmark_gnome_family runs without Nautilus")
        endif()
    else()
        message(STATUS "GLib not found: skipping speedup_benchmark_glib_stack")
    endif()
    # nnn, and C++ ports of yazi's and lf's comparisons (their native sorts:
    # benchmarks/terminal_stack/yazi_bench (Rust) and lf_bench (Go)).
    add_executable(speedup_benchmark_terminal_family benchmarks/terminal_stack/benchmark_terminal_family.cpp)
    target_link_libraries(speedup_benchmark_terminal_family PRIVATE speedup_bucketization)
    # Does strcmp () on strxfrm () order like strcoll ()? nnn's key relies on it.
    add_executable(speedup_check_strxfrm benchmarks/terminal_stack/check_strxfrm.c)
    # Godot editor's FileSystem dock sort (sort_file_info_list), extracted.
    add_executable(speedup_benchmark_godot benchmarks/godot_stack/benchmark_godot.cpp)
    target_link_libraries(speedup_benchmark_godot PRIVATE speedup_bucketization)
endif()

if(SPEEDUP_BUILD_EXAMPLES)
    add_executable(speedup_example_file_browser examples/bucketization/file_browser.cpp)
    target_link_libraries(speedup_example_file_browser PRIVATE speedup_bucketization)
endif()

if(SPEEDUP_BUILD_BENCHMARKS)
    # Binary search study (benchmarks/binary_search): every variant, L1 to DRAM.
    add_executable(speedup_bench_bsearch benchmarks/binary_search/bench_bsearch.cpp)
    target_include_directories(speedup_bench_bsearch PRIVATE src/algorithms/binary_search)
    target_link_libraries(speedup_bench_bsearch PRIVATE speedup)
endif()

if(WIN32 AND SPEEDUP_BUILD_BENCHMARKS AND SPEEDUP_ENABLE_ASM AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    enable_language(ASM_NASM)

    add_library(speedup_bsearch_win64_asm OBJECT
        src/backends/cpu/x86_64/binary_search_win64.asm
    )

    target_sources(speedup_bench_bsearch PRIVATE $<TARGET_OBJECTS:speedup_bsearch_win64_asm>)
    target_compile_definitions(speedup_bench_bsearch PRIVATE SPEEDUP_BENCH_HAVE_ASM)

    # The first prototype's harness (see legacy/README.md).
    add_executable(speedup_benchmark_win64
        benchmarks/core/benchmark_win.c
        $<TARGET_OBJECTS:speedup_bsearch_win64_asm>
    )
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(speedup_benchmark_win64 PRIVATE -mavx2)
    endif()
endif()

enable_testing()
add_test(NAME speedup_binary_search_test COMMAND speedup_binary_search_test)
add_test(NAME speedup_bucketization_test COMMAND speedup_bucketization_test)
