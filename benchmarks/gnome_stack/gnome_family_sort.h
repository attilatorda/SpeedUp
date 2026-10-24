#pragma once
// The sort paths of Thunar, Nemo, Caja and Nautilus, extracted for benchmarking.
//
// Each comparison below follows the project's source line by line, with the
// project's file object reduced to the fields the comparison reads. Snapshots:
//   Thunar   b1d2264  thunar/thunar-tree-view-model.c, thunar/thunar-file.c
//   Nemo     60120b8  src/nemo-list-model.c, libnemo-private/nemo-file.c
//   Caja     4de76eb  src/file-manager/fm-list-model.c, libcaja-private/caja-file.c
//   Nautilus 9f3a110  src/nautilus-list-view.c, src/nautilus-view-model.c, src/nautilus-file.c
// Those files are GPL-2.0-or-later (nautilus-view-model.c GPL-3.0-or-later);
// the extracts keep that license and are combined here under AGPL-3.0.
//
// Settings are each project's defaults: folders first, case-insensitive names
// (Thunar), "smart" filename collation keys, hidden files not last, no
// favorites, pins or filters. All files are in one folder, as in a real view;
// the flattened-tree datasets therefore contain equal names, which every
// project here treats as ties.

#include <glib.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gnome_family {

enum Role { kName, kSize, kModified };
inline const char* role_name(int role) { return role == kName ? "name" : role == kSize ? "size" : "modified"; }

/// What the four projects' file objects keep and the comparisons read.
struct File {
    std::string path;  // unique within the dataset
    std::string name;  // display name
    bool is_dir = false;
    bool is_hidden = false;
    guint64 size = 0;
    gint64 mtime = 0;  // seconds
    char* collate_key = nullptr;         // g_utf8_collate_key_for_filename (display name)
    char* collate_key_nocase = nullptr;  // Thunar: key of the casefolded name, or collate_key itself
    const char* directory_name = "";     // Nautilus: nautilus_file_get_directory_name (), one per folder
    const void* directory = nullptr;     // Nemo/Caja: file->details->directory, one per folder
    int sort_order = 0;                  // file->details->sort_order (Nemo/Caja/Nautilus), 0 normally
    bool favorite = false;               // Nemo
    bool pinned = false;                 // Nemo
};
using Item = File*;

inline void compute_keys(File& f) {
    f.collate_key = g_utf8_collate_key_for_filename(f.name.c_str(), -1);
    // thunar_file_info_reload (): only a separate key when the casefolded name differs.
    gchar* casefold = g_utf8_casefold(f.name.c_str(), -1);
    if (casefold != nullptr && std::strcmp(casefold, f.name.c_str()) != 0) {
        f.collate_key_nocase = g_utf8_collate_key_for_filename(casefold, -1);
    } else {
        f.collate_key_nocase = f.collate_key;
    }
    g_free(casefold);
}

inline void free_keys(File& f) {
    if (f.collate_key_nocase != f.collate_key) {
        g_free(f.collate_key_nocase);
    }
    g_free(f.collate_key);
}

// ---------------------------------------------------------------------------
// Thunar (thunar-file.c, thunar-tree-view-model.c)
// ---------------------------------------------------------------------------
namespace thunar {

inline gint compare_by_name(const File* a, const File* b, gboolean case_sensitive) {
    gint result = 0;
    if (G_LIKELY(!case_sensitive)) {
        result = g_strcmp0(a->collate_key_nocase, b->collate_key_nocase);
    }
    if (result == 0) {
        result = g_strcmp0(a->collate_key, b->collate_key);
    }
    // thunar_file_get_original_path () is NULL outside the trash.
    if (result == 0) {
        result = g_strcmp0(nullptr, nullptr);
    }
    return result;
}

inline gint cmp_by_size(const File* a, const File* b, gboolean case_sensitive) {
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;
    return compare_by_name(a, b, case_sensitive);
}

inline gint cmp_by_date_modified(const File* a, const File* b, gboolean case_sensitive) {
    const guint64 date_a = static_cast<guint64>(a->mtime);
    const guint64 date_b = static_cast<guint64>(b->mtime);
    if (date_a < date_b) return -1;
    if (date_a > date_b) return 1;
    return compare_by_name(a, b, case_sensitive);
}

using SortFunc = gint (*)(const File*, const File*, gboolean);

struct Model {
    SortFunc sort_func = compare_by_name;
    gint sort_sign = 1;
    gboolean sort_case_sensitive = FALSE;
    gboolean sort_folders_first = TRUE;
    gboolean sort_hidden_last = FALSE;

