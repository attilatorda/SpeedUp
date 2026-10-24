// Bucketization against the Godot editor's FileSystem dock sort
// (godot_sort.h): on every sort change, FileSystemDock::_update_file_list()
// builds a List<FileInfo> of the folder's files in the editor's cached order
// (by name, FileNoCaseComparator) and calls sort_file_info_list(). The timed
// "re-sort" is that call alone; building the list and refilling the ItemList,
// which Godot also does on every change, are not included.
//
// Folders are listed separately by the dock (in name order), so only files are
// sorted here. File types come from the extension (images, scenes, scripts...),
// standing in for the resource types EditorFileSystem detects.
//
// Usage: speedup_benchmark_godot [options] DATASET.tsv...   (see fm_harness.h)

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../common/dataset.h"
#include "../common/fm_harness.h"
#include "godot_sort.h"

namespace bz = speedup::bucketization;
using fm_bench::Case;
using fm_bench::Clock;
using fm_bench::ms_since;
using godot::String;

namespace {

struct File {
    std::string path;
    String name;  // Godot String (UTF-32)
    const String* type = nullptr;
    uint64_t modified_time = 0;  // seconds, as EditorFileSystem stores it
};
using Item = File*;

String to_utf32(const std::string& s) {
    String out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t r = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            r = c;
        } else if ((c >> 5) == 6 && i + 1 < s.size()) {
            r = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
            len = 2;
        } else if ((c >> 4) == 14 && i + 2 < s.size()) {
            r = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            len = 3;
        } else if ((c >> 3) == 30 && i + 3 < s.size()) {
            r = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            len = 4;
        }
        out.push_back(r);
        i += len;
    }
    return out;
}

/// Resource types by extension: what EditorFileSystem would report for common files.
const String& type_for(const String& name) {
    static const std::map<std::string, String> types = {
        {"png", U"CompressedTexture2D"}, {"jpg", U"CompressedTexture2D"}, {"jpeg", U"CompressedTexture2D"},
        {"webp", U"CompressedTexture2D"}, {"svg", U"CompressedTexture2D"}, {"bmp", U"CompressedTexture2D"},
        {"tscn", U"PackedScene"}, {"scn", U"PackedScene"}, {"glb", U"PackedScene"}, {"gltf", U"PackedScene"},
        {"gd", U"GDScript"}, {"cs", U"CSharpScript"}, {"tres", U"Resource"}, {"res", U"Resource"},
        {"wav", U"AudioStreamWAV"}, {"ogg", U"AudioStreamOggVorbis"}, {"mp3", U"AudioStreamMP3"},
        {"ttf", U"FontFile"}, {"otf", U"FontFile"}, {"gdshader", U"Shader"}, {"json", U"JSON"},
        {"txt", U"TextFile"}, {"md", U"TextFile"}, {"cfg", U"TextFile"}, {"ini", U"TextFile"},
    };
    static const String other = U"OtherFile";
    std::string ext;
    for (char32_t c : godot::get_extension(name)) {
        ext.push_back(c < 0x80 ? static_cast<char>(std::tolower(static_cast<int>(c))) : '?');
    }
    const auto it = types.find(ext);
    return it != types.end() ? it->second : other;
}

enum Role { kName, kType, kModified };
const char* role_name(int r) { return r == kName ? "name" : r == kType ? "type" : "modified"; }

godot::FileSortOption option_for(Role role, bool reverse) {
    using O = godot::FileSortOption;
    switch (role) {
        case kName: return reverse ? O::FILE_SORT_NAME_REVERSE : O::FILE_SORT_NAME;
        case kType: return reverse ? O::FILE_SORT_TYPE_REVERSE : O::FILE_SORT_TYPE;
        case kModified: return reverse ? O::FILE_SORT_MODIFIED_TIME_REVERSE : O::FILE_SORT_MODIFIED_TIME;
    }
    return O::FILE_SORT_NAME;
}

int three(bool less_ab, bool less_ba) { return less_ab ? -1 : (less_ba ? 1 : 0); }

int name_cmp(const Item& a, const Item& b) {
    const signed char c = godot::filenocasecmp_to(a->name, b->name);
    return (c > 0) - (c < 0);
}

godot::FileInfo info_of(const File* f) {
    godot::FileInfo fi;
    fi.name = f->name;
    fi.type = f->type;
    fi.modified_time = f->modified_time;
    fi.source = f;
    return fi;
}

/// Godot's order for a column, ascending: the comparator, then the cached name
/// order the stable merge sort keeps for ties. Godot's "reverse" reverses it all.
int godot_compare(Role role, const Item& a, const Item& b) {
    int c = 0;
    if (role == kType) {
        const godot::FileInfo fa = info_of(a), fb = info_of(b);
        godot::FileInfoTypeComparator less;
        c = three(less(fa, fb), less(fb, fa));
    } else if (role == kModified) {
        c = three(a->modified_time > b->modified_time, b->modified_time > a->modified_time);  // newest first
    }
    return c != 0 ? c : name_cmp(a, b);
}

