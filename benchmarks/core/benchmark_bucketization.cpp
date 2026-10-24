// Bucketization benchmark: time until the first page is visible after the
// user switches the sort column, compared with a full std::sort.
//
// Usage: speedup_benchmark_bucketization [output.csv] [max_n]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "speedup/bucketization.hpp"

namespace bz = speedup::bucketization;

namespace {

struct File {
    std::string name;
    std::int64_t size;
    std::int64_t modified;
    std::int64_t accessed;
};

/// Names like a real directory: camera files with long shared prefixes,
/// numbered documents and random words.
std::vector<File> make_files(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::lognormal_distribution<double> size(10.0, 2.5);
    std::uniform_int_distribution<std::int64_t> time(1'300'000'000, 1'760'000'000);
    std::uniform_int_distribution<int> kind(0, 2);
    std::uniform_int_distribution<int> letter(0, 25);
    std::vector<File> files(n);
    for (std::size_t i = 0; i < n; ++i) {
        File& f = files[i];
        switch (kind(rng)) {
            case 0: f.name = "IMG_20" + std::to_string(rng() % 100000000) + ".jpg"; break;
            case 1: f.name = "Document " + std::to_string(rng() % 100000) + ".pdf"; break;
            default:
                for (int c = 3 + static_cast<int>(rng() % 10); c > 0; --c) {
                    f.name.push_back(static_cast<char>('a' + letter(rng)));
                }
                f.name += ".txt";
        }
        f.size = static_cast<std::int64_t>(size(rng));
        f.modified = time(rng);
        f.accessed = std::max(f.modified, time(rng));
    }
    return files;
}

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct Row {
    std::size_t n;
    std::string key;
    double full_sort_ms;
    double presort_ms;
    double cold_switch_ms;
    double warm_switch_ms;
    double switch_and_sort_all_ms;
};

}  // namespace

int main(int argc, char** argv) {
    const char* csv_path = argc > 1 ? argv[1] : nullptr;
    const std::size_t max_n = argc > 2 ? std::stoul(argv[2]) : 1'000'000;
    const int repeats = 5;
    std::vector<Row> rows;

    for (std::size_t n = 10'000; n <= max_n; n *= 10) {
        const std::vector<File> files = make_files(n, 42);
        using Id = std::uint32_t;
        auto keys = [&files] {
            return std::vector<bz::SortKey<Id>>{
                bz::SortKey<Id>::of("name", [&files](Id i) -> const std::string& { return files[i].name; }),
                bz::SortKey<Id>::of("size", [&files](Id i) { return files[i].size; }),
                bz::SortKey<Id>::of("modified", [&files](Id i) { return files[i].modified; }),
                bz::SortKey<Id>::of("accessed", [&files](Id i) { return files[i].accessed; }),
            };
        }();
        // What a plain model does on every column click.
        const std::vector<std::function<bool(Id, Id)>> less = {
            [&files](Id a, Id b) { return files[a].name < files[b].name; },
            [&files](Id a, Id b) { return files[a].size < files[b].size; },
            [&files](Id a, Id b) { return files[a].modified < files[b].modified; },
            [&files](Id a, Id b) { return files[a].accessed < files[b].accessed; },
        };
        std::vector<Id> ids(n);
        for (std::size_t i = 0; i < n; ++i) {
            ids[i] = static_cast<Id>(i);
        }

        for (std::size_t k = 0; k < keys.size(); ++k) {
            std::vector<double> full, presort, cold, warm, sort_all;
            for (int r = 0; r < repeats; ++r) {
                std::vector<Id> perm = ids;
                auto t = Clock::now();
                std::sort(perm.begin(), perm.end(), less[k]);
                full.push_back(ms_since(t));

                bz::BucketIndex<Id> index;
                t = Clock::now();
                for (const auto& key : keys) {
                    index.add_sort_key(key);
                }
                index.add_all(ids);
                presort.push_back(ms_since(t));

                t = Clock::now();
                index.sort_by(keys[k].name());
                volatile Id sink = index.first_page().front();
                cold.push_back(ms_since(t));

                index.sort_by(keys[(k + 1) % keys.size()].name());
                t = Clock::now();
                index.sort_by(keys[k].name());
                sink = index.first_page().front();
                warm.push_back(ms_since(t));

                bz::BucketIndex<Id> fresh;
                fresh.add_sort_key(keys[k]);
                fresh.add_all(ids);
                t = Clock::now();
                fresh.sort_by(keys[k].name());
                fresh.sort_all();
                sink = fresh.at(0);
                sort_all.push_back(ms_since(t));
                (void)sink;
            }
            rows.push_back({n, keys[k].name(), median(full), median(presort), median(cold), median(warm),
                            median(sort_all)});
        }
    }

    std::printf("%10s %-9s %14s %14s %16s %16s %18s\n", "n", "key", "std::sort ms", "presort ms",
                "switch+page ms", "cached switch ms", "switch+sortall ms");
    for (const Row& r : rows) {
        std::printf("%10zu %-9s %14.3f %14.3f %16.3f %16.4f %18.3f\n", r.n, r.key.c_str(), r.full_sort_ms,
                    r.presort_ms, r.cold_switch_ms, r.warm_switch_ms, r.switch_and_sort_all_ms);
    }
    std::printf("presort = registering 4 sort keys and assigning every element to its buckets (once per load).\n");

    if (csv_path != nullptr) {
        FILE* out = std::fopen(csv_path, "w");
        if (out == nullptr) {
            std::perror(csv_path);
            return 1;
        }
        std::fprintf(out, "n,key,std_sort_ms,presort_ms,switch_first_page_ms,cached_switch_ms,switch_sort_all_ms\n");
        for (const Row& r : rows) {
            std::fprintf(out, "%zu,%s,%.4f,%.4f,%.4f,%.5f,%.4f\n", r.n, r.key.c_str(), r.full_sort_ms, r.presort_ms,
                         r.cold_switch_ms, r.warm_switch_ms, r.switch_and_sort_all_ms);
        }
        std::fclose(out);
    }
    return 0;
}
