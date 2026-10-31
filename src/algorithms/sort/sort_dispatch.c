#include "speedup/algorithms/sort.h"
#include "speedup/backend/dispatch.h"
#include "bitonic_net.h"
#include "sort_keys.h"

static int g_simd_enabled = 1;

void speedup_sort_set_simd(int enabled) { g_simd_enabled = enabled != 0; }

#ifdef SPEEDUP_SORT_HAVE_AVX2
int speedup_cpu_has_avx2(void); /* src/core/cpu_features.c */

/* -1: not checked yet; 0 or 1 afterwards. */
static int g_avx2 = -1;

int speedup_internal_sort_use_avx2(void) {
    if (g_avx2 < 0) g_avx2 = speedup_cpu_has_avx2();
    return g_simd_enabled && g_avx2;
}
#else
int speedup_internal_sort_use_avx2(void) { return 0; }
#endif

/* Bitonic sort is the sort with a GPU path (OpenCL, loaded at run time).
 * SPEEDUP_BACKEND_FORCE_OPENCL uses it whenever a GPU is available;
 * SPEEDUP_BACKEND_AUTO from SU_GPU_MIN keys on, where the GPU wins even
 * with the copies to and from it (docs/sorting/README.md); the other
 * preferences stay on the CPU. Any GPU failure falls back to the CPU. */
#ifndef SU_GPU_MIN
#define SU_GPU_MIN ((size_t)1 << 16)
#endif

#ifdef SPEEDUP_SORT_HAVE_OPENCL
int speedup_internal_bitonic_opencl_u32(uint32_t* data, size_t n);
int speedup_internal_bitonic_opencl_u64(uint64_t* data, size_t n);

static int su_gpu_wanted(size_t n) {
    switch (speedup_get_backend_preference()) {
        case SPEEDUP_BACKEND_FORCE_OPENCL: return 1;
        case SPEEDUP_BACKEND_AUTO: return n >= SU_GPU_MIN;
        default: return 0;
    }
}
#endif

static void su_bitonic32(uint32_t* a, size_t n, int mode) {
    su_encode32(a, n, mode);
#ifdef SPEEDUP_SORT_HAVE_OPENCL
    if (n > 1 && su_gpu_wanted(n) && speedup_internal_bitonic_opencl_u32(a, n) == 0) {
        su_decode32(a, n, mode);
        return;
    }
#endif
#ifdef SPEEDUP_SORT_HAVE_AVX2
    if (speedup_internal_sort_use_avx2()) {
        speedup_internal_bitonic_avx2_u32(a, n);
    } else
#endif
    {
        speedup_internal_bitonic_scalar_u32(a, n);
    }
    su_decode32(a, n, mode);
}

static void su_bitonic64(uint64_t* a, size_t n, int mode) {
    su_encode64(a, n, mode);
#ifdef SPEEDUP_SORT_HAVE_OPENCL
    if (n > 1 && su_gpu_wanted(n) && speedup_internal_bitonic_opencl_u64(a, n) == 0) {
        su_decode64(a, n, mode);
        return;
    }
#endif
#ifdef SPEEDUP_SORT_HAVE_AVX2
    if (speedup_internal_sort_use_avx2()) {
        speedup_internal_bitonic_avx2_u64(a, n);
    } else
#endif
    {
        speedup_internal_bitonic_scalar_u64(a, n);
    }
    su_decode64(a, n, mode);
}

void speedup_bitonic_sort_u32(uint32_t* data, size_t n) { su_bitonic32(data, n, SU_KEY_UNSIGNED); }
void speedup_bitonic_sort_i32(int32_t* data, size_t n) { su_bitonic32((uint32_t*)data, n, SU_KEY_SIGNED); }
void speedup_bitonic_sort_f32(float* data, size_t n) { su_bitonic32((uint32_t*)data, n, SU_KEY_FLOAT); }
void speedup_bitonic_sort_u64(uint64_t* data, size_t n) { su_bitonic64(data, n, SU_KEY_UNSIGNED); }
void speedup_bitonic_sort_i64(int64_t* data, size_t n) { su_bitonic64((uint64_t*)data, n, SU_KEY_SIGNED); }
void speedup_bitonic_sort_f64(double* data, size_t n) { su_bitonic64((uint64_t*)data, n, SU_KEY_FLOAT); }
