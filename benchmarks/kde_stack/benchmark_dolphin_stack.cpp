// Bucketization against Dolphin's real sorting stack (Qt QCollator on ICU,
// Dolphin's natural comparison, folders first, its parallel merge sort and the
// bookkeeping of KFileItemModel::resortAllItems()).
//
// Usage: speedup_benchmark_dolphin_stack [--csv FILE] [--repeats N] [--locale en_US]
//                                        [--comparator-limit N] DATASET.tsv...
//
// Datasets come from benchmarks/datasets/capture_listing.py. Every Bucketization
// order is checked against Dolphin's order; a mismatch is reported and fails the run.

#include <QCoreApplication>
#include <QLocale>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../common/dataset.h"
#include "dolphin_sort.h"
#include "icu_natural_key.h"
#include "speedup/bucketization.hpp"

namespace bz = speedup::bucketization;
using dolphin::BenchItem;
using Item = BenchItem*;
using Clock = std::chrono::steady_clock;

namespace {

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return -1;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

const char* role_name(dolphin::Model::RoleType role) {
    switch (role) {
        case dolphin::Model::NameRole: return "name";
        case dolphin::Model::SizeRole: return "size";
        case dolphin::Model::ModificationTimeRole: return "modified";
    }
    return "?";
}

/// Dolphin's order as a three-way comparator for Bucketization.
std::function<int(const Item&, const Item&)> dolphin_compare(const dolphin::Model* model) {
    return [model](const Item& a, const Item& b) {
        if (a == b) {
            return 0;
        }
        return model->lessThan(a, b, model->m_collator) ? -1 : 1;  // never equal for distinct items
    };
}

/// Keyed variant: the role's normalized key. Folders-first is a group
/// (SortKey::with_groups), so one key serves both directions: Dolphin keeps
/// folders first when the order is descending, and so do Bucketization groups.
std::function<std::uint64_t(const Item&)> dolphin_key(dolphin::Model::RoleType role, const IcuNaturalPrefixKey* icu) {
    switch (role) {
        case dolphin::Model::NameRole:
            return [icu](const Item& item) { return (*icu)(item->text); };
        case dolphin::Model::SizeRole:
            // Folders: no counts known yet, all equal; Dolphin orders them by name.
            return [](const Item& item) { return item->isDir ? std::uint64_t{0} : item->size; };
        case dolphin::Model::ModificationTimeRole:
            return [](const Item& item) { return bz::keys::from_signed(item->modificationTime); };
    }
    return {};
}

std::size_t folders_first(const Item& item) { return item->isDir ? 0 : 1; }

struct Row {
    std::string dataset;
    std::size_t n = 0;
    std::string role;
    std::string order;
    double dolphin_resort = -1;    // full KFileItemModel::resortAllItems()
    double dolphin_sort = -1;      // just its sort() call
    double partial_sort = -1;      // std::partial_sort of the first page, Dolphin comparator
    double cmp_presort = -1;       // Bucketization, comparator mode: add_sort_key on loaded items
    double cmp_switch = -1;        // sort_by + first_page, grouping not cached
    double cmp_cached = -1;        // sort_by + first_page, grouping cached
    double key_presort = -1;       // Bucketization, keyed mode (ICU prefix / numbers)
    double key_switch = -1;
    double key_cached = -1;
    std::size_t first_page_cost = 0;  // elements sorted for the first page (keyed mode)
    bool order_ok = true;
    // False when std::stable_sort with Dolphin's comparison does not reproduce
    // Dolphin's own order either: the comparison has cycles on this data, so
    // the result depends on the sort algorithm and no exact match is possible.
    bool comparator_consistent = true;
};

struct OpenRow {
    std::string dataset;
    std::size_t n = 0;
    double dolphin_open = -1;     // insertItems() sorting into an empty model, name ascending
    double key_open = -1;         // keyed name key: presort + first page
    double key_open_all = -1;     // same, with name, size and modified all pre-sorted
    double cmp_open = -1;
};

struct Options {
    std::string csv;
    int repeats = 5;
    std::string locale = "en_US";
    std::size_t comparator_limit = 200000;  // comparator mode is slow to pre-sort on huge sets
    std::size_t page = 64;
    std::vector<std::string> datasets;
};

template <class F>
std::vector<double> repeat(int times, F&& f) {
    std::vector<double> out;
    for (int i = 0; i < times; ++i) {
        out.push_back(f());
    }
    return out;
}

bool same_prefix(const std::vector<Item>& page, const QList<Item>& reference) {
    if (page.size() > static_cast<std::size_t>(reference.size())) {
        return false;
    }
    for (std::size_t i = 0; i < page.size(); ++i) {
        if (page[i] != reference[static_cast<int>(i)]) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (arg == "--csv") opt.csv = next();
        else if (arg == "--repeats") opt.repeats = std::stoi(next());
        else if (arg == "--locale") opt.locale = next();
        else if (arg == "--comparator-limit") opt.comparator_limit = std::stoul(next());
        else opt.datasets.push_back(arg);
    }
    if (opt.datasets.empty()) {
        std::fprintf(stderr, "usage: %s [--csv FILE] [--repeats N] [--locale en_US] DATASET.tsv...\n", argv[0]);
        return 2;
    }

    const QLocale locale(QString::fromStdString(opt.locale));
    QLocale::setDefault(locale);
    const IcuNaturalPrefixKey icu(locale);
    const bz::BucketPlan plan = bz::BucketPlan::defaults().with_page_size(opt.page);
    std::printf("Dolphin stack: Qt %s, %d threads for Dolphin's parallel sort, locale %s, page %zu\n",
                qVersion(), QThread::idealThreadCount(), opt.locale.c_str(), opt.page);

    std::vector<Row> rows;
    std::vector<OpenRow> opens;
    bool all_ok = true;

    for (const std::string& file : opt.datasets) {
        const std::vector<speedup_bench::Entry> entries = speedup_bench::load_listing(file);
        const std::string name = speedup_bench::dataset_name(file);
        const std::size_t n = entries.size();
        const int reps = n > 300000 ? std::max(1, opt.repeats / 3) : opt.repeats;
        const bool run_comparator = n <= opt.comparator_limit;
        std::vector<BenchItem> storage(n);
        QList<Item> listing;
        listing.reserve(static_cast<int>(n));
        for (std::size_t i = 0; i < n; ++i) {
            BenchItem& it = storage[i];
            it.text = QString::fromStdString(entries[i].name);
            it.name = it.text;
            it.url = QUrl::fromLocalFile(QStringLiteral("/bench/") + QString::fromStdString(entries[i].path));
            it.isDir = entries[i].is_dir;
            it.isHidden = entries[i].is_hidden;
            it.size = entries[i].size;
            it.modificationTime = entries[i].mtime_ns / 1000000000;
            listing.append(&it);
        }
        const std::vector<Item> listing_vec(listing.begin(), listing.end());
        std::printf("\n%s: %zu entries (%d repeats)\n", name.c_str(), n, reps);

        // One configured model per (role, order): the comparators read its settings.
        std::unique_ptr<dolphin::Model> models[3][2];
        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 2; ++d) {
                models[r][d] = std::make_unique<dolphin::Model>(locale);
                models[r][d]->m_sortRole = static_cast<dolphin::Model::RoleType>(r);
                models[r][d]->m_sortOrder = d == 0 ? Qt::AscendingOrder : Qt::DescendingOrder;
            }
        }
        // One sort key per role; descending order is the same key read backwards inside each group.
        auto make_key = [&](int r, bool keyed) {
            const auto role = static_cast<dolphin::Model::RoleType>(r);
            const std::string key_name = std::string(role_name(role)) + (keyed ? "" : "-cmp");
            if (keyed) {
                return bz::SortKey<Item>::of_key(key_name, dolphin_key(role, &icu), dolphin_compare(models[r][0].get()))
                    .with_groups(2, folders_first);
            }
            return bz::SortKey<Item>::of_comparator(key_name, dolphin_compare(models[r][0].get()))
                .with_groups(2, folders_first);
        };
        const auto other_key = bz::SortKey<Item>::of_key(
            "other", [](const Item& item) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(item)); });

        // --- Opening the folder (name, ascending) ---------------------------------
        OpenRow open;
        open.dataset = name;
        open.n = n;
        open.dolphin_open = median(repeat(reps, [&] {
            dolphin::Model model(locale);
            const auto t = Clock::now();
            model.insertItemsIntoEmptyModel(listing);
            return ms_since(t);
        }));
        open.key_open = median(repeat(reps, [&] {
            const auto t = Clock::now();
            bz::BucketIndex<Item> index(plan);
            index.add_sort_key(make_key(0, true));
            index.add_all(listing_vec);
            index.sort_by("name");
            index.first_page();
            return ms_since(t);
        }));
        open.key_open_all = median(repeat(reps, [&] {
            const auto t = Clock::now();
            bz::BucketIndex<Item> index(plan);
            for (int r = 0; r < 3; ++r) {
                index.add_sort_key(make_key(r, true));
            }
            index.add_all(listing_vec);
            index.sort_by("name");
            index.first_page();
            return ms_since(t);
        }));
        if (run_comparator) {
            open.cmp_open = median(repeat(reps, [&] {
                const auto t = Clock::now();
                bz::BucketIndex<Item> index(plan);
                index.add_sort_key(make_key(0, false));
                index.add_all(listing_vec);
                index.sort_by("name-cmp");
                index.first_page();
                return ms_since(t);
            }));
        }
        opens.push_back(open);

        // --- Switching the sort role / order ---------------------------------------
        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 2; ++d) {
                Row row;
                row.dataset = name;
                row.n = n;
                row.role = role_name(static_cast<dolphin::Model::RoleType>(r));
                row.order = d == 0 ? "asc" : "desc";
                const int previous_role = (r + 1) % 3;

                // Dolphin: the items are sorted by the previous role, then the role changes.
                dolphin::Model model(locale);
                QList<Item> previous_order;
                auto establish_previous = [&] {
                    model.m_sortRole = static_cast<dolphin::Model::RoleType>(previous_role);
                    model.m_sortOrder = Qt::AscendingOrder;
                    model.m_itemData = listing;
                    model.resortAllItems();
                    previous_order = model.m_itemData;
                    model.m_sortRole = static_cast<dolphin::Model::RoleType>(r);
                    model.m_sortOrder = d == 0 ? Qt::AscendingOrder : Qt::DescendingOrder;
                };
                row.dolphin_resort = median(repeat(reps, [&] {
                    establish_previous();
                    const auto t = Clock::now();
                    model.resortAllItems();
                    return ms_since(t);
                }));
                const QList<Item> reference = model.m_itemData;
                {
                    std::vector<Item> check(previous_order.begin(), previous_order.end());
                    const dolphin::Model* m = models[r][d].get();
                    std::stable_sort(check.begin(), check.end(),
                                     [m](Item a, Item b) { return m->lessThan(a, b, m->m_collator); });
                    row.comparator_consistent = std::equal(check.begin(), check.end(), reference.begin());
                }
                row.dolphin_sort = median(repeat(reps, [&] {
                    QList<Item> copy = previous_order;
                    const auto t = Clock::now();
                    model.sort(copy.begin(), copy.end());
                    return ms_since(t);
                }));
                const std::size_t page = std::min(opt.page, n);
                row.partial_sort = median(repeat(reps, [&] {
                    std::vector<Item> copy(previous_order.begin(), previous_order.end());
                    const dolphin::Model* m = models[r][d].get();
                    const auto t = Clock::now();
                    std::partial_sort(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(page), copy.end(),
                                      [m](Item a, Item b) { return m->lessThan(a, b, m->m_collator); });
                    const double elapsed = ms_since(t);
                    if (!std::equal(copy.begin(), copy.begin() + static_cast<std::ptrdiff_t>(page), reference.begin())) {
                        row.order_ok = false;
                    }
                    return elapsed;
                }));

                // Bucketization: items loaded once, then the sort key is added (pre-sorting).
                auto measure = [&](bool keyed, double& presort, double& first_switch, double& cached) {
                    bz::BucketIndex<Item> index(plan);
                    index.add_all(listing_vec);
                    index.add_sort_key(other_key);
                    const auto key = make_key(r, keyed);
                    const auto direction = d == 0 ? bz::Direction::ascending : bz::Direction::descending;
                    presort = median(repeat(reps, [&] {
                        index.remove_sort_key(key.name());
                        const auto t = Clock::now();
                        index.add_sort_key(key);
                        return ms_since(t);
                    }));
                    first_switch = median(repeat(reps, [&] {
                        index.rebalance(key.name());  // drops the cached grouping
                        index.sort_by("other");
                        const auto t = Clock::now();
                        index.sort_by(key.name(), direction);
                        const std::vector<Item> first = index.first_page();
                        const double elapsed = ms_since(t);
                        if (!same_prefix(first, reference)) {
                            row.order_ok = false;
                        }
                        return elapsed;
                    }));
                    cached = median(repeat(reps, [&] {
                        index.sort_by("other");
                        const auto t = Clock::now();
                        index.sort_by(key.name(), direction);
                        index.first_page();
                        return ms_since(t);
                    }));
                    if (keyed) {
                        const bz::BucketStats stats = index.stats(key.name());
                        row.first_page_cost = d == 0 ? stats.first_page_cost_ascending : stats.first_page_cost_descending;
                    }
                    // Whole order must equal Dolphin's.
                    index.sort_by(key.name(), direction);
                    std::size_t position = 0;
                    index.for_each([&](const Item& item) {
                        if (reference[static_cast<int>(position++)] != item) {
                            row.order_ok = false;
                        }
                    });
                };
                measure(true, row.key_presort, row.key_switch, row.key_cached);
                if (run_comparator) {
                    measure(false, row.cmp_presort, row.cmp_switch, row.cmp_cached);
                }
                if (!row.order_ok && row.comparator_consistent) {
                    all_ok = false;
                    std::printf("  ORDER MISMATCH: %s %s %s\n", name.c_str(), row.role.c_str(), row.order.c_str());
                } else if (!row.order_ok) {
                    std::printf("  order differs from Dolphin's, but so does std::stable_sort with Dolphin's own comparison:"
                                " the comparison has cycles on this data (%s %s %s)\n",
                                name.c_str(), row.role.c_str(), row.order.c_str());
                }
                std::printf("  %-8s %-4s dolphin %9.3f ms (sort %9.3f) | partial_sort %8.3f | keyed: presort %8.3f switch %7.3f cached %7.4f"
                            " | cmp: presort %9.3f switch %7.3f%s\n",
                            row.role.c_str(), row.order.c_str(), row.dolphin_resort, row.dolphin_sort, row.partial_sort,
                            row.key_presort, row.key_switch, row.key_cached, row.cmp_presort, row.cmp_switch,
                            row.order_ok ? "" : "  MISMATCH");
                rows.push_back(row);
            }
        }
        std::printf("  open (name asc): dolphin %9.3f ms | keyed name %8.3f | keyed all 3 keys %8.3f | cmp %9.3f\n",
                    open.dolphin_open, open.key_open, open.key_open_all, open.cmp_open);
    }

    if (!opt.csv.empty()) {
        FILE* out = std::fopen(opt.csv.c_str(), "w");
        if (!out) {
            std::perror(opt.csv.c_str());
            return 1;
        }
        std::fprintf(out, "dataset,n,role,order,dolphin_resort_ms,dolphin_sort_ms,partial_sort_ms,"
                          "keyed_presort_ms,keyed_switch_ms,keyed_cached_ms,cmp_presort_ms,cmp_switch_ms,cmp_cached_ms,"
                          "first_page_cost,order_ok,dolphin_comparator_consistent\n");
        for (const Row& r : rows) {
            std::fprintf(out, "%s,%zu,%s,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.4f,%.4f,%.5f,%zu,%d,%d\n", r.dataset.c_str(), r.n,
                         r.role.c_str(), r.order.c_str(), r.dolphin_resort, r.dolphin_sort, r.partial_sort, r.key_presort,
                         r.key_switch, r.key_cached, r.cmp_presort, r.cmp_switch, r.cmp_cached, r.first_page_cost,
                         r.order_ok ? 1 : 0, r.comparator_consistent ? 1 : 0);
        }
        std::fprintf(out, "\ndataset,n,dolphin_open_ms,keyed_open_ms,keyed_open_all_keys_ms,cmp_open_ms\n");
        for (const OpenRow& o : opens) {
            std::fprintf(out, "%s,%zu,%.4f,%.4f,%.4f,%.4f\n", o.dataset.c_str(), o.n, o.dolphin_open, o.key_open,
                         o.key_open_all, o.cmp_open);
        }
        std::fclose(out);
    }
    std::printf("\n%s\n", all_ok ? "Every Bucketization order matches Dolphin's wherever Dolphin's comparison is consistent."
                                 : "SOME ORDERS DO NOT MATCH DOLPHIN'S");
    return all_ok ? 0 : 1;
}
