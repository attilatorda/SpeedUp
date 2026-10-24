// File browser example: list a directory, then switch between sort columns
// the way a file manager does when a column header is clicked.
//
// Usage: speedup_example_file_browser [directory] [rows]

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "speedup/bucketization.hpp"

namespace bz = speedup::bucketization;
namespace fs = std::filesystem;

namespace {

struct Entry {
    std::string name;
    bool is_dir = false;
    std::int64_t size = 0;
    std::int64_t modified = 0;  // Unix time, seconds
};

/// "file2" < "file10", case-insensitive. Equal names fall back to byte order
/// so the order is total.
int natural_compare(std::string_view a, std::string_view b) {
    auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            std::size_t si = i;
            std::size_t sj = j;
            while (si < a.size() && a[si] == '0') ++si;
            while (sj < b.size() && b[sj] == '0') ++sj;
            std::size_t ei = si;
            std::size_t ej = sj;
            while (ei < a.size() && digit(a[ei])) ++ei;
            while (ej < b.size() && digit(b[ej])) ++ej;
            if (ei - si != ej - sj) {
                return ei - si < ej - sj ? -1 : 1;
            }
            const int c = a.substr(si, ei - si).compare(b.substr(sj, ej - sj));
            if (c != 0) {
                return c < 0 ? -1 : 1;
            }
            i = ei;
            j = ej;
            continue;
        }
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[j]));
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ++i;
        ++j;
    }
    if (a.size() - i != b.size() - j) {
        return a.size() - i < b.size() - j ? -1 : 1;
    }
    const int c = a.compare(b);
    return (c > 0) - (c < 0);
}

void print_page(bz::Bucketization<Entry>& view, std::size_t rows) {
    const auto start = std::chrono::steady_clock::now();
    const std::vector<Entry> page = view.range(0, rows);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const char* dir = view.direction() == bz::Direction::ascending ? "ascending" : "descending";
    std::printf("\n== sorted by %s (%s), first page in %.3f ms ==\n", view.active_sort_key()->name().c_str(), dir, ms);
    for (const Entry& e : page) {
        std::printf("  %-40.40s %12lld %14lld%s\n", e.name.c_str(), static_cast<long long>(e.size),
                    static_cast<long long>(e.modified), e.is_dir ? "  <dir>" : "");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::current_path();
    const std::size_t rows = argc > 2 ? std::stoul(argv[2]) : 10;

    std::vector<Entry> entries;
    std::error_code ec;
    for (const fs::directory_entry& de : fs::directory_iterator(root, ec)) {
        Entry e;
        e.name = de.path().filename().string();
        e.is_dir = de.is_directory(ec);
        e.size = e.is_dir ? 0 : static_cast<std::int64_t>(de.file_size(ec));
        // C++17 has no clock_cast; shift the file clock onto the system clock.
        const auto written = de.last_write_time(ec);
        const auto as_system = std::chrono::system_clock::now() +
                               std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                   written - fs::file_time_type::clock::now());
        e.modified = std::chrono::duration_cast<std::chrono::seconds>(as_system.time_since_epoch()).count();
        entries.push_back(std::move(e));
    }
    if (ec) {
        std::fprintf(stderr, "cannot list %s: %s\n", root.string().c_str(), ec.message().c_str());
        return 1;
    }

    // The first bucket holds one screen of rows.
    bz::BucketIndex<Entry> index(bz::BucketPlan::defaults().with_page_size(rows));
    index.add_sort_key(bz::SortKey<Entry>::of("name", &Entry::name));
    index.add_sort_key(bz::SortKey<Entry>::of_comparator(
        "natural", [](const Entry& a, const Entry& b) { return natural_compare(a.name, b.name); }));
    index.add_sort_key(bz::SortKey<Entry>::of("size", &Entry::size));
    // Folders stay first in both directions: a group, not part of the key.
    index.add_sort_key(bz::SortKey<Entry>::of("modified", &Entry::modified).with_groups(2, [](const Entry& e) {
        return e.is_dir ? std::size_t{0} : std::size_t{1};
    }));
    index.add_all(entries);  // pre-sorting: every entry picks a bucket per sort key

    std::printf("%zu entries in %s\n", index.size(), root.string().c_str());

    index.sort_by("natural");
    print_page(index, rows);
    index.sort_by("size", bz::Direction::descending);
    print_page(index, rows);
    index.sort_by("modified", bz::Direction::descending);  // folders first, newest first
    print_page(index, rows);
    index.sort_by("name");
    print_page(index, rows);

    const bz::BucketStats stats = index.stats("size");
    std::printf("\nsize key: %zu buckets, first page needs %zu (asc) / %zu (desc) elements sorted, max skew %.2f\n",
                stats.bucket_count, stats.first_page_cost_ascending, stats.first_page_cost_descending,
                stats.max_skew);
    return 0;
}
