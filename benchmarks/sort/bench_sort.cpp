// Sorting study: the new sorts against the standard library.
//   numeric  radix, bucket and bitonic sort against std::sort,
//            std::stable_sort and qsort, on uniform, skewed, sorted and
//            few-unique data, for u32, u64, i64 and f64
//   names    name sort against std::sort on string views and a plain MSD
//            radix sort, on a name list (one per line, e.g. from
//            benchmarks/datasets/capture_names.py), plus the bucket
//            statistics of name sort's plan
// Every algorithm is checked against std::sort before it is timed. The
// timed runs of all algorithms on one input are interleaved round-robin, so
// that slow periods of the machine affect them alike; each run sorts a fresh
// copy of the input (the copy is not timed). The thread is pinned to one core
// and runs at high priority. Reported: median, first and third quartile.
//
// Usage: speedup_bench_sort [--min-log2 10] [--max-log2 24] [--steps 1]
//        [--reps 7] [--names file.txt] [--only numeric|names]
//        [--algos name,name] (time only these, e.g. name_sort or bitonic)
//        [--bitonic-scalar] (also time bitonic sort with the AVX2 kernels off)
//        [--types u32,u64,i64,f64] [--dists uniform,skewed,sorted,few-unique]
//        [--out sort_results.csv]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "speedup/api.h"
#include "speedup/sort.hpp"

namespace {

double now_ns() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Short name for grouping results; the version goes to stdout.
const char* compiler_name() {
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

void print_compiler_version() {
#if defined(__clang__)
    std::printf("compiler: clang %d.%d\n", __clang_major__, __clang_minor__);
#elif defined(__GNUC__)
    std::printf("compiler: gcc %d.%d\n", __GNUC__, __GNUC_MINOR__);
#elif defined(_MSC_VER)
    std::printf("compiler: msvc %d\n", _MSC_FULL_VER);
#endif
}

void pin_thread() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << 2);
#endif
}

double quantile(std::vector<double> v, double q) {
    std::sort(v.begin(), v.end());
    double pos = q * (double)(v.size() - 1);
    std::size_t lo = (std::size_t)pos;
    std::size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (pos - (double)lo);
}

// One algorithm on one input: `prepare` restores the input (untimed), `run`
// sorts it, `ok` compares the result with std::sort's.
struct Timed {
    const char* name;
    std::function<void()> prepare;
    std::function<void()> run;
    std::function<bool()> ok;
};

struct Stats {
    double median, q1, q3;
    bool ok;
};

std::vector<Stats> measure(const std::vector<Timed>& algos, int reps, std::size_t n) {
    std::vector<Stats> out(algos.size());
    std::vector<std::vector<double>> times(algos.size());
    for (std::size_t a = 0; a < algos.size(); ++a) {
        algos[a].prepare();
        algos[a].run();
        out[a].ok = algos[a].ok();
    }
    for (int r = 0; r < reps; ++r) {
        for (std::size_t a = 0; a < algos.size(); ++a) {
            algos[a].prepare();
            double t0 = now_ns();
            algos[a].run();
            times[a].push_back((now_ns() - t0) / (double)n);
        }
    }
    for (std::size_t a = 0; a < algos.size(); ++a) {
        out[a].median = quantile(times[a], 0.5);
        out[a].q1 = quantile(times[a], 0.25);
        out[a].q3 = quantile(times[a], 0.75);
    }
    return out;
}

// ---------------------------------------------------------------- numeric

template <class T>
int qsort_cmp(const void* a, const void* b) {
    T x, y;
    std::memcpy(&x, a, sizeof x);
    std::memcpy(&y, b, sizeof y);
    return (x > y) - (x < y);
}

template <class T>
std::vector<T> make_data(const std::string& dist, std::size_t n, std::mt19937_64& rng) {
    std::vector<T> v(n);
    if (dist == "uniform") {
        if constexpr (std::is_floating_point_v<T>) {
            std::uniform_real_distribution<T> d(0, 1);
            for (T& x : v) x = d(rng);
        } else {
            for (T& x : v) x = static_cast<T>(rng());
        }
    } else if (dist == "skewed") {
        // log-normal sizes (file sizes, prices): heavy tail, dense small values
        std::lognormal_distribution<double> d(10.0, 2.5);
        for (T& x : v) {
            double s = d(rng);
            if constexpr (std::is_floating_point_v<T>) {
                x = static_cast<T>(s);
            } else {
                x = static_cast<T>(std::min(s, 4.0e9));
            }
        }
    } else if (dist == "sorted") {
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<T>(i);
    } else {  // few-unique
        for (T& x : v) x = static_cast<T>(rng() % 16);
    }
    return v;
}