    void set_sort(Role role, bool descending) {
        sort_func = role == kName ? compare_by_name : role == kSize ? cmp_by_size : cmp_by_date_modified;
        sort_sign = descending ? -1 : 1;
    }
};

/// Thunar's Node: the GSequence holds these, the comparison reads ->file.
struct Node {
    File* file;
    void* ptr = nullptr;
    GSequence* children = nullptr;
    gint n_children = 0;
};

inline gint cmp_nodes(gconstpointer pa, gconstpointer pb, gpointer data) {
    const Model* model = static_cast<const Model*>(data);
    const File* a = static_cast<const Node*>(pa)->file;
    const File* b = static_cast<const Node*>(pb)->file;
    if (G_LIKELY(model->sort_folders_first)) {
        if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    }
    if (model->sort_hidden_last) {
        if (a->is_hidden != b->is_hidden) return a->is_hidden ? 1 : -1;
    }
    return (*model->sort_func)(a, b, model->sort_case_sensitive) * model->sort_sign;
}

/// _thunar_tree_view_model_sort () for the folder node, without the view's
/// handling of rows-reordered: store the old order, g_sequence_sort (), then
/// new_order[newpos] = oldpos.
inline void sort_children(GSequence* children, gint length, Model* model) {
    GSequenceIter** old_order = g_new(GSequenceIter*, length);
    gint* new_order = g_new(gint, length);
    GSequenceIter* row = g_sequence_get_begin_iter(children);
    for (gint n = 0; n < length; ++n) {
        old_order[n] = row;
        row = g_sequence_iter_next(row);
    }
    g_sequence_sort(children, cmp_nodes, model);
    for (gint n = 0; n < length; ++n) {
        new_order[g_sequence_iter_get_position(old_order[n])] = n;
    }
    g_free(old_order);
    g_free(new_order);
}

}  // namespace thunar

