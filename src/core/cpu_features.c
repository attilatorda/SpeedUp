#include <stdint.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#include <immintrin.h>
#define SPEEDUP_X86 1
static void speedup_cpuid(int leaf, int sub, int r[4]) { __cpuidex(r, leaf, sub); }
static uint64_t speedup_xgetbv0(void) { return _xgetbv(0); }
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define SPEEDUP_X86 1
static void speedup_cpuid(int leaf, int sub, int r[4]) {
    unsigned a, b, c, d;
    __cpuid_count((unsigned)leaf, (unsigned)sub, a, b, c, d);
    r[0] = (int)a; r[1] = (int)b; r[2] = (int)c; r[3] = (int)d;
}
static uint64_t speedup_xgetbv0(void) {
    uint32_t lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return ((uint64_t)hi << 32) | lo;
}
#endif

/* AVX2 needs the CPU flag and an OS that saves the YMM registers. */
int speedup_cpu_has_avx2(void) {
#ifdef SPEEDUP_X86
    int r[4];
    speedup_cpuid(0, 0, r);
    if (r[0] < 7) return 0;
    speedup_cpuid(1, 0, r);
    int osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
    if (!osxsave || !avx || (speedup_xgetbv0() & 6) != 6) return 0;
    speedup_cpuid(7, 0, r);
    return (r[1] >> 5) & 1;
#else
    return 0;
#endif
}

int speedup_cpu_has_neon(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return 1;
#else
    return 0;
#endif
}
