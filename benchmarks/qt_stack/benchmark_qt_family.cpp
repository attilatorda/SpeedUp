// Bucketization against the real sort paths of the Qt-based file managers
// other than Dolphin (benchmarks/kde_stack covers Dolphin):
// - PCManFM-Qt (libfm-qt): QSortFilterProxyModel::sort () with ProxyFolderModel::lessThan ()
// - KDE file dialogs (KIO): QSortFilterProxyModel::sort () with KDirSortFilterProxyModel's comparison
// - Krusader: ListModel::sort (): KrSort::Sorter (std::stable_sort) plus rebuilding its lookup hashes
// The comparisons are in qt_family_sort.h; Qt's own proxy model does the sorting.
//
// Usage: speedup_benchmark_qt_family [options] DATASET.tsv...   (see fm_harness.h; --locale takes en_US)

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../common/dataset.h"
#include "../common/fm_harness.h"
#include "icu_collation_key.h"
#include "qt_family_sort.h"

namespace bz = speedup::bucketization;
using namespace qt_family;
using fm_bench::Case;
using fm_bench::Clock;
using fm_bench::ms_since;

namespace {

std::size_t folders_first(const Item& f) { return f->is_dir ? 0 : 1; }

/// A three-way comparison from "a comes before b".
template <class Before>
int three_way(const Before& before, const Item& a, const Item& b) {
    if (a == b) return 0;
    if (before(a, b)) return -1;
    return before(b, a) ? 1 : 0;
}

/// Order of a proxy model's rows as source items.
std::vector<Item> proxy_order(const QSortFilterProxyModel& proxy, const std::vector<Item>& listing) {
    std::vector<Item> out;
    out.reserve(listing.size());
    for (int i = 0; i < proxy.rowCount(); ++i) {
        out.push_back(listing[static_cast<std::size_t>(proxy.mapToSource(proxy.index(i, 0)).row())]);
    }
    return out;
}

/// What clicking a column header does in a view on a QSortFilterProxyModel:
/// QSortFilterProxyModel::sort () on the new column or order.
double proxy_resort(QSortFilterProxyModel& proxy, int previous_column, int column, Qt::SortOrder order) {
    proxy.sort(previous_column, Qt::AscendingOrder);
    const auto t = Clock::now();
    proxy.sort(column, order);
    return ms_since(t);
}

// ---------------------------------------------------------------------------
// PCManFM-Qt
// ---------------------------------------------------------------------------
struct PcmanfmStack {
    explicit PcmanfmStack(const std::vector<Item>& listing) : source(listing) { proxy.setSourceModel(&source); }
    pcmanfm::FolderModel source;
    pcmanfm::ProxyFolderModel proxy;