// ---------------------------------------------------------------------------
// Nemo and Caja (nemo-file.c / caja-file.c, nemo-list-model.c / fm-list-model.c)
// Caja is the same code without favorites and pins.
// ---------------------------------------------------------------------------
namespace nemo {

constexpr char kSortLastChar1 = '.';
constexpr char kSortLastChar2 = '#';
enum Knowledge { KNOWN, UNKNOWABLE, UNKNOWN };
enum SortType { BY_DISPLAY_NAME, BY_SIZE, BY_TYPE, BY_DETAILED_TYPE, BY_EXTENSION, BY_MTIME };

inline Knowledge get_size(const File* f, goffset* size) {
    *size = static_cast<goffset>(f->size);
    return KNOWN;
}

inline Knowledge get_time(const File* f, time_t* out) {
    *out = static_cast<time_t>(f->mtime);
    return *out == 0 ? UNKNOWABLE : KNOWN;
}

/// Folder item counts load asynchronously; until then they are unknown.
inline Knowledge get_item_count(const File*, guint* count) {
    *count = 0;
    return UNKNOWN;
}

inline int compare_directories_by_count(const File* a, const File* b) {
    guint count_1 = 0, count_2 = 0;
    const Knowledge known_1 = get_item_count(a, &count_1);
    const Knowledge known_2 = get_item_count(b, &count_2);
    if (known_1 > known_2) return -1;
    if (known_1 < known_2) return +1;
    if (known_1 == UNKNOWABLE || known_1 == UNKNOWN) return 0;
    if (count_1 < count_2) return -1;
    if (count_1 > count_2) return +1;
    return 0;
}

inline int compare_files_by_size(const File* a, const File* b) {
    goffset size_1 = 0, size_2 = 0;
    const Knowledge known_1 = get_size(a, &size_1);
    const Knowledge known_2 = get_size(b, &size_2);
    if (known_1 > known_2) return -1;
    if (known_1 < known_2) return +1;
    if (known_1 == UNKNOWABLE || known_1 == UNKNOWN) return 0;
    if (size_1 < size_2) return -1;
    if (size_1 > size_2) return +1;
    return 0;
}

inline int compare_by_size(const File* a, const File* b) {
    if (a->is_dir && !b->is_dir) return -1;
    if (b->is_dir && !a->is_dir) return +1;
    return a->is_dir ? compare_directories_by_count(a, b) : compare_files_by_size(a, b);
}

inline int compare_by_display_name(const File* a, const File* b) {
    const char* name_1 = a->name.c_str();
    const char* name_2 = b->name.c_str();
    const bool sort_last_1 = name_1[0] == kSortLastChar1 || name_1[0] == kSortLastChar2;
    const bool sort_last_2 = name_2[0] == kSortLastChar1 || name_2[0] == kSortLastChar2;
    if (sort_last_1 && !sort_last_2) return +1;
    if (!sort_last_1 && sort_last_2) return -1;
    return g_strcmp0(a->collate_key, b->collate_key);
}

/// Same folder for every file of a view; different folders compare their parent URIs.
inline int compare_by_directory_name(const File* a, const File* b) {
    if (a->directory == b->directory) return 0;
    return g_utf8_collate(a->directory_name, b->directory_name);
}

inline int compare_by_time(const File* a, const File* b) {
    time_t time_1 = 0, time_2 = 0;
    const Knowledge known_1 = get_time(a, &time_1);
    const Knowledge known_2 = get_time(b, &time_2);
    if (known_1 > known_2) return -1;
    if (known_1 < known_2) return +1;
    if (known_1 == UNKNOWABLE || known_1 == UNKNOWN) return 0;
    if (time_1 < time_2) return -1;
    if (time_1 > time_2) return +1;
    return 0;
}

inline int compare_by_full_path(const File* a, const File* b) {
    const int compare = compare_by_directory_name(a, b);
    return compare != 0 ? compare : compare_by_display_name(a, b);
}

inline int compare_for_sort_internal(const File* a, const File* b, bool directories_first, bool favorites_first,
                                     bool reversed, bool caja) {
    if (!caja) {
        if (favorites_first) {
            if (a->favorite && !b->favorite) return -1;
            if (b->favorite && !a->favorite) return +1;
        }
        if (a->pinned && !b->pinned) return -1;
        if (b->pinned && !a->pinned) return +1;
    }
    if (directories_first) {
        if (a->is_dir && !b->is_dir) return -1;
        if (b->is_dir && !a->is_dir) return +1;
    }
    if (a->sort_order < b->sort_order) return reversed ? 1 : -1;
    if (a->sort_order > b->sort_order) return reversed ? -1 : 1;
    return 0;
}

inline int compare_for_sort(const File* a, const File* b, SortType sort_type, bool directories_first,
                            bool favorites_first, bool reversed, bool caja) {
    if (a == b) return 0;
    int result = compare_for_sort_internal(a, b, directories_first, favorites_first, reversed, caja);
    if (result == 0) {
        switch (sort_type) {
            case BY_DISPLAY_NAME:
                result = compare_by_display_name(a, b);
                if (result == 0) result = compare_by_directory_name(a, b);
                break;
            case BY_SIZE:
                result = compare_by_size(a, b);
                if (result == 0) result = compare_by_full_path(a, b);
                break;
            case BY_MTIME:
                result = compare_by_time(a, b);
                if (result == 0) result = compare_by_full_path(a, b);
                break;
            default:
                g_return_val_if_reached(0);
        }
        if (reversed) result = -result;
    }
    return result;
}

/// The attribute quarks the list model sorts by, checked in the same order as
/// nemo_file_compare_for_sort_by_attribute_q ().
struct Quarks {
    GQuark name = g_quark_from_static_string("name");
    GQuark size = g_quark_from_static_string("size");
    GQuark type = g_quark_from_static_string("type");
    GQuark detailed_type = g_quark_from_static_string("detailed_type");
    GQuark extension = g_quark_from_static_string("extension");
    GQuark modification_date = g_quark_from_static_string("modification_date");
    GQuark date_modified = g_quark_from_static_string("date_modified");
};

inline int compare_for_sort_by_attribute_q(const File* a, const File* b, GQuark attribute, const Quarks& q,
                                           bool directories_first, bool favorites_first, bool reversed, bool caja) {
    if (a == b) return 0;
    if (attribute == 0 || attribute == q.name) {
        return compare_for_sort(a, b, BY_DISPLAY_NAME, directories_first, favorites_first, reversed, caja);
    } else if (attribute == q.size) {
        return compare_for_sort(a, b, BY_SIZE, directories_first, favorites_first, reversed, caja);
    } else if (attribute == q.type || attribute == q.detailed_type || attribute == q.extension) {
        g_return_val_if_reached(0);  // not benchmarked
    } else if (attribute == q.modification_date || attribute == q.date_modified) {
        return compare_for_sort(a, b, BY_MTIME, directories_first, favorites_first, reversed, caja);
    }
    g_return_val_if_reached(0);
}

struct Model {
    Quarks quarks;
    GQuark sort_attribute = 0;
    bool sort_directories_first = true;
    bool sort_favorites_first = true;
    bool descending = false;
    bool filter_active = false;
    bool caja = false;

