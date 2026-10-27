/* Differential test: every binary search entry point against a linear scan.
 * Exhaustive on sizes 0..1025 (every element, every gap, both ends), random
 * arrays with duplicates, and a few large arrays with random keys. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

static int64_t linear_lower_bound(const int64_t* a, int64_t key, int64_t n) {
    int64_t i = 0;
    while (i < n && a[i] < key) i++;
    return i;
}

static int64_t binary_lower_bound(const int64_t* a, int64_t key, int64_t n) {
    int64_t lo = 0, hi = n;
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (a[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

/* Checks the single-key functions for one key; lb is the known answer. */
static void check_key(const int64_t* a, int64_t n, int64_t key, int64_t lb) {
    int64_t match = (lb < n && a[lb] == key) ? lb : -1;
    int64_t got = speedup_lower_bound_i64(a, key, n);
    CHECK(got == lb, "lower_bound n=%lld key=%lld: %lld, want %lld", (long long)n, (long long)key, (long long)got,
          (long long)lb);
    got = speedup_binary_search_i64(a, key, n);
    CHECK(got == match, "binary_search n=%lld key=%lld: %lld, want %lld", (long long)n, (long long)key,
          (long long)got, (long long)match);
    /* The reference may return any matching index. */
    got = speedup_binary_search_i64_ref(a, key, n);
    CHECK((match < 0) ? got == -1 : (got >= 0 && got < n && a[got] == key), "reference n=%lld key=%lld: %lld",
          (long long)n, (long long)key, (long long)got);
}

/* Checks both batch functions on a list of keys. */
static void check_batch(const int64_t* a, int64_t n, const int64_t* keys, int64_t count) {
    int64_t* lb = (int64_t*)malloc((size_t)(count ? count : 1) * sizeof(int64_t));
    int64_t* match = (int64_t*)malloc((size_t)(count ? count : 1) * sizeof(int64_t));
    speedup_lower_bound_batch_i64(a, n, keys, count, lb);
    speedup_binary_search_batch_i64(a, n, keys, count, match);
    for (int64_t i = 0; i < count; i++) {
        int64_t want = binary_lower_bound(a, keys[i], n);
        int64_t want_match = (want < n && a[want] == keys[i]) ? want : -1;
        CHECK(lb[i] == want, "lower_bound_batch n=%lld key=%lld: %lld, want %lld", (long long)n,
              (long long)keys[i], (long long)lb[i], (long long)want);
        CHECK(match[i] == want_match, "binary_search_batch n=%lld key=%lld: %lld, want %lld", (long long)n,
              (long long)keys[i], (long long)match[i], (long long)want_match);
    }
    free(lb);
    free(match);
}

static void test_exhaustive_small(void) {
    int64_t a[1025];
    int64_t keys[2 * 1025 + 3];
    for (int64_t n = 0; n <= 1025; n++) {
        for (int64_t i = 0; i < n; i++) a[i] = 2 * i;
        int64_t count = 0;
        for (int64_t key = -1; key <= 2 * n + 1; key++) {
            check_key(a, n, key, linear_lower_bound(a, key, n));
            keys[count++] = key;
        }
        check_batch(a, n, keys, count);
    }
}

static void test_duplicates(void) {
    int64_t a[600];
    int64_t keys[64];
    for (int round = 0; round < 2000; round++) {
        int64_t n = (int64_t)(next_random() % 600);
        int64_t range = 1 + (int64_t)(next_random() % 50);
        for (int64_t i = 0; i < n; i++) a[i] = (int64_t)(next_random() % (uint64_t)range);
        /* insertion sort keeps the test free of library calls */
        for (int64_t i = 1; i < n; i++) {
            int64_t v = a[i], j = i - 1;
            while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
            a[j + 1] = v;
        }
        for (int k = 0; k < 64; k++) {
            keys[k] = (int64_t)(next_random() % (uint64_t)(range + 2)) - 1;
            check_key(a, n, keys[k], linear_lower_bound(a, keys[k], n));
        }
        check_batch(a, n, keys, 64);
    }
}

static void test_extremes(void) {
    int64_t a[4] = {INT64_MIN, -1, 0, INT64_MAX};
    int64_t keys[6] = {INT64_MIN, INT64_MIN + 1, -1, 0, INT64_MAX - 1, INT64_MAX};
    for (int k = 0; k < 6; k++) check_key(a, 4, keys[k], linear_lower_bound(a, keys[k], 4));
    check_batch(a, 4, keys, 6);
}

static void test_large(void) {
    const int64_t sizes[3] = {65536, 1000003, 4194304};
    const int64_t count = 100000;
    int64_t* keys = (int64_t*)malloc((size_t)count * sizeof(int64_t));
    for (int s = 0; s < 3; s++) {
        int64_t n = sizes[s];
        int64_t* a = (int64_t*)malloc((size_t)n * sizeof(int64_t));
        for (int64_t i = 0; i < n; i++) a[i] = 3 * i;
        for (int64_t k = 0; k < count; k++) {
            keys[k] = (int64_t)(next_random() % (uint64_t)(3 * n + 2)) - 1;
            if (k < 20000) check_key(a, n, keys[k], binary_lower_bound(a, keys[k], n));
        }
        check_batch(a, n, keys, count);
        free(a);
    }
    free(keys);
}

int main(void) {
    speedup_init();
    /* Twice: with the default cache hint small arrays take the plain
     * branchless path; with an L2 hint of zero every array takes the
     * prefetching path. */
    speedup_cache_hint_t hint = speedup_get_cache_hint();
    for (int pass = 0; pass < 2; pass++) {
        test_exhaustive_small();
        test_duplicates();
        test_extremes();
        test_large();
        speedup_cache_hint_t forced = hint;
        forced.l2_bytes = 0;
        speedup_set_cache_hint(forced);
    }
    speedup_set_cache_hint(hint);
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("binary search: all checks passed\n");
    return 0;
}
