#pragma once
// Shared measurement loop for the per-file-manager benchmarks
// (benchmarks/gnome_stack, qt_stack, terminal_stack).
//
// Every benchmark describes, per project, column and direction, a Case:
//   - resort:  the project's own code path for a sort change, run on the items
//              in the previous order (the timed part is what the project runs
//              when the user clicks a column header);
//   - compare: the project's comparison for that column and direction;
//   - key:     a Bucketization sort key for the column (folders first as a
//              group, the project's comparison as the tie-breaker).
// The harness times both sides and checks Bucketization's whole order against
// the project's comparison (ties allowed) and against the project's own result.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "speedup/bucketization.hpp"

namespace fm_bench {

namespace bz = speedup::bucketization;
using Clock = std::chrono::steady_clock;

inline double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

inline double median(std::vector<double> v) {
    if (v.empty()) {
        return -1;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

template <class F>
std::vector<double> repeat(int times, F&& f) {
    std::vector<double> out;
    for (int i = 0; i < times; ++i) {
        out.push_back(f());
    }
    return out;
}

/// FNV-1a over the paths in order, '\n' separated. The native Rust and Go
/// benchmarks compute the same hash, so orders can be compared across languages.
inline std::uint64_t fnv1a(std::uint64_t hash, const std::string& text) {
    for (unsigned char c : text) {
        hash = (hash ^ c) * 0x100000001b3ULL;
    }
    return (hash ^ '\n') * 0x100000001b3ULL;
}
constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ULL;

struct Options {
    std::string csv;
    std::string locale = "en_US.UTF-8";
    int repeats = 5;
    std::size_t page = 64;
    std::size_t max_entries = 0;  // skip larger datasets (0: no limit)
    std::vector<std::string> only;  // run only these projects
    std::vector<std::string> datasets;

    bool wants(const std::string& project) const {
        return only.empty() || std::find(only.begin(), only.end(), project) != only.end();
    }
};

inline Options parse_options(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(arg + " needs a value");
            }
            return argv[++i];
        };
        if (arg == "--csv") opt.csv = next();
        else if (arg == "--repeats") opt.repeats = std::stoi(next());
        else if (arg == "--locale") opt.locale = next();
        else if (arg == "--page") opt.page = std::stoul(next());
        else if (arg == "--max-entries") opt.max_entries = std::stoul(next());
        else if (arg == "--only") opt.only.push_back(next());
        else opt.datasets.push_back(arg);
    }
    return opt;
}

constexpr const char* kUsage =
    "[--csv FILE] [--repeats N] [--locale L] [--page N] [--max-entries N] [--only PROJECT]... DATASET.tsv...";

struct Row {
    std::string project;
    std::string dataset;
    std::size_t n = 0;
    std::string role;
    std::string order;
    double fm_resort = -1;     // the project's own path for a sort change
    double partial_sort = -1;  // std::partial_sort of the first page with the project's comparison
    double bz_presort = -1;    // Bucketization: add_sort_key on the loaded items (once per column)
    double bz_switch = -1;     // sort_by + first_page, grouping not cached
    double bz_cached = -1;     // sort_by + first_page, grouping cached
    std::size_t first_page_cost = 0;
    bool fm_sorted = true;     // the project's result is sorted under its own comparison
    std::size_t ties = 0;      // neighbours in the project's result that its comparison calls equal
    bool bz_valid = true;      // Bucketization's order is sorted under the project's comparison
    bool identical = true;     // ... and equals the project's result element by element
    std::uint64_t order_hash = 0;  // fnv1a of the project's result (for the native ports)
};

template <class Item>
struct Case {
    std::string project;
    std::string role;
    bool descending = false;
    /// Re-sorts `previous` the way the project does on a sort change; returns the
    /// timed part in milliseconds and the resulting order in `out`.
    std::function<double(const std::vector<Item>& previous, std::vector<Item>& out)> resort;
    /// The project's comparison for this column and direction.
    std::function<int(const Item&, const Item&)> compare;
    /// Bucketization key for the column, ascending (descending reads it backwards per group).
    std::function<bz::SortKey<Item>()> make_key;
    /// Path of an item, for the order hash. Optional.
    std::function<const std::string&(const Item&)> path;
};

/// Previous order: the listing sorted by `compare` (stable).
template <class Item>
std::vector<Item> sorted_by(std::vector<Item> items, const std::function<int(const Item&, const Item&)>& compare) {
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) { return compare(a, b) < 0; });
    return items;
}

