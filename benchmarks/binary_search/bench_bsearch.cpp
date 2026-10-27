// Binary search study: every variant on the same arrays and keys, from L1 to
// DRAM, in two modes.
//   throughput  keys are independent, so the core may overlap searches
//   latency     each key depends on the previous result, one search at a time
// Keys are uniform over the whole array (splitmix64), half hits, half misses.
// Every variant is checked against the reference before it is timed, and the
// result checksums of each timed run must agree.
//
// Usage: speedup_bench_bsearch [--min-log2 10] [--max-log2 26] [--steps 2]
//        [--keys-log2 20] [--reps 9] [--modes throughput,latency]
//        [--only name,name] [--legacy-rand] [--out file.csv]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <malloc.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#include "speedup/api.h"
#include "bsearch_kernels.h"
#include "legacy_simd.h"

#if defined(_WIN64) && defined(SPEEDUP_BENCH_HAVE_ASM)
extern "C" {
int64_t branching_binary_search_win64(const int64_t* array, int64_t key, int64_t size);
int64_t branchless_binary_search_win64(const int64_t* array, int64_t key, int64_t size);
int64_t optimized_binary_search_win64(const int64_t* array, int64_t key, int64_t size);
}
#endif

typedef int64_t (*search_fn)(const int64_t*, int64_t, int64_t);
typedef void (*batch_fn)(const int64_t*, int64_t, const int64_t*, int64_t, int64_t*);

// ---------------------------------------------------------------- variants

static int64_t v_std_lower_bound(const int64_t* a, int64_t key, int64_t n) {
    int64_t lb = std::lower_bound(a, a + n, key) - a;
    return speedup_lb_to_match(a, key, n, lb);
}

static int64_t v_branchless(const int64_t* a, int64_t key, int64_t n) {
    return speedup_lb_to_match(a, key, n, speedup_lb_branchless(a, key, n));
}

static int64_t v_branchless_prefetch(const int64_t* a, int64_t key, int64_t n) {
    return speedup_lb_to_match(a, key, n, speedup_lb_branchless_prefetch(a, key, n));
}

template <int K, int Prefetch>
static void v_batch(const int64_t* a, int64_t n, const int64_t* keys, int64_t count, int64_t* out) {
    speedup_lb_batch(a, n, keys, count, out, K, Prefetch, 1);
}

// The first version of the batch kernel: a pointer select per lane. When the
// compiler does not unroll the lane loop (GCC 13 at K = 32) the select
// becomes a branch; kept to measure that.
template <int K>
static void v_batch_select(const int64_t* a, int64_t n, const int64_t* keys, int64_t count, int64_t* out) {
    int64_t i = 0;
    for (; i + K <= count; i += K) {
        const int64_t* base[K];
        for (int j = 0; j < K; j++) base[j] = a;
        int64_t len = n;
        while (len > 1) {
            int64_t half = len >> 1;
            for (int j = 0; j < K; j++) base[j] = (base[j][half] < keys[i + j]) ? base[j] + half : base[j];
            len -= half;
        }
        for (int j = 0; j < K; j++) {
            int64_t lb = (int64_t)(base[j] - a) + (*base[j] < keys[i + j]);
            out[i + j] = speedup_lb_to_match(a, keys[i + j], n, lb);
        }
    }
    for (; i < count; i++) out[i] = v_branchless(a, keys[i], n);
}

static void v_library_batch(const int64_t* a, int64_t n, const int64_t* keys, int64_t count, int64_t* out) {
    speedup_binary_search_batch_i64(a, n, keys, count, out);
}

// Eytzinger (BFS) layout, 1-based: b[k] has children b[2k] and b[2k+1].
// b[8k..8k+7] are the descendants three levels down and share one cache line
// when b is 64-byte aligned, so each step prefetches three levels ahead.
static int64_t eytzinger_fill(const int64_t* a, int64_t* b, int64_t i, int64_t k, int64_t n) {
    if (k <= n) {
        i = eytzinger_fill(a, b, i, 2 * k, n);
        b[k] = a[i++];
        i = eytzinger_fill(a, b, i, 2 * k + 1, n);
    }
    return i;
}

static inline int ctz64(uint64_t x) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanForward64(&index, x);
    return (int)index;