bool listed(const std::string& only_algos, const char* name) {
    return only_algos.empty() || ("," + only_algos + ",").find("," + std::string(name) + ",") != std::string::npos;
}

template <class T>
void bench_numeric(FILE* csv, const char* type, const std::vector<std::size_t>& sizes, int reps,
                   const std::string& only_algos, bool bitonic_scalar, const std::string& only_dists) {
    for (const char* dist : {"uniform", "skewed", "sorted", "few-unique"}) {
        if (!listed(only_dists, dist)) continue;
        for (std::size_t n : sizes) {
            std::mt19937_64 rng(n * 31 + std::strlen(dist));
            const std::vector<T> input = make_data<T>(dist, n, rng);
            std::vector<T> expect = input;
            std::sort(expect.begin(), expect.end());
            std::vector<T> work(n), scratch(n);
            auto prepare = [&] { std::copy(input.begin(), input.end(), work.begin()); };
            auto ok = [&] { return work == expect; };
            std::vector<Timed> algos = {
                {"std::sort", prepare, [&] { std::sort(work.begin(), work.end()); }, ok},
                {"std::stable_sort", prepare, [&] { std::stable_sort(work.begin(), work.end()); }, ok},
                {"qsort", prepare, [&] { std::qsort(work.data(), n, sizeof(T), qsort_cmp<T>); }, ok},
                {"radix", prepare, [&] { speedup::sort::radix_sort(work.data(), n, scratch.data()); }, ok},
            };
            // a sorting network does O(n log^2 n) work: measured up to 4 Mi elements
            if (n <= ((std::size_t)1 << 22)) {
                algos.push_back({"bitonic", prepare,
                                 [&] {
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_CPU);
                                     speedup::sort::bitonic_sort(work.data(), n);
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_AUTO);
                                 },
                                 ok});
            }
#ifdef SPEEDUP_SORT_BENCH_HYBRIDS
            // 0.3.1: bitonic hybrids, and bitonic sort on an OpenCL GPU (copies included)
            algos.push_back({"bitonic_merge", prepare,
                             [&] { speedup::sort::bitonic_merge_sort(work.data(), n, scratch.data()); }, ok});
            algos.push_back({"bitonic_bucket", prepare,
                             [&] { speedup::sort::bitonic_bucket_sort(work.data(), n, scratch.data()); }, ok});
            if (speedup_opencl_available()) {
                algos.push_back({"bitonic_gpu", prepare,
                                 [&] {
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_OPENCL);
                                     speedup::sort::bitonic_sort(work.data(), n);
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_AUTO);
                                 },
                                 ok});
            }
#endif
            if (bitonic_scalar && n <= ((std::size_t)1 << 22)) {
                algos.push_back({"bitonic_scalar", prepare,
                                 [&] {
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_FORCE_CPU);
                                     speedup_sort_set_simd(0);
                                     speedup::sort::bitonic_sort(work.data(), n);
                                     speedup_sort_set_simd(1);
                                     speedup_set_backend_preference(SPEEDUP_BACKEND_AUTO);
                                 },
                                 ok});
            }
            if constexpr (sizeof(T) == 8) {
                algos.push_back({"bucket", prepare,
                                 [&] { speedup::sort::bucket_sort(work.data(), n, scratch.data()); }, ok});
            }
            algos.erase(std::remove_if(algos.begin(), algos.end(),
                                       [&](const Timed& t) { return !listed(only_algos, t.name); }),
                        algos.end());
            const std::vector<Stats> st = measure(algos, reps, n);
            for (std::size_t a = 0; a < algos.size(); ++a) {
                std::printf("%-4s %-10s n=%-9zu %-17s %8.2f ns/elem%s\n", type, dist, n, algos[a].name,
                            st[a].median, st[a].ok ? "" : "  WRONG");
                std::fprintf(csv, "%s,numeric,%s,%s,%zu,%s,%.4f,%d,,,,%.4f,%.4f,%d\n", compiler_name(), type, dist,
                             n, algos[a].name, st[a].median, st[a].ok ? 1 : 0, st[a].q1, st[a].q3, reps);
            }
        }
    }
}

