/* Differential test: every sort against qsort.
 * Numeric sorts: every size 0..1025 on random, few-unique, sorted, reversed
 * and constant data, special floating-point values (both NaN signs, NaN
 * payloads, +-0, +-inf, subnormals), large arrays, with and without a
 * scratch buffer, with the AVX2 kernels on and off. Results must match the
 * reference bit for bit.
 * Name sort: person-like names, lowercase and mixed case, empty and
 * one-byte names, UTF-8, embedded NUL bytes, long shared prefixes, all
 * names equal; byte order and ASCII-caseless order; small and large n. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "speedup/api.h"

static int failures = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            if (failures < 20) {                          \
                fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
                fprintf(stderr, __VA_ARGS__);             \
                fprintf(stderr, "\n");                    \
            }                                             \
            failures++;                                   \
        }                                                 \
    } while (0)

static uint64_t rng_state = 42;
static uint64_t next_random(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ---- reference order ---------------------------------------------------- */

/* IEEE 754 totalOrder through the bit patterns, written independently of
 * the library's sort_keys.h. */
static uint64_t total_key64(uint64_t bits) {
    return (bits >> 63) ? ~bits : bits | 0x8000000000000000ULL;
}
static uint32_t total_key32(uint32_t bits) {
    return (bits >> 31) ? ~bits : bits | 0x80000000U;
}

static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return (x > y) - (x < y);
}
static int cmp_i32(const void* a, const void* b) {
    int32_t x = *(const int32_t*)a, y = *(const int32_t*)b;
    return (x > y) - (x < y);
}
static int cmp_f32(const void* a, const void* b) {
    uint32_t x, y;
    memcpy(&x, a, 4); memcpy(&y, b, 4);
    x = total_key32(x); y = total_key32(y);
    return (x > y) - (x < y);
}
static int cmp_u64(const void* a, const void* b) {
    uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
    return (x > y) - (x < y);
}
static int cmp_i64(const void* a, const void* b) {
    int64_t x = *(const int64_t*)a, y = *(const int64_t*)b;
    return (x > y) - (x < y);
}
static int cmp_f64(const void* a, const void* b) {
    uint64_t x, y;
    memcpy(&x, a, 8); memcpy(&y, b, 8);
    x = total_key64(x); y = total_key64(y);
    return (x > y) - (x < y);
}

/* ---- numeric sorts -------------------------------------------------------- */

typedef enum { W32, W64 } width_t;

typedef struct sorter {
    const char* name;
    width_t width;
    int (*cmp)(const void*, const void*);
    void (*with_scratch)(void* data, size_t n, void* scratch);
    void (*no_scratch)(void* data, size_t n);
} sorter;

#define WRAP_SCRATCH(FN, T) \
    static void FN##_w(void* d, size_t n, void* s) { FN((T*)d, n, s); }
#define WRAP_PLAIN(FN, T) \
    static void FN##_w(void* d, size_t n) { FN((T*)d, n); }

WRAP_SCRATCH(speedup_radix_sort_u32, uint32_t)
WRAP_SCRATCH(speedup_radix_sort_i32, int32_t)
WRAP_SCRATCH(speedup_radix_sort_f32, float)
WRAP_SCRATCH(speedup_radix_sort_u64, uint64_t)
WRAP_SCRATCH(speedup_radix_sort_i64, int64_t)
WRAP_SCRATCH(speedup_radix_sort_f64, double)
WRAP_SCRATCH(speedup_bucket_sort_u64, uint64_t)
WRAP_SCRATCH(speedup_bucket_sort_i64, int64_t)
WRAP_SCRATCH(speedup_bucket_sort_f64, double)
WRAP_SCRATCH(speedup_bitonic_merge_sort_u32, uint32_t)
WRAP_SCRATCH(speedup_bitonic_merge_sort_i32, int32_t)
WRAP_SCRATCH(speedup_bitonic_merge_sort_f32, float)
WRAP_SCRATCH(speedup_bitonic_merge_sort_u64, uint64_t)
WRAP_SCRATCH(speedup_bitonic_merge_sort_i64, int64_t)
WRAP_SCRATCH(speedup_bitonic_merge_sort_f64, double)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_u32, uint32_t)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_i32, int32_t)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_f32, float)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_u64, uint64_t)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_i64, int64_t)
WRAP_SCRATCH(speedup_bitonic_bucket_sort_f64, double)
WRAP_PLAIN(speedup_bitonic_sort_u32, uint32_t)
WRAP_PLAIN(speedup_bitonic_sort_i32, int32_t)
WRAP_PLAIN(speedup_bitonic_sort_f32, float)
WRAP_PLAIN(speedup_bitonic_sort_u64, uint64_t)
WRAP_PLAIN(speedup_bitonic_sort_i64, int64_t)
WRAP_PLAIN(speedup_bitonic_sort_f64, double)