#else
    return __builtin_ctzll(x);
#endif
}

// Returns the Eytzinger index holding key, or -1. The prefetch address is
// clamped to the array: in the last three levels 8k runs past the end, and
// prefetching unmapped pages costs a page walk each (5x slower at 2^12).
static int64_t v_eytzinger(const int64_t* b, int64_t key, int64_t n) {
    uint64_t k = 1;
    while (k <= (uint64_t)n) {
        uint64_t ahead = k * 8;
        SPEEDUP_PREFETCH(b + (ahead <= (uint64_t)n ? ahead : (uint64_t)n));
        k = 2 * k + (b[k] < key);
    }
    k >>= ctz64(~k) + 1;
    return (k != 0 && b[k] == key) ? (int64_t)k : -1;
}

enum Kind { SINGLE, BATCH, EYTZINGER };

struct Variant {
    const char* name;
    Kind kind;
    search_fn single;
    batch_fn batch;
    bool needs_avx2;
};

static std::vector<Variant> all_variants() {
    std::vector<Variant> v;
    v.push_back({"reference", SINGLE, speedup_binary_search_i64_ref, nullptr, false});
    v.push_back({"std_lower_bound", SINGLE, v_std_lower_bound, nullptr, false});
#if defined(_WIN64) && defined(SPEEDUP_BENCH_HAVE_ASM)
    v.push_back({"asm_branching", SINGLE, branching_binary_search_win64, nullptr, false});
    v.push_back({"asm_branchless", SINGLE, branchless_binary_search_win64, nullptr, false});
    v.push_back({"asm_optimized", SINGLE, optimized_binary_search_win64, nullptr, false});
#endif
    v.push_back({"simd_avx2", SINGLE, simd_binary_search, nullptr, true});
    v.push_back({"simd_avx2_optimized", SINGLE, simd_optimized_binary_search, nullptr, true});
    v.push_back({"branchless", SINGLE, v_branchless, nullptr, false});
    v.push_back({"branchless_prefetch", SINGLE, v_branchless_prefetch, nullptr, false});
    v.push_back({"eytzinger_prefetch", EYTZINGER, v_eytzinger, nullptr, false});
    v.push_back({"library", SINGLE, speedup_binary_search_i64, nullptr, false});
    v.push_back({"batch4", BATCH, nullptr, v_batch<4, 0>, false});
    v.push_back({"batch8", BATCH, nullptr, v_batch<8, 0>, false});
    v.push_back({"batch16", BATCH, nullptr, v_batch<16, 0>, false});
    v.push_back({"batch32", BATCH, nullptr, v_batch<32, 0>, false});
    v.push_back({"batch4_prefetch", BATCH, nullptr, v_batch<4, 1>, false});
    v.push_back({"batch8_prefetch", BATCH, nullptr, v_batch<8, 1>, false});
    v.push_back({"batch16_prefetch", BATCH, nullptr, v_batch<16, 1>, false});
    v.push_back({"batch32_prefetch", BATCH, nullptr, v_batch<32, 1>, false});
    v.push_back({"batch16_select", BATCH, nullptr, v_batch_select<16>, false});
    v.push_back({"batch32_select", BATCH, nullptr, v_batch_select<32>, false});
    v.push_back({"library_batch", BATCH, nullptr, v_library_batch, false});
    return v;
}

// ---------------------------------------------------------------- platform

static void* aligned_alloc64(size_t bytes) {
#ifdef _WIN32
    return _aligned_malloc(bytes, 64);
#else
    void* p = nullptr;
    return posix_memalign(&p, 64, bytes) == 0 ? p : nullptr;
#endif
}