Case<Item> godot_case(Role role, bool reverse) {
    Case<Item> c;
    c.project = "Godot";
    c.role = role_name(role);
    c.descending = reverse;
    c.compare = [role, reverse](const Item& a, const Item& b) {
        const int r = godot_compare(role, a, b);
        return reverse ? -r : r;
    };
    c.resort = [role, reverse](const std::vector<Item>& cached_order, std::vector<Item>& out) {
        godot::FileList file_list;
        const String directory = U"res://assets";
        for (Item f : cached_order) {  // FileSystemDock::_update_file_list ()
            godot::FileInfo fi = info_of(f);
            fi.path = godot::path_join(directory, fi.name);
            file_list.push_back(std::move(fi));
        }
        const auto t = Clock::now();
        godot::sort_file_info_list(file_list, option_for(role, reverse));
        const double elapsed = ms_since(t);
        out.clear();
        for (auto* e = file_list.front(); e; e = e->next_ptr) {
            out.push_back(const_cast<File*>(static_cast<const File*>(e->value.source)));
        }
        return elapsed;
    };
    c.make_key = [role] {
        if (role == kModified) {
            return bz::SortKey<Item>::of_key(
                "modified", [](const Item& f) { return ~f->modified_time; },  // newest first
                [](const Item& a, const Item& b) { return name_cmp(a, b); });
        }
        // Godot's natural, case-folded comparison: comparator mode (no 64-bit key).
        return bz::SortKey<Item>::of_comparator(role_name(role),
                                                [role](const Item& a, const Item& b) { return godot_compare(role, a, b); });
    };
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    fm_bench::Options opt = fm_bench::parse_options(argc, argv);
    if (opt.datasets.empty() || !std::setlocale(LC_ALL, opt.locale.c_str())) {
        std::fprintf(stderr, "usage: %s %s\n(the locale must exist)\n", argv[0], fm_bench::kUsage);
        return 2;
    }
    std::printf("Godot FileSystem dock (c8256a8 sort path), locale %s, page %zu, case table %zu entries\n",
                opt.locale.c_str(), opt.page, godot::caps_table().size());
    std::vector<fm_bench::Row> rows;

    for (const std::string& path : opt.datasets) {
        const auto entries = speedup_bench::load_listing(path);
        const std::string dataset = speedup_bench::dataset_name(path);
        if (opt.max_entries != 0 && entries.size() > opt.max_entries) {
            std::printf("\n%s: %zu entries, skipped (--max-entries)\n", dataset.c_str(), entries.size());
            continue;
        }
        std::vector<File> storage;
        for (const auto& e : entries) {
            if (e.is_dir) {
                continue;  // folders are listed separately by the dock
            }
            File f;
            f.path = e.path;
            f.name = to_utf32(e.name);
            f.type = &type_for(f.name);
            f.modified_time = static_cast<uint64_t>(e.mtime_ns / 1000000000);
            storage.push_back(std::move(f));
        }
        // EditorFileSystem keeps a folder's files sorted by name (FileNoCaseComparator).
        std::vector<Item> cached_order;
        for (File& f : storage) {
            cached_order.push_back(&f);
        }
        std::stable_sort(cached_order.begin(), cached_order.end(), [](const Item& a, const Item& b) { return name_cmp(a, b) < 0; });
        if (cached_order.size() < 2) {
            std::printf("\n%s: %zu files, skipped (only folders)\n", dataset.c_str(), cached_order.size());
            continue;
        }
        std::printf("\n%s: %zu files\n", dataset.c_str(), cached_order.size());

        for (int r = 0; r < 3; ++r) {
            for (int d = 0; d < 2; ++d) {
                if (!opt.wants("Godot")) {
                    continue;
                }
                const Case<Item> c = godot_case(static_cast<Role>(r), d == 1);
                // Godot always starts from the cached name order, whatever the previous sort was.
                rows.push_back(fm_bench::run_case(c, dataset, cached_order, cached_order, opt));
                fm_bench::print_row(rows.back());
            }
        }
    }
    if (!opt.csv.empty() && !fm_bench::write_csv(opt.csv, rows)) {
        return 1;
    }
    const bool ok = fm_bench::all_valid(rows);
    std::printf("\n%s\n", ok ? "Every Bucketization order is sorted under Godot's comparison."
                             : "SOME BUCKETIZATION ORDERS ARE NOT SORTED UNDER GODOT'S COMPARISON");
    return ok ? 0 : 1;
}