// ---------------------------------------------------------------- names

int cmp_view(std::string_view a, std::string_view b) { return a.compare(b); }

void insertion_views(std::string_view* a, std::size_t n, std::size_t depth) {
    for (std::size_t i = 1; i < n; ++i) {
        std::string_view v = a[i];
        std::size_t j = i;
        while (j > 0 && cmp_view(a[j - 1].substr(std::min(depth, a[j - 1].size())),
                                 v.substr(std::min(depth, v.size()))) > 0) {
            a[j] = a[j - 1];
            --j;
        }
        a[j] = v;
    }
}

// Plain MSD radix sort (byte order): 257 buckets per level, insertion sort
// below 32 names. No plan, no sample: the baseline name sort must beat.
void msd_radix(std::string_view* a, std::string_view* tmp, std::size_t n, std::size_t depth) {
    if (n < 32) {
        insertion_views(a, n, depth);
        return;
    }
    std::size_t count[258] = {0};
    for (std::size_t i = 0; i < n; ++i) {
        unsigned s = depth < a[i].size() ? 1u + (unsigned char)a[i][depth] : 0u;
        count[s + 1]++;
    }
    for (int s = 0; s < 257; ++s) count[s + 1] += count[s];
    std::size_t pos[257];
    std::copy(count, count + 257, pos);
    for (std::size_t i = 0; i < n; ++i) {
        unsigned s = depth < a[i].size() ? 1u + (unsigned char)a[i][depth] : 0u;
        tmp[pos[s]++] = a[i];
    }
    std::copy(tmp, tmp + n, a);
    for (int s = 1; s < 257; ++s) {
        std::size_t lo = count[s], hi = count[s + 1];
        if (hi - lo > 1) msd_radix(a + lo, tmp + lo, hi - lo, depth + 1);
    }
}