static void aligned_free64(void* p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

static void cpuid(unsigned leaf, unsigned out[4]) {
#if defined(_MSC_VER)
    int r[4];
    __cpuidex(r, (int)leaf, 0);
    for (int i = 0; i < 4; i++) out[i] = (unsigned)r[i];
#else
    __cpuid_count(leaf, 0, out[0], out[1], out[2], out[3]);
#endif
}

static std::string cpu_brand() {
    unsigned r[4];
    cpuid(0x80000000u, r);
    if (r[0] < 0x80000004u) return "unknown";
    char brand[49] = {0};
    for (unsigned i = 0; i < 3; i++) {
        cpuid(0x80000002u + i, r);
        std::memcpy(brand + 16 * i, r, 16);
    }
    std::string s(brand);
    s.erase(s.find_last_not_of(' ') + 1);
    s.erase(0, s.find_first_not_of(' '));
    return s;
}

static std::string compiler_name() {
    char buf[64];
#if defined(__clang__)
    std::snprintf(buf, sizeof buf, "clang %d.%d", __clang_major__, __clang_minor__);
#elif defined(_MSC_VER)
    std::snprintf(buf, sizeof buf, "msvc %d", _MSC_FULL_VER);
#elif defined(__GNUC__)
    std::snprintf(buf, sizeof buf, "gcc %d.%d", __GNUC__, __GNUC_MINOR__);
#else
    std::snprintf(buf, sizeof buf, "unknown");
#endif
    return buf;
}

// One core, high priority: keeps the scheduler from moving the thread
// between cores, which would cold-start the caches mid-run.
static void pin_thread() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
#endif
}

// ---------------------------------------------------------------- harness