    void set_sort(Role role, bool desc) {
        sort_attribute = role == kName ? quarks.name : role == kSize ? quarks.size : quarks.date_modified;
        descending = desc;
    }
};

/// The list model's FileEntry: the GSequence holds these.
struct FileEntry {
    File* file;
    GHashTable* reverse_map = nullptr;
    GSequenceIter* ptr = nullptr;
    GSequence* files = nullptr;
    FileEntry* parent = nullptr;
};

inline int file_entry_compare_func(gconstpointer pa, gconstpointer pb, gpointer user_data) {
    const Model* model = static_cast<const Model*>(user_data);
    const FileEntry* e1 = static_cast<const FileEntry*>(pa);
    const FileEntry* e2 = static_cast<const FileEntry*>(pb);
    if (e1->file != nullptr && e2->file != nullptr) {
        return compare_for_sort_by_attribute_q(e1->file, e2->file, model->sort_attribute, model->quarks,
                                               model->sort_directories_first, model->sort_favorites_first,
                                               model->descending, model->caja);
    }
    return e1->file == nullptr ? -1 : 1;
}

/// nemo_list_model_sort_file_entries () / fm_list_model_sort_file_entries ()
/// for the folder (no expanded subfolders), without the view's handling of
/// rows-reordered.
inline void sort_file_entries(GSequence* files, Model* model) {
    const int length = g_sequence_get_length(files);
    if (length <= 1) return;
    GSequenceIter** old_order = g_new(GSequenceIter*, length);
    for (int i = 0; i < length; ++i) {
        GSequenceIter* ptr = g_sequence_get_iter_at_pos(files, i);
        const FileEntry* entry = static_cast<const FileEntry*>(g_sequence_get(ptr));
        if (entry->files != nullptr) {
            sort_file_entries(entry->files, model);
        }
        old_order[i] = ptr;
    }
    g_sequence_sort(files, file_entry_compare_func, model);
    int* new_order = g_new(int, length);
    for (int i = 0; i < length; ++i) {
        new_order[g_sequence_iter_get_position(old_order[i])] = i;
    }
    g_free(old_order);
    g_free(new_order);
}

}  // namespace nemo

// ---------------------------------------------------------------------------
// Nautilus (nautilus-file.c, nautilus-list-view.c). The sort itself is GTK 4's
// GtkSortListModel; see benchmark_gnome_family.cpp.
// ---------------------------------------------------------------------------
namespace nautilus {

/// nautilus_file_compare_for_sort () with directories_first and reversed FALSE,
/// as nautilus_list_view_sort () calls it: GTK's column view sorter applies the
/// direction and a separate sorter puts folders first.
inline int compare_for_sort(const File* a, const File* b, Role role) {
    if (a == b) return 0;
    int result = nemo::compare_for_sort_internal(a, b, false, false, false, true);
    if (result != 0) return result;
    switch (role) {
        case kName:
            result = nemo::compare_by_display_name(a, b);
            if (result == 0) result = std::strcmp(a->directory_name, b->directory_name);
            // Some GVfs backends allow equal display names: compare_by_name ().
            if (result == 0) result = g_strcmp0(a->name.c_str(), b->name.c_str());
            break;
        case kSize:
            result = nemo::compare_by_size(a, b);
            if (result == 0) result = std::strcmp(a->directory_name, b->directory_name);
            if (result == 0) result = nemo::compare_by_display_name(a, b);
            break;
        case kModified:
            result = nemo::compare_by_time(a, b);
            if (result == 0) result = std::strcmp(a->directory_name, b->directory_name);
            if (result == 0) result = nemo::compare_by_display_name(a, b);
            break;
    }
    return result;
}

/// sort_directories_func ().
inline int compare_directories(const File* a, const File* b, bool directories_first) {
    if (directories_first) {
        if (a->is_dir && !b->is_dir) return -1;
        if (b->is_dir && !a->is_dir) return 1;
    }
    return 0;
}

}  // namespace nautilus

}  // namespace gnome_family