template <class Item>
Row run_case(const Case<Item>& c, const std::string& dataset, const std::vector<Item>& listing,
             const std::vector<Item>& previous, const Options& opt) {
    Row row;
    row.project = c.project;
    row.dataset = dataset;
    row.n = listing.size();
    row.role = c.role;
    row.order = c.descending ? "desc" : "asc";
    const int reps = row.n > 300000 ? std::max(1, opt.repeats / 3) : opt.repeats;
    const auto less = [&](const Item& a, const Item& b) { return c.compare(a, b) < 0; };

    std::vector<Item> reference;
    row.fm_resort = median(repeat(reps, [&] { return c.resort(previous, reference); }));
    if (reference.size() != listing.size()) {
        throw std::runtime_error(c.project + ": resort returned " + std::to_string(reference.size()) + " items");
    }
    for (std::size_t i = 1; i < reference.size(); ++i) {
        const int r = c.compare(reference[i - 1], reference[i]);
        row.fm_sorted = row.fm_sorted && r <= 0;
        row.ties += r == 0;
    }
    if (c.path) {
        row.order_hash = kFnvOffset;
        for (const Item& item : reference) {
            row.order_hash = fnv1a(row.order_hash, c.path(item));
        }
    }

    const std::size_t page = std::min(opt.page, listing.size());
    row.partial_sort = median(repeat(reps, [&] {
        std::vector<Item> copy = previous;
        const auto t = Clock::now();
        std::partial_sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(page), copy.end(), less);
        return ms_since(t);
    }));

    const bz::BucketPlan plan = bz::BucketPlan::defaults().with_page_size(opt.page);
    bz::BucketIndex<Item> index(plan);
    index.add_all(listing);
    index.add_sort_key(bz::SortKey<Item>::of_key(
        "other", [](const Item& item) { return static_cast<std::uint64_t>(std::hash<Item>{}(item)); }));
    const bz::SortKey<Item> key = c.make_key();
    const auto direction = c.descending ? bz::Direction::descending : bz::Direction::ascending;
    row.bz_presort = median(repeat(reps, [&] {
        index.remove_sort_key(key.name());
        const auto t = Clock::now();
        index.add_sort_key(key);
        return ms_since(t);
    }));
    row.bz_switch = median(repeat(reps, [&] {
        index.rebalance(key.name());  // drops the cached grouping
        index.sort_by("other");
        const auto t = Clock::now();
        index.sort_by(key.name(), direction);
        const std::vector<Item> first = index.first_page();
        const double elapsed = ms_since(t);
        for (std::size_t i = 1; i < first.size(); ++i) {
            row.bz_valid = row.bz_valid && c.compare(first[i - 1], first[i]) <= 0;
        }
        return elapsed;
    }));
    row.bz_cached = median(repeat(reps, [&] {
        index.sort_by("other");
        const auto t = Clock::now();
        index.sort_by(key.name(), direction);
        index.first_page();
        return ms_since(t);
    }));
    const bz::BucketStats stats = index.stats(key.name());
    row.first_page_cost = c.descending ? stats.first_page_cost_descending : stats.first_page_cost_ascending;

    index.sort_by(key.name(), direction);
    std::size_t position = 0;
    const Item* previous_item = nullptr;
    index.for_each([&](const Item& item) {
        if (previous_item != nullptr) {
            row.bz_valid = row.bz_valid && c.compare(*previous_item, item) <= 0;
        }
        row.identical = row.identical && reference[position] == item;
        previous_item = &item;
        ++position;
    });
    row.bz_valid = row.bz_valid && position == listing.size();
    return row;
}

inline void print_row(const Row& r) {
    std::printf("  %-14s %-8s %-4s resort %9.3f | partial_sort %8.3f | bz: presort %8.3f switch %7.3f cached %7.4f"
                " | %s%s%s\n",
                r.project.c_str(), r.role.c_str(), r.order.c_str(), r.fm_resort, r.partial_sort, r.bz_presort,
                r.bz_switch, r.bz_cached, r.bz_valid ? "valid" : "INVALID",
                r.identical ? ", identical" : (r.ties ? ", ties ordered differently" : ", DIFFERENT"),
                r.fm_sorted ? "" : ", project's own result is not sorted under its comparison");
    std::fflush(stdout);
}

inline bool write_csv(const std::string& path, const std::vector<Row>& rows) {
    FILE* out = std::fopen(path.c_str(), "w");
    if (!out) {
        std::perror(path.c_str());
        return false;
    }
    std::fprintf(out, "project,dataset,n,role,order,fm_resort_ms,partial_sort_ms,bz_presort_ms,bz_switch_ms,bz_cached_ms,"
                      "first_page_cost,fm_sorted,ties,bz_valid,identical,order_hash\n");
    for (const Row& r : rows) {
        std::fprintf(out, "%s,%s,%zu,%s,%s,%.4f,%.4f,%.4f,%.4f,%.5f,%zu,%d,%zu,%d,%d,%016llx\n", r.project.c_str(),
                     r.dataset.c_str(), r.n, r.role.c_str(), r.order.c_str(), r.fm_resort, r.partial_sort, r.bz_presort,
                     r.bz_switch, r.bz_cached, r.first_page_cost, r.fm_sorted ? 1 : 0, r.ties, r.bz_valid ? 1 : 0,
                     r.identical ? 1 : 0, static_cast<unsigned long long>(r.order_hash));
    }
    std::fclose(out);
    return true;
}

/// A Bucketization order is acceptable when it is sorted under the project's
/// comparison. It need not be identical: projects with ties (equal names in
/// flattened trees, case-insensitive duplicates) leave the order of tied items
/// to their sort algorithm.
inline bool all_valid(const std::vector<Row>& rows) {
    return std::all_of(rows.begin(), rows.end(), [](const Row& r) { return r.bz_valid; });
}

}  // namespace fm_bench
