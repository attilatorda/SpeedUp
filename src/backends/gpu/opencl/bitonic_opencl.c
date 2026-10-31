/* Bitonic sort on an OpenCL GPU.
 *
 * The network is the one of the CPU code (src/algorithms/sort/bitonic_net.h):
 * every comparator puts the smaller key at the lower index, a flip stage per
 * block size k, then half-cleaners. On the GPU the keys are padded to a
 * power of two N with the largest key (padding never moves, so it stays at
 * the end) and the network runs as:
 *   local_sort    each work-group sorts LB = 2 x LS keys in local memory:
 *                 all stages with k <= LB, in one launch;
 *   global_flip   the flip stage of a block size k > LB, one launch;
 *   global_half   a half-cleaner with distance j >= LB, one launch;
 *   local_merge   all half-cleaners with j < LB of one k, in local memory.
 * For N = 2^24 keys and LB = 1024 that is 1 + 14 local merges and 105
 * global stages. Keys are uploaded, sorted and read back on every call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opencl_api.h"

static const char* SU_OCL_BITONIC_SOURCE =
    "#ifdef SU_WIDE\n"
    "typedef ulong key_t;\n"
    "#else\n"
    "typedef uint key_t;\n"
    "#endif\n"
    "#define LB (2 * LS)\n"
    "inline void su_cswap(__local key_t* s, uint i, uint l) {\n"
    "    key_t x = s[i], y = s[l];\n"
    "    s[i] = min(x, y);\n"
    "    s[l] = max(x, y);\n"
    "}\n"
    "__kernel void su_fill(__global key_t* a, ulong from, key_t value) {\n"
    "    a[from + get_global_id(0)] = value;\n"
    "}\n"
    "__kernel __attribute__((reqd_work_group_size(LS, 1, 1))) void su_local_sort(__global key_t* a) {\n"
    "    __local key_t s[LB];\n"
    "    uint t = get_local_id(0);\n"
    "    size_t base = get_group_id(0) * (size_t)LB;\n"
    "    s[t] = a[base + t];\n"
    "    s[t + LS] = a[base + t + LS];\n"
    "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    "    for (uint lk = 1; (1u << lk) <= LB; lk++) {\n"
    "        uint i = ((t >> (lk - 1)) << lk) | (t & ((1u << (lk - 1)) - 1));\n"
    "        su_cswap(s, i, i ^ ((1u << lk) - 1));\n"
    "        barrier(CLK_LOCAL_MEM_FENCE);\n"
    "        for (int lj = (int)lk - 2; lj >= 0; lj--) {\n"
    "            uint i2 = ((t >> lj) << (lj + 1)) | (t & ((1u << lj) - 1));\n"
    "            su_cswap(s, i2, i2 + (1u << lj));\n"
    "            barrier(CLK_LOCAL_MEM_FENCE);\n"
    "        }\n"
    "    }\n"
    "    a[base + t] = s[t];\n"
    "    a[base + t + LS] = s[t + LS];\n"
    "}\n"
    "__kernel void su_global_flip(__global key_t* a, uint lk) {\n"
    "    ulong p = get_global_id(0);\n"
    "    ulong i = ((p >> (lk - 1)) << lk) | (p & ((1ul << (lk - 1)) - 1));\n"
    "    ulong l = i ^ ((1ul << lk) - 1);\n"
    "    key_t x = a[i], y = a[l];\n"
    "    a[i] = min(x, y);\n"
    "    a[l] = max(x, y);\n"
    "}\n"
    "__kernel void su_global_half(__global key_t* a, uint lj) {\n"
    "    ulong p = get_global_id(0);\n"
    "    ulong i = ((p >> lj) << (lj + 1)) | (p & ((1ul << lj) - 1));\n"
    "    ulong l = i + (1ul << lj);\n"
    "    key_t x = a[i], y = a[l];\n"
    "    a[i] = min(x, y);\n"
    "    a[l] = max(x, y);\n"
    "}\n"
    "__kernel __attribute__((reqd_work_group_size(LS, 1, 1))) void su_local_merge(__global key_t* a) {\n"
    "    __local key_t s[LB];\n"
    "    uint t = get_local_id(0);\n"
    "    size_t base = get_group_id(0) * (size_t)LB;\n"
    "    s[t] = a[base + t];\n"
    "    s[t + LS] = a[base + t + LS];\n"
    "    barrier(CLK_LOCAL_MEM_FENCE);\n"
    "    for (int lj = LOG_LS; lj >= 0; lj--) {\n"
    "        uint i = ((t >> lj) << (lj + 1)) | (t & ((1u << lj) - 1));\n"
    "        su_cswap(s, i, i + (1u << lj));\n"
    "        barrier(CLK_LOCAL_MEM_FENCE);\n"
    "    }\n"
    "    a[base + t] = s[t];\n"
    "    a[base + t + LS] = s[t + LS];\n"
    "}\n";

enum { SU_K_FILL, SU_K_LOCAL_SORT, SU_K_FLIP, SU_K_HALF, SU_K_LOCAL_MERGE, SU_K_COUNT };
static const char* SU_KERNEL_NAMES[SU_K_COUNT] = {"su_fill", "su_local_sort", "su_global_flip", "su_global_half",
                                                  "su_local_merge"};

typedef struct su_bitonic_program {
    int state; /* -1 not built, 0 failed, 1 ready */
    cl_program program;
    cl_kernel kernels[SU_K_COUNT];
    size_t ls; /* work-group size; LB = 2 * ls keys per local block */
} su_bitonic_program;