static uint64_t splitmix64(uint64_t* state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Read once per run: the compiler cannot prove it is zero, so a key that
// mixes in the previous result really depends on it.
static volatile int64_t g_zero = 0;

struct Data {
    int64_t n;
    int64_t* sorted;
    int64_t* eytz;  // nullptr unless an Eytzinger variant runs
    std::vector<int64_t> keys;
};

static double now_ns() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Runs one timed pass and returns ns per query; *checksum gets the sum of
// the results (Eytzinger results are mapped back to the stored value).
static double run_pass(const Variant& v, const Data& d, bool latency, std::vector<int64_t>& out,
                       int64_t* checksum) {
    const int64_t n = d.n;
    const int64_t count = (int64_t)d.keys.size();
    const int64_t* keys = d.keys.data();
    int64_t sum = 0;
    double t0 = now_ns();
    if (v.kind == BATCH) {
        v.batch(d.sorted, n, keys, count, out.data());
        for (int64_t i = 0; i < count; i++) sum += out[i];
    } else {
        const int64_t* arr = v.kind == EYTZINGER ? d.eytz : d.sorted;
        search_fn f = v.single;
        if (latency) {
            const int64_t z = g_zero;
            int64_t r = 0;
            for (int64_t i = 0; i < count; i++) {
                r = f(arr, keys[i] ^ (r & z), n);
                sum += r;
            }
        } else {
            for (int64_t i = 0; i < count; i++) sum += f(arr, keys[i], n);
        }
    }
    double t1 = now_ns();
    *checksum = sum;
    return (t1 - t0) / (double)count;
}

// The checksum every exact-match variant must produce; Eytzinger returns
// positions in its own layout, so it gets its own reference sum.
static int64_t expected_checksum(const Data& d, bool eytzinger) {
    int64_t sum = 0;
    for (int64_t key : d.keys) {
        if (!eytzinger) {
            sum += speedup_binary_search_i64_ref(d.sorted, key, d.n);
        } else {
            sum += v_eytzinger(d.eytz, key, d.n);
        }
    }
    return sum;
}

// Checks a variant against the reference on edge keys and the first keys of
// the run. For Eytzinger: same hit or miss, and the hit holds the key.
static bool verify(const Variant& v, const Data& d) {
    std::vector<int64_t> probe;
    probe.push_back(-1);
    probe.push_back(0);
    probe.push_back(d.sorted[d.n - 1]);
    probe.push_back(d.sorted[d.n - 1] + 1);
    for (size_t i = 0; i < d.keys.size() && i < 4096; i++) probe.push_back(d.keys[i]);
    std::vector<int64_t> out(probe.size());
    if (v.kind == BATCH) v.batch(d.sorted, d.n, probe.data(), (int64_t)probe.size(), out.data());
    for (size_t i = 0; i < probe.size(); i++) {
        int64_t expected = speedup_binary_search_i64_ref(d.sorted, probe[i], d.n);
        int64_t got;
        if (v.kind == BATCH) {
            got = out[i];
        } else if (v.kind == EYTZINGER) {
            int64_t e = v_eytzinger(d.eytz, probe[i], d.n);
            if ((e < 0) != (expected < 0) || (e >= 0 && d.eytz[e] != probe[i])) {
                std::fprintf(stderr, "verify %s n=%lld key=%lld: eytzinger %lld, reference %lld\n", v.name,
                             (long long)d.n, (long long)probe[i], (long long)e, (long long)expected);
                return false;
            }
            continue;
        } else {
            got = v.single(d.sorted, probe[i], d.n);
        }
        if (got != expected) {
            std::fprintf(stderr, "verify %s n=%lld key=%lld: got %lld, reference %lld\n", v.name, (long long)d.n,
                         (long long)probe[i], (long long)got, (long long)expected);
            return false;
        }
    }
    return true;
}

static double quantile(std::vector<double> x, double q) {
    std::sort(x.begin(), x.end());
    double pos = q * (double)(x.size() - 1);
    size_t lo = (size_t)std::floor(pos), hi = (size_t)std::ceil(pos);
    return x[lo] + (x[hi] - x[lo]) * (pos - (double)lo);
}

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos) comma = s.size();
        if (comma > start) out.push_back(s.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

static bool listed(const std::vector<std::string>& list, const char* name) {
    return list.empty() || std::find(list.begin(), list.end(), name) != list.end();
}

int main(int argc, char** argv) {
    double min_log2 = 10, max_log2 = 26;
    int steps = 2;  // sizes per octave
    int keys_log2 = 20, reps = 9;
    bool legacy_rand = false;
    std::vector<std::string> modes = {"throughput", "latency"}, only;
    const char* out_path = "bsearch_results.csv";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--min-log2") min_log2 = std::atof(next());
        else if (a == "--max-log2") max_log2 = std::atof(next());
        else if (a == "--steps") steps = std::atoi(next());
        else if (a == "--keys-log2") keys_log2 = std::atoi(next());
        else if (a == "--reps") reps = std::atoi(next());
        else if (a == "--modes") modes = split(next());
        else if (a == "--only") only = split(next());
        else if (a == "--legacy-rand") legacy_rand = true;
        else if (a == "--out") out_path = next();
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }

    if (steps < 1 || reps < 1 || keys_log2 < 4 || min_log2 < 1 || max_log2 < min_log2) {
        std::fprintf(stderr, "need --steps >= 1, --reps >= 1, --keys-log2 >= 4, 1 <= --min-log2 <= --max-log2\n");
        return 2;
    }
    pin_thread();
    unsigned r[4];
    cpuid(7, r);
    const bool have_avx2 = (r[1] >> 5) & 1;
    const std::string cpu = cpu_brand(), compiler = compiler_name();
    std::printf("cpu: %s\ncompiler: %s\navx2: %s\nkeys per pass: %d, reps: %d%s\n", cpu.c_str(), compiler.c_str(),
                have_avx2 ? "yes" : "no", 1 << keys_log2, reps, legacy_rand ? ", legacy rand() keys" : "");

    std::vector<Variant> variants;
    for (const Variant& v : all_variants()) {
        if (!listed(only, v.name)) continue;
        if (v.needs_avx2 && !have_avx2) continue;
        variants.push_back(v);
    }
    bool need_eytz = false;
    for (const Variant& v : variants) need_eytz |= v.kind == EYTZINGER;

    FILE* csv = std::fopen(out_path, "w");
    if (!csv) { std::fprintf(stderr, "cannot open %s\n", out_path); return 1; }
    std::fprintf(csv, "compiler,cpu,keys,mode,variant,n,bytes,reps,queries,median_ns,q1_ns,q3_ns,min_ns,checksum_ok\n");

    std::vector<int64_t> sizes;
    for (int s = (int)std::lround(min_log2 * steps); s <= (int)std::lround(max_log2 * steps); s++) {
        int64_t n = (int64_t)std::llround(std::pow(2.0, (double)s / steps));
        if (sizes.empty() || sizes.back() != n) sizes.push_back(n);
    }

    const int64_t count = (int64_t)1 << keys_log2;
    bool all_ok = true;
    for (int64_t n : sizes) {
        Data d;
        d.n = n;
        d.sorted = (int64_t*)aligned_alloc64((size_t)n * sizeof(int64_t));
        d.eytz = need_eytz ? (int64_t*)aligned_alloc64((size_t)(n + 1) * sizeof(int64_t)) : nullptr;
        if (!d.sorted || (need_eytz && !d.eytz)) { std::fprintf(stderr, "out of memory at n=%lld\n", (long long)n); return 1; }
        for (int64_t i = 0; i < n; i++) d.sorted[i] = 2 * i;

        d.keys.resize((size_t)count);
        if (legacy_rand) {
            std::srand(12345);
            for (int64_t i = 0; i < count; i++) {
                d.keys[i] = (std::rand() % 2 == 0) ? (std::rand() % n) * 2 : (std::rand() % n) * 2 + 1;
            }
        } else {
            uint64_t rng = 12345 + (uint64_t)n;
            for (int64_t i = 0; i < count; i++) {
                uint64_t x = splitmix64(&rng);
                d.keys[i] = (int64_t)((x >> 1) % (uint64_t)n) * 2 + (int64_t)(x & 1);
            }
        }

        if (need_eytz) {
            double t0 = now_ns();
            d.eytz[0] = 0;
            eytzinger_fill(d.sorted, d.eytz, 0, 1, n);
            double t1 = now_ns();
            std::fprintf(csv, "%s,%s,%s,build,eytzinger_build,%lld,%lld,1,%lld,%.4f,%.4f,%.4f,%.4f,1\n",
                         compiler.c_str(), cpu.c_str(), legacy_rand ? "legacy_rand" : "uniform", (long long)n,
                         (long long)(n * 8), (long long)n, (t1 - t0) / n, (t1 - t0) / n, (t1 - t0) / n,
                         (t1 - t0) / n);
        }

        const int64_t sum_ref = expected_checksum(d, false);
        const int64_t sum_eytz = need_eytz ? expected_checksum(d, true) : 0;
        std::vector<int64_t> out((size_t)count);

        for (const std::string& mode : modes) {
            const bool latency = mode == "latency";
            std::vector<const Variant*> run;
            for (const Variant& v : variants) {
                if (latency && v.kind == BATCH) continue;  // a dependent chain cannot be batched
                if (!verify(v, d)) { all_ok = false; continue; }
                run.push_back(&v);
            }
            std::vector<std::vector<double>> times(run.size());
            std::vector<bool> ok(run.size(), true);
            // One untimed pass each (page faults, branch predictors), then
            // the timed passes round-robin, so a slow stretch of the machine
            // hits every variant alike.
            for (size_t i = 0; i < run.size(); i++) {
                int64_t c;
                run_pass(*run[i], d, latency, out, &c);
            }
            for (int rep = 0; rep < reps; rep++) {
                for (size_t i = 0; i < run.size(); i++) {
                    int64_t c;
                    times[i].push_back(run_pass(*run[i], d, latency, out, &c));
                    int64_t want = run[i]->kind == EYTZINGER ? sum_eytz : sum_ref;
                    if (c != want) ok[i] = false;
                }
            }
            for (size_t i = 0; i < run.size(); i++) {
                if (!ok[i]) {
                    all_ok = false;
                    std::fprintf(stderr, "checksum mismatch: %s n=%lld %s\n", run[i]->name, (long long)n, mode.c_str());
                }
                double med = quantile(times[i], 0.5);
                std::fprintf(csv, "%s,%s,%s,%s,%s,%lld,%lld,%d,%lld,%.4f,%.4f,%.4f,%.4f,%d\n", compiler.c_str(),
                             cpu.c_str(), legacy_rand ? "legacy_rand" : "uniform", mode.c_str(), run[i]->name,
                             (long long)n, (long long)(n * 8), reps, (long long)count, med,
                             quantile(times[i], 0.25), quantile(times[i], 0.75),
                             *std::min_element(times[i].begin(), times[i].end()), ok[i] ? 1 : 0);
                std::printf("%-10s n=%-10lld %-22s %8.2f ns%s\n", mode.c_str(), (long long)n, run[i]->name, med,
                            ok[i] ? "" : "  CHECKSUM MISMATCH");
            }
            std::fflush(csv);
            std::fflush(stdout);
        }
        aligned_free64(d.sorted);
        if (d.eytz) aligned_free64(d.eytz);
    }
    std::fclose(csv);
    std::printf(all_ok ? "all variants verified\n" : "VERIFICATION FAILED\n");
    return all_ok ? 0 : 1;
}