static const sorter SORTERS[] = {
    {"radix_u32", W32, cmp_u32, speedup_radix_sort_u32_w, NULL},
    {"radix_i32", W32, cmp_i32, speedup_radix_sort_i32_w, NULL},
    {"radix_f32", W32, cmp_f32, speedup_radix_sort_f32_w, NULL},
    {"radix_u64", W64, cmp_u64, speedup_radix_sort_u64_w, NULL},
    {"radix_i64", W64, cmp_i64, speedup_radix_sort_i64_w, NULL},
    {"radix_f64", W64, cmp_f64, speedup_radix_sort_f64_w, NULL},
    {"bucket_u64", W64, cmp_u64, speedup_bucket_sort_u64_w, NULL},
    {"bucket_i64", W64, cmp_i64, speedup_bucket_sort_i64_w, NULL},
    {"bucket_f64", W64, cmp_f64, speedup_bucket_sort_f64_w, NULL},
    {"bitonic_merge_u32", W32, cmp_u32, speedup_bitonic_merge_sort_u32_w, NULL},
    {"bitonic_merge_i32", W32, cmp_i32, speedup_bitonic_merge_sort_i32_w, NULL},
    {"bitonic_merge_f32", W32, cmp_f32, speedup_bitonic_merge_sort_f32_w, NULL},
    {"bitonic_merge_u64", W64, cmp_u64, speedup_bitonic_merge_sort_u64_w, NULL},
    {"bitonic_merge_i64", W64, cmp_i64, speedup_bitonic_merge_sort_i64_w, NULL},
    {"bitonic_merge_f64", W64, cmp_f64, speedup_bitonic_merge_sort_f64_w, NULL},
    {"bitonic_bucket_u32", W32, cmp_u32, speedup_bitonic_bucket_sort_u32_w, NULL},
    {"bitonic_bucket_i32", W32, cmp_i32, speedup_bitonic_bucket_sort_i32_w, NULL},
    {"bitonic_bucket_f32", W32, cmp_f32, speedup_bitonic_bucket_sort_f32_w, NULL},
    {"bitonic_bucket_u64", W64, cmp_u64, speedup_bitonic_bucket_sort_u64_w, NULL},
    {"bitonic_bucket_i64", W64, cmp_i64, speedup_bitonic_bucket_sort_i64_w, NULL},
    {"bitonic_bucket_f64", W64, cmp_f64, speedup_bitonic_bucket_sort_f64_w, NULL},
    {"bitonic_u32", W32, cmp_u32, NULL, speedup_bitonic_sort_u32_w},
    {"bitonic_i32", W32, cmp_i32, NULL, speedup_bitonic_sort_i32_w},
    {"bitonic_f32", W32, cmp_f32, NULL, speedup_bitonic_sort_f32_w},
    {"bitonic_u64", W64, cmp_u64, NULL, speedup_bitonic_sort_u64_w},
    {"bitonic_i64", W64, cmp_i64, NULL, speedup_bitonic_sort_i64_w},
    {"bitonic_f64", W64, cmp_f64, NULL, speedup_bitonic_sort_f64_w},
};
#define NSORTERS (sizeof SORTERS / sizeof SORTERS[0])

enum { RANDOM, FEW_UNIQUE, SORTED, REVERSED, CONSTANT, SPECIAL, SKEWED, PATTERNS };
static const char* PATTERN_NAMES[PATTERNS] = {"random", "few-unique", "sorted", "reversed",
                                              "constant", "special", "skewed"};

