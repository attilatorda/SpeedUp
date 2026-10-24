// Bucketization against the GNOME-family sorting stack: names compared through
// cached g_utf8_collate_key_for_filename() keys with strcmp (Nautilus, Thunar,
// Nemo and Caja all cache these keys), folders first, and a full re-sort on
// every sort change:
// - g_sequence_sort(), what Thunar, Nemo and Caja call on their GSequence models
// - std::stable_sort, standing in for GTK 4's GtkSortListModel (Nautilus),
//   which also re-sorts every item with the sorter's comparison
// - std::partial_sort of the first page
//
// Usage: speedup_benchmark_glib_stack [--csv FILE] [--repeats N] [--locale en_US.UTF-8] DATASET.tsv...

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../common/dataset.h"
#include "speedup/bucketization.hpp"

namespace bz = speedup::bucketization;
using Clock = std::chrono::steady_clock;

namespace {

struct File {
    std::string path;
    bool is_dir = false;
    std::uint64_t size = 0;
    std::int64_t mtime = 0;  // seconds
    char* collate_key = nullptr;  // g_utf8_collate_key_for_filename(name)
};
using Item = File*;

enum Role { kName, kSize, kModified };
const char* role_name(int r) { return r == kName ? "name" : r == kSize ? "size" : "modified"; }

struct Order {
    Role role;
    bool descending;
};

/// Folders first in both directions, then the role, then the name key, then the path.
int compare(const Order& order, const File* a, const File* b) {
    if (a == b) {
        return 0;
    }
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    int c = 0;
    if (order.role == kSize && !a->is_dir) {
        c = (a->size > b->size) - (a->size < b->size);
    } else if (order.role == kModified) {
        c = (a->mtime > b->mtime) - (a->mtime < b->mtime);
    }
    if (c == 0) {
        c = std::strcmp(a->collate_key, b->collate_key);
    }
    if (c == 0) {
        c = a->path.compare(b->path);
    }
    c = (c > 0) - (c < 0);
    return order.descending ? -c : c;
}

gint sequence_compare(gconstpointer a, gconstpointer b, gpointer order) {
    return compare(*static_cast<const Order*>(order), static_cast<const File*>(a), static_cast<const File*>(b));
}

constexpr std::uint64_t kFilesAfterFolders = std::uint64_t{1} << 63;

std::uint64_t key_of(const Order& order, const File* f) {
    std::uint64_t value = 0;
    if (order.role == kName) {
        value = bz::keys::from_bytes(f->collate_key, std::strlen(f->collate_key));
    } else if (order.role == kSize) {
        value = f->is_dir ? 0 : f->size;
    } else {
        value = bz::keys::from_signed(f->mtime);
    }
    if (order.descending && !(order.role == kSize && f->is_dir)) {
        value = ~value;
    }
    return (f->is_dir ? 0 : kFilesAfterFolders) | (value >> 1);
}

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

template <class F>
double median_of(int times, F&& f) {
    std::vector<double> v;
    for (int i = 0; i < times; ++i) {
        v.push_back(f());
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    std::string csv;
    std::string locale = "en_US.UTF-8";
    int repeats = 5;
    std::vector<std::string> datasets;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--csv") csv = next();
        else if (arg == "--repeats") repeats = std::stoi(next());
        else if (arg == "--locale") locale = next();
        else datasets.push_back(arg);
    }
    if (datasets.empty() || !std::setlocale(LC_ALL, locale.c_str())) {
        std::fprintf(stderr, "usage: %s [--csv FILE] [--repeats N] [--locale L] DATASET.tsv...  (locale must exist)\n", argv[0]);
        return 2;
    }
    const std::size_t page = 64;
    const bz::BucketPlan plan = bz::BucketPlan::defaults().with_page_size(page);
    FILE* out = csv.empty() ? nullptr : std::fopen(csv.c_str(), "w");
    if (out) {
        std::fprintf(out, "dataset,n,role,order,collate_keys_ms,g_sequence_sort_ms,stable_sort_ms,partial_sort_ms,"
                          "keyed_presort_ms,keyed_switch_ms,keyed_cached_ms,order_ok\n");
    }
    std::printf("GNOME stack: GLib %u.%u, locale %s, page %zu\n", glib_major_version, glib_minor_version, locale.c_str(), page);
    bool all_ok = true;

    for (const std::string& file : datasets) {
        const auto entries = speedup_bench::load_listing(file);
        const std::string name = speedup_bench::dataset_name(file);
        const std::size_t n = entries.size();
        const int reps = n > 300000 ? std::max(1, repeats / 3) : repeats;
        std::vector<File> storage(n);
        for (std::size_t i = 0; i < n; ++i) {
            storage[i].path = entries[i].path;
            storage[i].is_dir = entries[i].is_dir;
            storage[i].size = entries[i].size;
            storage[i].mtime = entries[i].mtime_ns / 1000000000;
        }
        // The file managers compute these once per file when it is loaded.
        const auto tk = Clock::now();
        for (std::size_t i = 0; i < n; ++i) {
            storage[i].collate_key = g_utf8_collate_key_for_filename(entries[i].name.c_str(), -1);
        }
        const double collate_ms = ms_since(tk);
        std::vector<Item> listing(n);
        for (std::size_t i = 0; i < n; ++i) {
            listing[i] = &storage[i];
        }
        std::printf("\n%s: %zu entries, collation keys %.3f ms\n", name.c_str(), n, collate_ms);

        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 2; ++d) {
                const Order order{static_cast<Role>(r), d == 1};
                const Order previous{static_cast<Role>((r + 1) % 3), false};
                auto less = [&](const Item& a, const Item& b) { return compare(order, a, b) < 0; };
                std::vector<Item> previous_order = listing;
                std::stable_sort(previous_order.begin(), previous_order.end(),
                                 [&](const Item& a, const Item& b) { return compare(previous, a, b) < 0; });
                std::vector<Item> reference = previous_order;
                std::stable_sort(reference.begin(), reference.end(), less);
                bool ok = true;

                const double seq_ms = median_of(reps, [&] {
                    GSequence* seq = g_sequence_new(nullptr);
                    for (Item item : previous_order) {
                        g_sequence_append(seq, item);
                    }
                    const auto t = Clock::now();
                    g_sequence_sort(seq, sequence_compare, const_cast<Order*>(&order));
                    const double elapsed = ms_since(t);
                    GSequenceIter* it = g_sequence_get_begin_iter(seq);
                    for (std::size_t i = 0; i < std::min(page, n); ++i, it = g_sequence_iter_next(it)) {
                        ok = ok && g_sequence_get(it) == reference[i];
                    }
                    g_sequence_free(seq);
                    return elapsed;
                });
                const double stable_ms = median_of(reps, [&] {
                    std::vector<Item> copy = previous_order;
                    const auto t = Clock::now();
                    std::stable_sort(copy.begin(), copy.end(), less);
                    return ms_since(t);
                });
                const double partial_ms = median_of(reps, [&] {
                    std::vector<Item> copy = previous_order;
                    const auto t = Clock::now();
                    std::partial_sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(std::min(page, n)), copy.end(), less);
                    return ms_since(t);
                });

                bz::BucketIndex<Item> index(plan);
                index.add_all(listing);
                index.add_sort_key(bz::SortKey<Item>::of_key(
                    "other", [](const Item& f) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(f)); }));
                const auto key = bz::SortKey<Item>::of_key(
                    "k", [order](const Item& f) { return key_of(order, f); },
                    [order](const Item& a, const Item& b) { return compare(order, a, b); });
                const double presort_ms = median_of(reps, [&] {
                    index.remove_sort_key("k");
                    const auto t = Clock::now();
                    index.add_sort_key(key);
                    return ms_since(t);
                });
                const double switch_ms = median_of(reps, [&] {
                    index.rebalance("k");
                    index.sort_by("other");
                    const auto t = Clock::now();
                    index.sort_by("k");
                    const auto first = index.first_page();
                    const double elapsed = ms_since(t);
                    for (std::size_t i = 0; i < first.size(); ++i) {
                        ok = ok && first[i] == reference[i];
                    }
                    return elapsed;
                });
                const double cached_ms = median_of(reps, [&] {
                    index.sort_by("other");
                    const auto t = Clock::now();
                    index.sort_by("k");
                    index.first_page();
                    return ms_since(t);
                });
                index.sort_by("k");
                std::size_t position = 0;
                index.for_each([&](const Item& item) { ok = ok && reference[position++] == item; });
                all_ok = all_ok && ok;

                std::printf("  %-8s %-4s g_sequence_sort %9.3f | stable_sort %9.3f | partial_sort %8.3f | keyed: presort %8.3f switch %7.3f cached %7.4f%s\n",
                            role_name(r), d ? "desc" : "asc", seq_ms, stable_ms, partial_ms, presort_ms, switch_ms, cached_ms,
                            ok ? "" : "  MISMATCH");
                if (out) {
                    std::fprintf(out, "%s,%zu,%s,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%d\n", name.c_str(), n, role_name(r),
                                 d ? "desc" : "asc", collate_ms, seq_ms, stable_ms, partial_ms, presort_ms, switch_ms, cached_ms, ok ? 1 : 0);
                }
            }
        }
        for (File& f : storage) {
            g_free(f.collate_key);
        }
    }
    if (out) {
        std::fclose(out);
    }
    std::printf("\n%s\n", all_ok ? "All Bucketization orders match the reference order." : "SOME ORDERS DO NOT MATCH");
    return all_ok ? 0 : 1;
}
