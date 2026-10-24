// Bucketization against the real sort paths of the GNOME-family file managers:
// - Thunar, Nemo, Caja: their list models' g_sequence_sort () re-sort with
//   their own comparisons, including the new_order bookkeeping they do for
//   rows-reordered (gnome_family_sort.h)
// - Nautilus: GTK 4's GtkSortListModel over a GtkTreeListModel, with a
//   GtkTreeListRowSorter around a GtkMultiSorter (folders first, then the
//   column), the model chain Nautilus builds in nautilus-view-model.c
//
// Usage: speedup_benchmark_gnome_family [options] DATASET.tsv...   (see fm_harness.h)

#include <glib.h>
#ifdef SPEEDUP_HAVE_GTK4
#include <gtk/gtk.h>
#endif

#include <clocale>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../common/dataset.h"
#include "../common/fm_harness.h"
#include "gnome_family_sort.h"

namespace bz = speedup::bucketization;
using namespace gnome_family;
using fm_bench::Case;
using fm_bench::Clock;
using fm_bench::ms_since;

namespace {

constexpr std::uint64_t kTop = std::uint64_t{1} << 63;

std::size_t folders_first(const Item& f) { return f->is_dir ? 0 : 1; }

// ---------------------------------------------------------------------------
// Thunar
// ---------------------------------------------------------------------------
int thunar_compare(const thunar::Model& model, const File* a, const File* b) {
    thunar::Node na{const_cast<File*>(a)};
    thunar::Node nb{const_cast<File*>(b)};
    return thunar::cmp_nodes(&na, &nb, const_cast<thunar::Model*>(&model));
}

Case<Item> thunar_case(Role role, bool descending) {
    auto model = std::make_shared<thunar::Model>();
    model->set_sort(role, descending);
    auto ascending = std::make_shared<thunar::Model>();
    ascending->set_sort(role, false);

    Case<Item> c;
    c.project = "Thunar";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [model](const Item& a, const Item& b) { return thunar_compare(*model, a, b); };
    c.resort = [model](const std::vector<Item>& previous, std::vector<Item>& out) {
        std::vector<thunar::Node> nodes;
        nodes.reserve(previous.size());
        GSequence* children = g_sequence_new(nullptr);
        for (Item f : previous) {
            nodes.push_back(thunar::Node{f});
            g_sequence_append(children, &nodes.back());
        }
        const auto t = Clock::now();
        thunar::sort_children(children, static_cast<gint>(previous.size()), model.get());
        const double elapsed = ms_since(t);
        out.clear();
        for (GSequenceIter* it = g_sequence_get_begin_iter(children); !g_sequence_iter_is_end(it);
             it = g_sequence_iter_next(it)) {
            out.push_back(static_cast<thunar::Node*>(g_sequence_get(it))->file);
        }
        g_sequence_free(children);
        return elapsed;
    };
    c.make_key = [role, ascending] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = [](const Item& f) { return bz::keys::from_bytes(f->collate_key_nocase, std::strlen(f->collate_key_nocase)); };
        } else if (role == kSize) {
            key = [](const Item& f) { return static_cast<std::uint64_t>(f->size); };
        } else {
            key = [](const Item& f) { return static_cast<std::uint64_t>(f->mtime); };  // compared as guint64
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [ascending](const Item& a, const Item& b) { return thunar_compare(*ascending, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// Nemo and Caja
// ---------------------------------------------------------------------------
int nemo_compare(const nemo::Model& model, const File* a, const File* b) {
    nemo::FileEntry ea{const_cast<File*>(a)};
    nemo::FileEntry eb{const_cast<File*>(b)};
    return nemo::file_entry_compare_func(&ea, &eb, const_cast<nemo::Model*>(&model));
}

/// Shared by Nemo, Caja and Nautilus: their names compare "sort last" names
/// (starting with '.' or '#') after all others, then by the collation key.
std::function<std::uint64_t(const Item&)> nautilus_family_key(Role role) {
    if (role == kName) {
        return [](const Item& f) {
            const char c = f->name.empty() ? '\0' : f->name[0];
            const bool sort_last = c == nemo::kSortLastChar1 || c == nemo::kSortLastChar2;
            return (sort_last ? kTop : 0) | (bz::keys::from_bytes(f->collate_key, std::strlen(f->collate_key)) >> 1);
        };
    }
    if (role == kSize) {
        return [](const Item& f) { return f->is_dir ? std::uint64_t{0} : static_cast<std::uint64_t>(f->size); };
    }
    // Time 0 is "unknowable" and sorts before every known time.
    return [](const Item& f) { return f->mtime == 0 ? 0 : kTop | (bz::keys::from_signed(f->mtime) >> 1); };
}

Case<Item> nemo_case(Role role, bool descending, bool caja) {
    auto model = std::make_shared<nemo::Model>();
    model->caja = caja;
    model->set_sort(role, descending);
    auto ascending = std::make_shared<nemo::Model>();
    ascending->caja = caja;
    ascending->set_sort(role, false);

    Case<Item> c;
    c.project = caja ? "Caja" : "Nemo";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [model](const Item& a, const Item& b) { return nemo_compare(*model, a, b); };
    c.resort = [model](const std::vector<Item>& previous, std::vector<Item>& out) {
        std::vector<nemo::FileEntry> entries;
        entries.reserve(previous.size());
        GSequence* files = g_sequence_new(nullptr);
        for (Item f : previous) {
            entries.push_back(nemo::FileEntry{f});
            entries.back().ptr = g_sequence_append(files, &entries.back());
        }
        const auto t = Clock::now();
        nemo::sort_file_entries(files, model.get());
        const double elapsed = ms_since(t);
        out.clear();
        for (GSequenceIter* it = g_sequence_get_begin_iter(files); !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it)) {
            out.push_back(static_cast<nemo::FileEntry*>(g_sequence_get(it))->file);
        }
        g_sequence_free(files);
        return elapsed;
    };
    c.make_key = [role, ascending] {
        return bz::SortKey<Item>::of_key(role_name(role), nautilus_family_key(role),
                                         [ascending](const Item& a, const Item& b) { return nemo_compare(*ascending, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// Nautilus on GTK 4
// ---------------------------------------------------------------------------
int nautilus_compare(Role role, bool descending, const File* a, const File* b) {
    const int dirs = nautilus::compare_directories(a, b, true);
    if (dirs != 0) {
        return dirs;
    }
    const int result = nautilus::compare_for_sort(a, b, role);
    return descending ? -result : result;
}

#ifdef SPEEDUP_HAVE_GTK4

/// Stands in for NautilusViewItem: a GObject the list store holds.
struct BenchViewItem {
    GObject parent_instance;
    File* file;
};
struct BenchViewItemClass {
    GObjectClass parent_class;
};
G_DEFINE_TYPE(BenchViewItem, bench_view_item, G_TYPE_OBJECT)
void bench_view_item_class_init(BenchViewItemClass*) {}
void bench_view_item_init(BenchViewItem*) {}

File* file_of(gconstpointer item) { return reinterpret_cast<const BenchViewItem*>(item)->file; }

/// What GtkColumnViewSorter does for the clicked column: the column's sorter
/// (nautilus_list_view_sort ()) in the chosen direction.
struct ColumnState {
    Role role = kName;
    bool descending = false;
};

int column_sort(gconstpointer a, gconstpointer b, gpointer user_data) {
    const ColumnState* state = static_cast<const ColumnState*>(user_data);
    const int result = nautilus::compare_for_sort(file_of(a), file_of(b), state->role);
    return state->descending ? -result : result;
}

int sort_directories_func(gconstpointer a, gconstpointer b, gpointer user_data) {
    const gboolean* directories_first = static_cast<const gboolean*>(user_data);
    return nautilus::compare_directories(file_of(a), file_of(b), *directories_first);
}

GListModel* no_children(gpointer, gpointer) { return nullptr; }

/// The model chain of nautilus-view-model.c with the sorters of nautilus-list-view.c.
class NautilusModel {
public:
    explicit NautilusModel(const std::vector<Item>& listing) {
        GListStore* store = g_list_store_new(bench_view_item_get_type());
        std::vector<gpointer> objects;
        objects.reserve(listing.size());
        for (Item f : listing) {
            auto* item = static_cast<BenchViewItem*>(g_object_new(bench_view_item_get_type(), nullptr));
            item->file = f;
            objects.push_back(item);
        }
        g_list_store_splice(store, 0, 0, objects.data(), static_cast<guint>(objects.size()));
        for (gpointer o : objects) {
            g_object_unref(o);
        }
        GtkFilterListModel* filter = gtk_filter_list_model_new(G_LIST_MODEL(store), nullptr);
        GtkTreeListModel* tree = gtk_tree_list_model_new(G_LIST_MODEL(filter), FALSE, FALSE, no_children, nullptr, nullptr);
        sort_model_ = gtk_sort_list_model_new(G_LIST_MODEL(tree), nullptr);

        column_sorter_ = gtk_custom_sorter_new(column_sort, &state_, nullptr);
        GtkMultiSorter* multi = gtk_multi_sorter_new();
        gtk_multi_sorter_append(multi, GTK_SORTER(gtk_custom_sorter_new(sort_directories_func, &directories_first_, nullptr)));
        gtk_multi_sorter_append(multi, GTK_SORTER(g_object_ref(column_sorter_)));
        GtkTreeListRowSorter* row_sorter = gtk_tree_list_row_sorter_new(GTK_SORTER(multi));
        gtk_sort_list_model_set_sorter(sort_model_, GTK_SORTER(row_sorter));
        g_object_unref(row_sorter);
    }
    ~NautilusModel() {
        g_object_unref(column_sorter_);
        g_object_unref(sort_model_);
    }
    NautilusModel(const NautilusModel&) = delete;
    NautilusModel& operator=(const NautilusModel&) = delete;

    /// A click on a column header: the column view sorter changes and the sort
    /// model re-sorts (synchronously; Nautilus does not make it incremental).
    double set_sort(Role role, bool descending) {
        state_.role = role;
        state_.descending = descending;
        const auto t = Clock::now();
        gtk_sorter_changed(GTK_SORTER(column_sorter_), GTK_SORTER_CHANGE_DIFFERENT);
        return ms_since(t);
    }

    std::vector<Item> order() const {
        const guint n = g_list_model_get_n_items(G_LIST_MODEL(sort_model_));
        std::vector<Item> out;
        out.reserve(n);
        for (guint i = 0; i < n; ++i) {
            auto* row = static_cast<GtkTreeListRow*>(g_list_model_get_item(G_LIST_MODEL(sort_model_), i));
            gpointer item = gtk_tree_list_row_get_item(row);
            out.push_back(file_of(item));
            g_object_unref(item);
            g_object_unref(row);
        }
        return out;
    }

private:
    GtkSortListModel* sort_model_ = nullptr;
    GtkCustomSorter* column_sorter_ = nullptr;
    ColumnState state_;
    gboolean directories_first_ = TRUE;
};

Case<Item> nautilus_case(Role role, bool descending, const std::shared_ptr<NautilusModel>& model) {
    Case<Item> c;
    c.project = "Nautilus";
    c.role = role_name(role);
    c.descending = descending;
    c.compare = [role, descending](const Item& a, const Item& b) { return nautilus_compare(role, descending, a, b); };
    c.resort = [model, role, descending](const std::vector<Item>&, std::vector<Item>& out) {
        model->set_sort(static_cast<Role>((role + 1) % 3), false);  // the previous column
        const double elapsed = model->set_sort(role, descending);
        out = model->order();
        return elapsed;
    };
    c.make_key = [role] {
        return bz::SortKey<Item>::of_key(role_name(role), nautilus_family_key(role),
                                         [role](const Item& a, const Item& b) { return nautilus_compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

#endif  // SPEEDUP_HAVE_GTK4

}  // namespace

int main(int argc, char** argv) {
    fm_bench::Options opt = fm_bench::parse_options(argc, argv);
    if (opt.datasets.empty() || !std::setlocale(LC_ALL, opt.locale.c_str())) {
        std::fprintf(stderr, "usage: %s %s\n(the locale must exist)\n", argv[0], fm_bench::kUsage);
        return 2;
    }
#ifdef SPEEDUP_HAVE_GTK4
    std::printf("GNOME family: GLib %u.%u, GTK %u.%u.%u, locale %s, page %zu\n", glib_major_version, glib_minor_version,
                gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(), opt.locale.c_str(), opt.page);
#else
    std::printf("GNOME family: GLib %u.%u (GTK 4 not found: no Nautilus), locale %s, page %zu\n", glib_major_version,
                glib_minor_version, opt.locale.c_str(), opt.page);
#endif
    std::vector<fm_bench::Row> rows;
    static const char kFolder[] = "/bench";

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
        const auto tk = Clock::now();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            File& f = storage[i];
            f.path = entries[i].path;
            f.name = entries[i].name;
            f.is_dir = entries[i].is_dir;
            f.is_hidden = entries[i].is_hidden;
            f.size = entries[i].size;
            f.mtime = entries[i].mtime_ns / 1000000000;
            f.directory = kFolder;
            f.directory_name = kFolder;
            compute_keys(f);  // the projects do this once per file, when it is loaded
            listing.push_back(&f);
        }
        std::printf("\n%s: %zu entries, collation keys %.1f ms\n", dataset.c_str(), listing.size(), ms_since(tk));

        auto run = [&](const Case<Item>& c, const Case<Item>& previous_case) {
            const std::vector<Item> previous = fm_bench::sorted_by(listing, previous_case.compare);
            rows.push_back(fm_bench::run_case(c, dataset, listing, previous, opt));
            fm_bench::print_row(rows.back());
        };
        for (int r = 0; r < 3; ++r) {
            const Role role = static_cast<Role>(r);
            const Role previous_role = static_cast<Role>((r + 1) % 3);
            for (int d = 0; d < 2; ++d) {
                if (opt.wants("Thunar")) run(thunar_case(role, d == 1), thunar_case(previous_role, false));
                if (opt.wants("Nemo")) run(nemo_case(role, d == 1, false), nemo_case(previous_role, false, false));
                if (opt.wants("Caja")) run(nemo_case(role, d == 1, true), nemo_case(previous_role, false, true));
            }
        }
#ifdef SPEEDUP_HAVE_GTK4
        if (opt.wants("Nautilus")) {
            auto model = std::make_shared<NautilusModel>(listing);
            for (int r = 0; r < 3; ++r) {
                const Role role = static_cast<Role>(r);
                const Role previous_role = static_cast<Role>((r + 1) % 3);
                for (int d = 0; d < 2; ++d) {
                    run(nautilus_case(role, d == 1, model), nautilus_case(previous_role, false, model));
                }
            }
        }
#endif
        for (File& f : storage) {
            free_keys(f);
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