static const uint64_t SPECIAL64[] = {
    0x0000000000000000ULL, 0x8000000000000000ULL, /* +0, -0 */
    0x7FF0000000000000ULL, 0xFFF0000000000000ULL, /* +inf, -inf */
    0x7FF8000000000000ULL, 0xFFF8000000000000ULL, /* quiet NaNs */
    0x7FF0000000000001ULL, 0xFFF00000DEADBEEFULL, /* signalling NaNs with payloads */
    0x0000000000000001ULL, 0x8000000000000001ULL, /* smallest subnormals */
    0x3FF0000000000000ULL, 0xBFF0000000000000ULL, /* +-1 */
    0x7FEFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, /* max finite, NaN with all bits */
};
static const uint32_t SPECIAL32[] = {
    0x00000000U, 0x80000000U, 0x7F800000U, 0xFF800000U, 0x7FC00000U, 0xFFC00000U, 0x7F800001U,
    0xFF80BEEFU, 0x00000001U, 0x80000001U, 0x3F800000U, 0xBF800000U, 0x7F7FFFFFU, 0xFFFFFFFFU,
};

static void fill(void* data, width_t w, size_t n, int pattern) {
    for (size_t i = 0; i < n; i++) {
        uint64_t v;
        switch (pattern) {
            case FEW_UNIQUE: v = next_random() % 7; v = v * 0x0123456789ABCDEFULL; break;
            case SORTED: v = (uint64_t)i * 0x9E3779B97ULL; break;
            case REVERSED: v = (uint64_t)(n - i) * 0x9E3779B97ULL; break;
            case CONSTANT: v = 0x4045000000000000ULL; break;
            case SPECIAL:
                v = (next_random() & 1) ? (w == W64 ? SPECIAL64[next_random() % 14] : SPECIAL32[next_random() % 14])
                                        : next_random();
                break;
            case SKEWED: {
                /* mostly small values, a few huge: a stress for bucket estimates */
                uint64_t r = next_random();
                v = (r & 15) ? (r >> 40) : r;
                break;
            }
            default: v = next_random(); break;
        }
        if (w == W64) {
            ((uint64_t*)data)[i] = v;
        } else {
            ((uint32_t*)data)[i] = (pattern == SORTED || pattern == REVERSED) ? (uint32_t)(v >> 8) : (uint32_t)v;
        }
    }
}

static void run_one(const sorter* s, size_t n, int pattern, void* data, void* expect, void* scratch,
                    int use_scratch) {
    const size_t size = (s->width == W64) ? 8 : 4;
    fill(data, s->width, n, pattern);
    memcpy(expect, data, n * size);
    qsort(expect, n, size, s->cmp);
    if (s->with_scratch) {
        s->with_scratch(data, n, use_scratch ? scratch : NULL);
    } else {
        s->no_scratch(data, n);
    }
    if (memcmp(data, expect, n * size) != 0) {
        size_t at = 0;
        while (at < n && memcmp((char*)data + at * size, (char*)expect + at * size, size) == 0) at++;
        CHECK(0, "%s n=%llu pattern=%s scratch=%d: differs from qsort at %llu", s->name, (unsigned long long)n,
              PATTERN_NAMES[pattern], use_scratch, (unsigned long long)at);
    }
}

static void test_numeric(void) {
    const size_t max = 1 << 20;
    void* data = malloc(max * 8);
    void* expect = malloc(max * 8);
    void* scratch = malloc(max * 8);
    for (size_t k = 0; k < NSORTERS; k++) {
        const sorter* s = &SORTERS[k];
        for (size_t n = 0; n <= 1025; n++) {
            for (int p = 0; p < PATTERNS; p++) run_one(s, n, p, data, expect, scratch, (int)(n & 1));
        }
        const size_t large[] = {4096, 65537, 300007, (size_t)1 << 20};
        for (size_t i = 0; i < sizeof large / sizeof large[0]; i++) {
            if (!s->with_scratch && large[i] > 300007) continue; /* bitonic: O(n log^2 n) */
            for (int p = 0; p < PATTERNS; p++) run_one(s, large[i], p, data, expect, scratch, p & 1);
        }
    }
    free(data);
    free(expect);
    free(scratch);
}