    /// ProxyFolderModel's order: ascending is lessThan (a, b); QSortFilterProxyModel
    /// sorts descending with lessThan (b, a).
    int compare(Role role, bool descending, const Item& a, const Item& b) const {
        const int column = pcmanfm::column_of(role);
        const Qt::SortOrder order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        return three_way(
            [&](const Item& x, const Item& y) {
                const QModelIndex ix = source.index(x->row, column);
                const QModelIndex iy = source.index(y->row, column);
                return descending ? proxy.lessThanFor(iy, ix, column, order) : proxy.lessThanFor(ix, iy, column, order);
            },
            a, b);
    }
};

Case<Item> pcmanfm_case(Role role, bool descending, const std::shared_ptr<PcmanfmStack>& stack,
                        const std::shared_ptr<IcuPrimaryKey>& numeric_key, const std::vector<Item>& listing) {
    Case<Item> c;
    c.project = "PCManFM-Qt";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [stack, role, descending](const Item& a, const Item& b) { return stack->compare(role, descending, a, b); };
    c.resort = [stack, role, descending, &listing](const std::vector<Item>&, std::vector<Item>& out) {
        const double elapsed = proxy_resort(stack->proxy, pcmanfm::column_of(static_cast<Role>((role + 1) % 3)),
                                            pcmanfm::column_of(role), descending ? Qt::DescendingOrder : Qt::AscendingOrder);
        out = proxy_order(stack->proxy, listing);
        return elapsed;
    };
    c.make_key = [stack, role, numeric_key] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = [numeric_key](const Item& f) { return (*numeric_key)(pcmanfm::ProxyFolderModel::firstPart(f->name)); };
        } else if (role == kSize) {
            key = [](const Item& f) { return static_cast<std::uint64_t>(f->size); };
        } else {
            key = [](const Item& f) { return static_cast<std::uint64_t>(f->mtime); };  // quint64 in Fm::FileInfo
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [stack, role](const Item& a, const Item& b) { return stack->compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// KDE file dialogs (KIO)
// ---------------------------------------------------------------------------
struct KioStack {
    explicit KioStack(const std::vector<Item>& listing) : source(listing) { proxy.setSourceModel(&source); }
    kio::KDirModel source;
    kio::KDirSortFilterProxyModel proxy;

    int compare(Role role, bool descending, const Item& a, const Item& b) const {
        const int column = kio::column_of(role);
        const Qt::SortOrder order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
        return three_way(
            [&](const Item& x, const Item& y) {
                const QModelIndex ix = source.index(x->row, column);
                const QModelIndex iy = source.index(y->row, column);
                return descending ? proxy.subSortLessThanFor(iy, ix, order) : proxy.subSortLessThanFor(ix, iy, order);
            },
            a, b);
    }
};

Case<Item> kio_case(Role role, bool descending, const std::shared_ptr<KioStack>& stack,
                    const std::shared_ptr<IcuPrimaryKey>& numeric_key, const std::vector<Item>& listing) {
    Case<Item> c;
    c.project = "KIO dialogs";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [stack, role, descending](const Item& a, const Item& b) { return stack->compare(role, descending, a, b); };
    c.resort = [stack, role, descending, &listing](const std::vector<Item>&, std::vector<Item>& out) {
        const double elapsed = proxy_resort(stack->proxy, kio::column_of(static_cast<Role>((role + 1) % 3)),
                                            kio::column_of(role), descending ? Qt::DescendingOrder : Qt::AscendingOrder);
        out = proxy_order(stack->proxy, listing);
        return elapsed;
    };
    c.make_key = [stack, role, numeric_key] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = [numeric_key](const Item& f) { return (*numeric_key)(f->name); };
        } else if (role == kSize) {
            key = [](const Item& f) { return f->is_dir ? std::uint64_t{0} : static_cast<std::uint64_t>(f->size); };
        } else {
            key = [](const Item& f) { return bz::keys::from_signed(f->mtime_ns); };  // seconds, then nanoseconds
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [stack, role](const Item& a, const Item& b) { return stack->compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// Krusader
// ---------------------------------------------------------------------------
struct KrusaderStack {
    explicit KrusaderStack(const std::vector<Item>& listing) {
        for (Item f : listing) {
            items.push_back(krusader::FileItem{f});
        }
        model = std::make_unique<krusader::ListModel>(items);
        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 2; ++d) {
                properties[r][d].sortColumn = krusader::column_of(static_cast<Role>(r));
                if (d == 1) {
                    properties[r][d].sortOptions |= krusader::KrViewProperties::Descending;
                }
                auto& props = sort_props[r][d];
                props.reserve(items.size());
                for (std::size_t i = 0; i < items.size(); ++i) {
                    props.emplace_back(&items[i], properties[r][d].sortColumn, &properties[r][d], false, d == 0,
                                       static_cast<int>(i));
                }
            }
        }
    }
    std::vector<krusader::FileItem> items;
    std::unique_ptr<krusader::ListModel> model;
    krusader::KrViewProperties properties[3][2];
    std::vector<krusader::SortProps> sort_props[3][2];  // for checking orders and tie-breaking

    /// The order Krusader means: ascending by itemLessThan (); descending puts a
    /// before b when itemLessThan (b, a) (Krusader's own descending comparison,
    /// !itemLessThan (), also says "before" for equal items).
    int compare(Role role, bool descending, const Item& a, const Item& b) {
        auto& props = sort_props[role][descending ? 1 : 0];
        return three_way(
            [&](const Item& x, const Item& y) {
                krusader::SortProps* px = &props[static_cast<std::size_t>(x->row)];
                krusader::SortProps* py = &props[static_cast<std::size_t>(y->row)];
                return descending ? krusader::itemLessThan(py, px) : krusader::itemLessThan(px, py);
            },
            a, b);
    }
};

Case<Item> krusader_case(Role role, bool descending, const std::shared_ptr<KrusaderStack>& stack,
                         const std::shared_ptr<IcuPrimaryKey>& plain_key) {
    Case<Item> c;
    c.project = "Krusader";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [stack, role, descending](const Item& a, const Item& b) { return stack->compare(role, descending, a, b); };
    c.resort = [stack, role, descending](const std::vector<Item>& previous, std::vector<Item>& out) {
        std::vector<krusader::FileItem*> order;
        order.reserve(previous.size());
        for (Item f : previous) {
            order.push_back(&stack->items[static_cast<std::size_t>(f->row)]);
        }
        stack->model->setItems(order);
        const auto t = Clock::now();
        stack->model->sort(&stack->properties[role][descending ? 1 : 0]);
        const double elapsed = ms_since(t);
        out.clear();
        for (krusader::FileItem* item : stack->model->fileItems()) {
            out.push_back(const_cast<File*>(item->file));
        }
        return elapsed;
    };
    c.make_key = [stack, role, plain_key] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            // Names are lowercased (IgnoreCase), then compared with localeAwareCompare.
            key = [plain_key](const Item& f) { return (*plain_key)(f->name.toLower()); };
        } else if (role == kSize) {
            key = [](const Item& f) { return static_cast<std::uint64_t>(f->size); };
        } else {
            key = [](const Item& f) { return bz::keys::from_signed(f->mtime); };
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [stack, role](const Item& a, const Item& b) { return stack->compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    fm_bench::Options opt = fm_bench::parse_options(argc, argv);
    if (opt.locale == "en_US.UTF-8") {
        opt.locale = "en_US";
    }
    if (opt.datasets.empty()) {
        std::fprintf(stderr, "usage: %s %s\n", argv[0], fm_bench::kUsage);
        return 2;
    }
    const QLocale locale(QString::fromStdString(opt.locale));
    QLocale::setDefault(locale);
    auto numeric_key = std::make_shared<IcuPrimaryKey>(locale, true);
    auto plain_key = std::make_shared<IcuPrimaryKey>(locale, false);
    std::printf("Qt family: Qt %s, locale %s, page %zu\n", qVersion(), opt.locale.c_str(), opt.page);
    std::vector<fm_bench::Row> rows;

    for (const std::string& path : opt.datasets) {
        const auto entries = speedup_bench::load_listing(path);
        const std::string dataset = speedup_bench::dataset_name(path);
        if (opt.max_entries != 0 && entries.size() > opt.max_entries) {
            std::printf("\n%s: %zu entries, skipped (--max-entries)\n", dataset.c_str(), entries.size());
            continue;
        }
        std::vector<File> storage(entries.size());
        std::vector<Item> listing;
        listing.reserve(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            File& f = storage[i];
            f.row = static_cast<int>(i);
            f.path = entries[i].path;
            f.name = QString::fromStdString(entries[i].name);
            f.url = QUrl::fromLocalFile(QStringLiteral("/bench/") + QString::fromStdString(entries[i].path));
            f.is_dir = entries[i].is_dir;
            f.is_hidden = entries[i].is_hidden;
            f.size = entries[i].size;
            f.mtime_ns = entries[i].mtime_ns;
            f.mtime = entries[i].mtime_ns / 1000000000;
            listing.push_back(&f);
        }
        std::printf("\n%s: %zu entries\n", dataset.c_str(), listing.size());

        auto run = [&](const Case<Item>& c, const Case<Item>& previous_case) {
            const std::vector<Item> previous = fm_bench::sorted_by(listing, previous_case.compare);
            rows.push_back(fm_bench::run_case(c, dataset, listing, previous, opt));
            fm_bench::print_row(rows.back());
        };
        for (int r = 0; r < 3; ++r) {
            const Role role = static_cast<Role>(r);
            const Role previous_role = static_cast<Role>((r + 1) % 3);
            for (int d = 0; d < 2; ++d) {
                if (opt.wants("PCManFM-Qt")) {
                    auto stack = std::make_shared<PcmanfmStack>(listing);
                    run(pcmanfm_case(role, d == 1, stack, numeric_key, listing),
                        pcmanfm_case(previous_role, false, stack, numeric_key, listing));
                }
                if (opt.wants("KIO dialogs")) {
                    auto stack = std::make_shared<KioStack>(listing);
                    run(kio_case(role, d == 1, stack, numeric_key, listing), kio_case(previous_role, false, stack, numeric_key, listing));
                }
                if (opt.wants("Krusader")) {
                    auto stack = std::make_shared<KrusaderStack>(listing);
                    run(krusader_case(role, d == 1, stack, plain_key), krusader_case(previous_role, false, stack, plain_key));
                }
            }
        }
    }
    if (!opt.csv.empty() && !fm_bench::write_csv(opt.csv, rows)) {
        return 1;
    }
    const bool ok = fm_bench::all_valid(rows);
    std::printf("\n%s\n", ok ? "Every Bucketization order is sorted under the project's own comparison."
                             : "SOME BUCKETIZATION ORDERS ARE NOT SORTED UNDER THE PROJECT'S COMPARISON");
    return ok ? 0 : 1;
}