void bench_names(FILE* csv, const char* path, int reps, const std::string& only_algos) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        std::exit(1);
    }
    std::vector<std::string> all;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        all.push_back(line);
    }
    std::printf("names: %zu from %s\n", all.size(), path);
    std::vector<std::size_t> sizes;  // 1-2-5 steps from 1,000 up to the whole list
    for (std::size_t decade = 1000; decade < all.size(); decade *= 10) {
        for (std::size_t m : {1, 2, 5}) {
            if (decade * m < all.size()) sizes.push_back(decade * m);
        }
    }
    sizes.push_back(all.size());

    for (std::size_t n : sizes) {
        std::vector<std::string_view> input(all.begin(), all.begin() + (std::ptrdiff_t)n);
        std::vector<std::string_view> expect = input;
        std::sort(expect.begin(), expect.end());
        std::vector<std::string_view> work(n), tmp(n);
        std::vector<speedup_str_t> raw(n), raw_input(n);
        for (std::size_t i = 0; i < n; ++i) raw_input[i] = {input[i].data(), input[i].size()};

        speedup_name_sort_stats_t stats{};
        auto views_prepare = [&] { std::copy(input.begin(), input.end(), work.begin()); };
        auto views_ok = [&] { return work == expect; };
        auto raw_prepare = [&] { std::copy(raw_input.begin(), raw_input.end(), raw.begin()); };
        auto raw_ok = [&] {
            for (std::size_t i = 0; i < n; ++i) {
                if (std::string_view(raw[i].data, raw[i].len) != expect[i]) return false;
            }
            return true;
        };
        std::vector<Timed> algos = {
            {"std::sort", views_prepare, [&] { std::sort(work.begin(), work.end()); }, views_ok},
            {"msd_radix", views_prepare, [&] { msd_radix(work.data(), tmp.data(), n, 0); }, views_ok},
            {"name_sort", raw_prepare, [&] { speedup_name_sort_ex(raw.data(), n, 0, nullptr, &stats); }, raw_ok},
        };
        algos.erase(std::remove_if(algos.begin(), algos.end(),
                                   [&](const Timed& t) { return !listed(only_algos, t.name); }),
                    algos.end());
        const std::vector<Stats> st = measure(algos, reps, n);
        for (std::size_t a = 0; a < algos.size(); ++a) {
            const bool is_name_sort = std::strcmp(algos[a].name, "name_sort") == 0;
            std::printf("names n=%-9zu %-10s %8.2f ns/name%s", n, algos[a].name, st[a].median,
                        st[a].ok ? "" : "  WRONG");
            if (is_name_sort) {
                std::printf("  buckets=%zu blocks=%zu max=%zu mean=%.1f sd=%.1f radix-finished=%zu", stats.buckets,
                            stats.trie_blocks, stats.max_bucket, stats.mean_bucket, stats.stddev_bucket,
                            stats.oversized);
                std::fprintf(csv, "%s,names,str,names,%zu,%s,%.4f,%d,%zu,%zu,%.2f,%.4f,%.4f,%d\n", compiler_name(), n,
                             algos[a].name, st[a].median, st[a].ok ? 1 : 0, stats.buckets, stats.max_bucket,
                             stats.stddev_bucket, st[a].q1, st[a].q3, reps);
            } else {
                std::fprintf(csv, "%s,names,str,names,%zu,%s,%.4f,%d,,,,%.4f,%.4f,%d\n", compiler_name(), n,
                             algos[a].name, st[a].median, st[a].ok ? 1 : 0, st[a].q1, st[a].q3, reps);
            }
            std::printf("\n");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    int min_log2 = 10, max_log2 = 24, steps = 1, reps = 7;
    const char* names_path = nullptr;
    const char* out_path = "sort_results.csv";
    std::string only, only_algos, only_types, only_dists;
    bool bitonic_scalar = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--min-log2") min_log2 = std::atoi(next());
        else if (a == "--max-log2") max_log2 = std::atoi(next());
        else if (a == "--steps") steps = std::atoi(next());
        else if (a == "--reps") reps = std::atoi(next());
        else if (a == "--names") names_path = next();
        else if (a == "--only") only = next();
        else if (a == "--algos") only_algos = next();
        else if (a == "--bitonic-scalar") bitonic_scalar = true;
        else if (a == "--types") only_types = next();
        else if (a == "--dists") only_dists = next();
        else if (a == "--out") out_path = next();
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (steps < 1 || reps < 1 || min_log2 < 1 || max_log2 < min_log2) {
        std::fprintf(stderr, "need --steps >= 1, --reps >= 1, 1 <= --min-log2 <= --max-log2\n");
        return 2;
    }
    speedup_init();
    pin_thread();
    print_compiler_version();
#ifdef SPEEDUP_SORT_BENCH_HYBRIDS
    if (speedup_opencl_available()) std::printf("opencl: %s\n", speedup_opencl_device_name());
#endif
    FILE* csv = std::fopen(out_path, "w");
    if (!csv) { std::fprintf(stderr, "cannot open %s\n", out_path); return 1; }
    std::fprintf(csv, "compiler,kind,type,distribution,n,algorithm,ns_per_element,ok,buckets,max_bucket,"
                      "stddev_bucket,q1_ns,q3_ns,reps\n");

    if (only.empty() || only == "numeric") {
        std::vector<std::size_t> sizes;
        for (int s = min_log2 * steps; s <= max_log2 * steps; s++) {
            std::size_t n = (std::size_t)std::llround(std::pow(2.0, (double)s / steps));
            if (sizes.empty() || sizes.back() != n) sizes.push_back(n);
        }
        if (listed(only_types, "u32")) bench_numeric<std::uint32_t>(csv, "u32", sizes, reps, only_algos, bitonic_scalar, only_dists);
        if (listed(only_types, "u64")) bench_numeric<std::uint64_t>(csv, "u64", sizes, reps, only_algos, bitonic_scalar, only_dists);
        if (listed(only_types, "i64")) bench_numeric<std::int64_t>(csv, "i64", sizes, reps, only_algos, bitonic_scalar, only_dists);
        if (listed(only_types, "f64")) bench_numeric<double>(csv, "f64", sizes, reps, only_algos, bitonic_scalar, only_dists);
    }
    if (names_path && (only.empty() || only == "names")) bench_names(csv, names_path, reps, only_algos);
    std::fclose(csv);
    return 0;
}