/* Bitonic sort on the OpenCL GPU, when there is one: forced, on sizes around
 * the local block (1,024 keys) and the padding boundaries. */
static void test_gpu(void) {
    if (!speedup_opencl_available()) {
        printf("no OpenCL GPU: GPU bitonic sort not tested\n");
        return;
    }
    printf("OpenCL GPU: %s\n", speedup_opencl_device_name());
    const size_t sizes[] = {2, 3, 7, 8, 63, 64, 65, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
                            4097, 65537, 300007, (size_t)1 << 20, ((size_t)1 << 21) + 5};
    void* data = malloc(((size_t)1 << 22) * 8);
    void* expect = malloc(((size_t)1 << 22) * 8);
    speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_OPENCL);
    for (size_t k = 0; k < NSORTERS; k++) {
        const sorter* s = &SORTERS[k];
        if (s->with_scratch) continue; /* the bitonic sorts */
        for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            for (int p = 0; p < PATTERNS; p++) run_one(s, sizes[i], p, data, expect, NULL, 0);
        }
    }
    speedup_set_backend_preference(SPEEDUP_BACKEND_AUTO);
    free(data);
    free(expect);
}

/* ---- name sort -------------------------------------------------------------- */

static uint32_t name_flags = 0;
static int cmp_name(const void* a, const void* b) {
    return speedup_name_compare(*(const speedup_str_t*)a, *(const speedup_str_t*)b, name_flags);
}

static const char* SURNAMES[] = {"Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller",
                                 "Davis", "McDonald", "McAllister", "Schmidt", "Schneider", "Nagy", "Kovacs",
                                 "Toth", "Szabo", "Horvath", "O'Brien", "van der Berg", "de la Cruz"};
static const char* UTF8[] = {"\xC3\x81rp\xC3\xA1""d", "\xC3\x96rs", "\xC5\x90z", "\xC3\x9C""b\xC3\xBCl",
                             "Stra\xC3\x9F""e", "\xE6\x97\xA5\xE6\x9C\xAC", "\xF0\x9F\x98\x80", "Zs\xC3\xB3""fia"};

enum { N_PERSON, N_LOWER, N_MIXED, N_SHORT, N_UTF8, N_SHARED, N_EQUAL, N_BINARY, N_PATTERNS };
static const char* NAME_PATTERNS[N_PATTERNS] = {"person", "lower", "mixed", "short", "utf8",
                                                "shared-prefix", "equal", "binary"};

/* Writes name i of a pattern into buf; returns its length. */
static size_t make_name(char* buf, int pattern) {
    size_t len = 0;
    uint64_t r = next_random();
    switch (pattern) {
        case N_PERSON:
        case N_LOWER:
        case N_MIXED: {
            const char* base = SURNAMES[r % 20];
            len = strlen(base);
            memcpy(buf, base, len);
            int extra = (int)((r >> 8) % 6);
            for (int i = 0; i < extra; i++) buf[len++] = (char)('a' + (next_random() % 26));
            if (pattern == N_LOWER) {
                for (size_t i = 0; i < len; i++) buf[i] = (char)((buf[i] >= 'A' && buf[i] <= 'Z') ? buf[i] + 32 : buf[i]);
            } else if (pattern == N_MIXED) {
                for (size_t i = 0; i < len; i++) {
                    char c = buf[i];
                    if ((next_random() & 3) == 0) {
                        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                        else if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                    }
                    buf[i] = c;
                }
            }
            break;
        }
        case N_SHORT:
            len = (size_t)(r % 3);
            for (size_t i = 0; i < len; i++) buf[i] = (char)("aAbB_z"[next_random() % 6]);
            break;
        case N_UTF8: {
            const char* base = UTF8[r % 8];
            len = strlen(base);
            memcpy(buf, base, len);
            if ((r >> 8) & 1) buf[len++] = (char)('a' + (r >> 16) % 26);
            break;
        }
        case N_SHARED: {
            /* 300 shared bytes, then a short tail: MSD radix has to go deep */
            for (len = 0; len < 300; len++) buf[len] = 'M';
            int extra = (int)(r % 4);
            for (int i = 0; i < extra; i++) buf[len++] = (char)('a' + (next_random() % 3));
            break;
        }
        case N_EQUAL:
            memcpy(buf, "Kovacs", 6);
            len = 6;
            break;
        case N_BINARY:
            len = (size_t)(r % 6);
            for (size_t i = 0; i < len; i++) buf[i] = (char)(next_random() % 256); /* includes NUL */
            break;
    }
    return len;
}