static su_bitonic_program g_prog[2] = {{-1, NULL, {NULL}, 0}, {-1, NULL, {NULL}, 0}};
static cl_mem g_buffer = NULL;
static size_t g_capacity = 0;

static unsigned su_log2(size_t v) {
    unsigned r = 0;
    while (v > 1) { v >>= 1; r++; }
    return r;
}

static int su_build(const su_ocl* ocl, int wide) {
    su_bitonic_program* p = &g_prog[wide];
    if (p->state >= 0) return p->state;
    p->state = 0;
    size_t ls = 512;
    while (ls > 64 && ls > ocl->max_work_group) ls >>= 1;
    char options[128];
    snprintf(options, sizeof options, "-DLS=%u -DLOG_LS=%u%s", (unsigned)ls, su_log2(ls), wide ? " -DSU_WIDE" : "");
    cl_int err = 0;
    p->program = ocl->CreateProgramWithSource(ocl->context, 1, &SU_OCL_BITONIC_SOURCE, NULL, &err);
    if (err != SU_CL_SUCCESS) return 0;
    if (ocl->BuildProgram(p->program, 1, &ocl->device, options, NULL, NULL) != SU_CL_SUCCESS) {
        if (getenv("SPEEDUP_OPENCL_DEBUG")) {
            char log[8192] = {0};
            ocl->GetProgramBuildInfo(p->program, ocl->device, SU_CL_PROGRAM_BUILD_LOG, sizeof log - 1, log, NULL);
            fprintf(stderr, "speedup: OpenCL build failed:\n%s\n", log);
        }
        return 0;
    }
    for (int k = 0; k < SU_K_COUNT; k++) {
        p->kernels[k] = ocl->CreateKernel(p->program, SU_KERNEL_NAMES[k], &err);
        if (err != SU_CL_SUCCESS) return 0;
    }
    p->ls = ls;
    p->state = 1;
    return 1;
}

static int su_launch(const su_ocl* ocl, cl_kernel kernel, size_t global, size_t local) {
    return ocl->EnqueueNDRangeKernel(ocl->queue, kernel, 1, NULL, &global, local ? &local : NULL, 0, NULL, NULL) ==
           SU_CL_SUCCESS;
}