static void test_names_case(size_t n, int pattern, uint32_t flags, size_t target, char* pool, speedup_str_t* names,
                            speedup_str_t* expect) {
    size_t used = 0;
    for (size_t i = 0; i < n; i++) {
        size_t len = make_name(pool + used, pattern);
        names[i].data = len ? pool + used : NULL;
        names[i].len = len;
        used += len;
    }
    memcpy(expect, names, n * sizeof(speedup_str_t));
    name_flags = flags;
    qsort(expect, n, sizeof(speedup_str_t), cmp_name);

    speedup_name_sort_options_t options = {target, 0};
    speedup_name_sort_stats_t stats;
    speedup_name_sort_ex(names, n, flags, &options, &stats);
    for (size_t i = 0; i < n; i++) {
        if (names[i].len != expect[i].len || (names[i].len && memcmp(names[i].data, expect[i].data, names[i].len))) {
            CHECK(0, "name sort n=%llu pattern=%s flags=%u target=%llu: differs from qsort at %llu",
                  (unsigned long long)n, NAME_PATTERNS[pattern], flags, (unsigned long long)target,
                  (unsigned long long)i);
            break;
        }
    }
    if (stats.buckets) {
        CHECK(stats.max_bucket <= n && stats.mean_bucket * (double)stats.buckets > (double)n - 0.5,
              "name sort stats n=%llu: buckets=%llu max=%llu mean=%f", (unsigned long long)n,
              (unsigned long long)stats.buckets, (unsigned long long)stats.max_bucket, stats.mean_bucket);
    }
}

static void test_names(void) {
    const size_t max = 200000;
    char* pool = (char*)malloc(max * 310);
    speedup_str_t* names = (speedup_str_t*)malloc(max * sizeof(speedup_str_t));
    speedup_str_t* expect = (speedup_str_t*)malloc(max * sizeof(speedup_str_t));
    /* the plan starts at 131072 names; below, MSD radix sort alone */
    const size_t sizes[] = {0, 1, 2, 3, 17, 255, 256, 600, 1000, 4096, 20000, 131072, 200000};
    const size_t targets[] = {0, 4, 32};
    for (uint32_t flags = 0; flags <= 1; flags++) {
        for (int p = 0; p < N_PATTERNS; p++) {
            for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
                for (size_t t = 0; t < 3; t++) {
                    test_names_case(sizes[s], p, flags, targets[t], pool, names, expect);
                }
            }
        }
    }
    /* The order itself: byte order with the shorter name first; caseless
     * folds A-Z only, and breaks ties by bytes. */
    speedup_str_t a = {"Abc", 3}, b = {"abc", 3}, c = {"ab", 2}, d = {"abD", 3}, e = {"\xC3\x81", 2};
    CHECK(speedup_name_compare(a, b, 0) < 0, "byte order: Abc < abc");
    CHECK(speedup_name_compare(c, b, 0) < 0, "byte order: ab < abc");
    CHECK(speedup_name_compare(d, b, 0) < 0, "byte order: abD < abc");
    CHECK(speedup_name_compare(d, b, 1) > 0, "caseless: abD > abc");
    CHECK(speedup_name_compare(a, b, 1) < 0, "caseless tie: Abc < abc by bytes");
    CHECK(speedup_name_compare(b, b, 1) == 0, "caseless: abc == abc");
    CHECK(speedup_name_compare(b, e, 1) < 0, "UTF-8 sorts after ASCII");
    free(pool);
    free(names);
    free(expect);
}

int main(void) {
    speedup_init();
    for (int simd = 1; simd >= 0; simd--) {
        speedup_sort_set_simd(simd);
        test_numeric();
    }
    speedup_sort_set_simd(1);
    test_gpu();
    test_names();
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("sort: all checks passed\n");
    return 0;
}