/* Sorts n unsigned keys of 4 (wide = 0) or 8 (wide = 1) bytes on the GPU.
 * Returns 0 on success; -1 if the GPU path is unavailable or failed, in
 * which case data is unchanged and the caller sorts on the CPU. */
static int su_bitonic_opencl(void* data, size_t n, int wide) {
    const su_ocl* ocl = speedup_internal_ocl();
    if (!ocl || !su_build(ocl, wide)) return -1;
    const su_bitonic_program* p = &g_prog[wide];
    const size_t key = wide ? 8 : 4, lb = 2 * p->ls;
    size_t total = lb;
    while (total < n) total <<= 1;
    if ((cl_ulong)(total * key) > ocl->global_mem / 2) return -1;
    if (total * key > g_capacity) {
        if (g_buffer) ocl->ReleaseMemObject(g_buffer);
        cl_int err = 0;
        g_buffer = ocl->CreateBuffer(ocl->context, SU_CL_MEM_READ_WRITE, total * key, NULL, &err);
        if (err != SU_CL_SUCCESS || !g_buffer) {
            g_buffer = NULL;
            g_capacity = 0;
            return -1;
        }
        g_capacity = total * key;
    }
    int ok = ocl->EnqueueWriteBuffer(ocl->queue, g_buffer, 0, 0, n * key, data, 0, NULL, NULL) == SU_CL_SUCCESS;
    if (ok && total > n) {
        cl_ulong from = n;
        cl_ulong max64 = ~(cl_ulong)0;
        cl_uint max32 = ~(cl_uint)0;
        cl_kernel k = p->kernels[SU_K_FILL];
        ok = ocl->SetKernelArg(k, 0, sizeof g_buffer, &g_buffer) == SU_CL_SUCCESS &&
             ocl->SetKernelArg(k, 1, sizeof from, &from) == SU_CL_SUCCESS &&
             ocl->SetKernelArg(k, 2, key, wide ? (const void*)&max64 : (const void*)&max32) == SU_CL_SUCCESS &&
             su_launch(ocl, k, total - n, 0);
    }
    for (int k = 0; ok && k < SU_K_COUNT; k++) {
        if (k != SU_K_FILL) ok = ocl->SetKernelArg(p->kernels[k], 0, sizeof g_buffer, &g_buffer) == SU_CL_SUCCESS;
    }
    ok = ok && su_launch(ocl, p->kernels[SU_K_LOCAL_SORT], total / 2, p->ls);
    const size_t group = total / 2 < 256 ? total / 2 : 256; /* global stages: any power of two divides */
    for (cl_uint lk = su_log2(lb) + 1; ok && ((size_t)1 << lk) <= total; lk++) {
        ok = ocl->SetKernelArg(p->kernels[SU_K_FLIP], 1, sizeof lk, &lk) == SU_CL_SUCCESS &&
             su_launch(ocl, p->kernels[SU_K_FLIP], total / 2, group);
        for (cl_uint lj = lk - 2; ok && ((size_t)1 << lj) >= lb; lj--) {
            ok = ocl->SetKernelArg(p->kernels[SU_K_HALF], 1, sizeof lj, &lj) == SU_CL_SUCCESS &&
                 su_launch(ocl, p->kernels[SU_K_HALF], total / 2, group);
        }
        ok = ok && su_launch(ocl, p->kernels[SU_K_LOCAL_MERGE], total / 2, p->ls);
    }
    ok = ok && ocl->EnqueueReadBuffer(ocl->queue, g_buffer, SU_CL_TRUE, 0, n * key, data, 0, NULL, NULL) ==
                   SU_CL_SUCCESS;
    if (!ok) {
        ocl->Finish(ocl->queue);
        speedup_internal_ocl_disable();
        return -1;
    }
    return 0;
}

int speedup_internal_bitonic_opencl_u32(uint32_t* data, size_t n) { return su_bitonic_opencl(data, n, 0); }
int speedup_internal_bitonic_opencl_u64(uint64_t* data, size_t n) { return su_bitonic_opencl(data, n, 1); }
