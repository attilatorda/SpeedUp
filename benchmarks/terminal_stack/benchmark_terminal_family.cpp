// Bucketization against terminal file managers' sort paths:
// - nnn: qsort () of its entry array with entrycmp () / reventrycmp ()
// - yazi and lf: C++ ports of their comparisons (terminal_family_sort.h). Their
//   native sorts are timed by yazi_bench (Rust) and lf_bench (Go); the "resort"
//   time here is the C++ port's, for comparing languages on the same work.
//
// Usage: speedup_benchmark_terminal_family [options] DATASET.tsv...   (see fm_harness.h)

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../common/dataset.h"
#include "../common/fm_harness.h"
#include "terminal_family_sort.h"

namespace bz = speedup::bucketization;
using namespace terminal_family;
using fm_bench::Case;
using fm_bench::Clock;
using fm_bench::ms_since;

namespace {

constexpr std::uint64_t kTop = std::uint64_t{1} << 63;

std::size_t folders_first(const Item& f) { return f->is_dir ? 0 : 1; }

const std::string& path_of(const Item& f) { return f->path; }

// ---------------------------------------------------------------------------
// nnn
// ---------------------------------------------------------------------------
struct NnnStack {
    std::vector<nnn::entry> entries;  // one per file, by row
    std::unordered_map<const char*, File*> by_name;

    explicit NnnStack(const std::vector<Item>& listing) {
        for (Item f : listing) {
            entries.push_back(nnn::make_entry(*f));
            by_name[entries.back().name] = f;
        }
    }
    int compare(Role role, bool reverse, const Item& a, const Item& b) const {
        nnn::set_order(role, reverse);
        const nnn::entry* ea = &entries[static_cast<std::size_t>(a->row)];
        const nnn::entry* eb = &entries[static_cast<std::size_t>(b->row)];
        return reverse ? nnn::reventrycmp(ea, eb) : nnn::entrycmp(ea, eb);
    }
};

/// xstricmp (): names starting with a number first, by value; the others by
/// strcoll (), which orders like strcmp () on strxfrm () output.
std::uint64_t nnn_name_key(const Item& f) {
    const char* s = f->name.c_str();
    char* p = nullptr;
    const long long v = std::strtoll(s, &p, 10);
    if (p != s) {
        return bz::keys::from_signed(v) >> 1;
    }
    const std::size_t len = std::strxfrm(nullptr, s, 0);
    std::string buffer(len + 1, '\0');
    std::strxfrm(buffer.data(), s, len + 1);
    return kTop | (bz::keys::from_bytes(buffer.data(), std::min<std::size_t>(len, 8)) >> 1);
}

Case<Item> nnn_case(Role role, bool reverse, const std::shared_ptr<NnnStack>& stack) {
    Case<Item> c;
    c.project = "nnn";
    c.role = role_name(role);
    c.descending = reverse;
    c.path = path_of;
    c.compare = [stack, role, reverse](const Item& a, const Item& b) { return stack->compare(role, reverse, a, b); };
    c.resort = [stack, role, reverse](const std::vector<Item>& previous, std::vector<Item>& out) {
        std::vector<nnn::entry> dents;
        dents.reserve(previous.size());
        for (Item f : previous) {
            dents.push_back(stack->entries[static_cast<std::size_t>(f->row)]);
        }
        nnn::set_order(role, reverse);
        const auto t = Clock::now();
        // ENTSORT (pdents, ndents, entrycmpfn): glibc qsort ()
        std::qsort(dents.data(), dents.size(), sizeof(nnn::entry), reverse ? nnn::reventrycmp : nnn::entrycmp);
        const double elapsed = ms_since(t);
        out.clear();
        for (const nnn::entry& e : dents) {
            out.push_back(stack->by_name.at(e.name));
        }
        return elapsed;
    };
    c.make_key = [stack, role] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = nnn_name_key;
        } else if (role == kSize) {
            key = [](const Item& f) { return ~f->size; };  // largest first
        } else {
            key = [](const Item& f) { return ~bz::keys::from_signed(f->mtime_ns); };  // newest first
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [stack, role](const Item& a, const Item& b) { return stack->compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// yazi (C++ port)
// ---------------------------------------------------------------------------
struct YaziStack {
    std::vector<yazi::YFile> files;  // by row
    explicit YaziStack(const std::vector<Item>& listing) {
        for (Item f : listing) {
            files.push_back(yazi::make_file(*f));
        }
    }
    int compare(Role role, bool reverse, const Item& a, const Item& b) const {
        return yazi::sorter(role, reverse).compare(files[static_cast<std::size_t>(a->row)], files[static_cast<std::size_t>(b->row)]);
    }
};

/// cmp_insensitive (): ASCII-lowercased bytes, then length.
std::uint64_t yazi_name_key(const Item& f) {
    char bytes[8] = {};
    const std::size_t n = std::min<std::size_t>(f->name.size(), 8);
    for (std::size_t i = 0; i < n; ++i) {
        const char c = f->name[i];
        bytes[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
    }
    return bz::keys::from_bytes(bytes, n);
}

Case<Item> yazi_case(Role role, bool reverse, const std::shared_ptr<YaziStack>& stack) {
    Case<Item> c;
    c.project = "yazi (C++ port)";
    c.role = role_name(role);
    c.descending = reverse;
    c.path = path_of;
    c.compare = [stack, role, reverse](const Item& a, const Item& b) { return stack->compare(role, reverse, a, b); };
    c.resort = [stack, role, reverse](const std::vector<Item>& previous, std::vector<Item>& out) {
        std::vector<yazi::YFile> items;
        items.reserve(previous.size());
        for (Item f : previous) {
            items.push_back(stack->files[static_cast<std::size_t>(f->row)]);
        }
        const yazi::FilesSorter sorter = yazi::sorter(role, reverse);
        const auto t = Clock::now();
        sorter.sort(items);
        const double elapsed = ms_since(t);
        out.clear();
        for (const yazi::YFile& y : items) {
            out.push_back(y.file);
        }
        return elapsed;
    };
    c.make_key = [stack, role] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = yazi_name_key;
        } else if (role == kSize) {
            key = [](const Item& f) { return f->size; };
        } else {
            key = [](const Item& f) { return bz::keys::from_signed(f->mtime_ns); };
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [stack, role](const Item& a, const Item& b) { return stack->compare(role, false, a, b); })
            .with_groups(2, folders_first);
    };
    return c;
}

// ---------------------------------------------------------------------------
// lf (C++ port)
// ---------------------------------------------------------------------------

/// naturalCmp () compares the first tokens (runs of digits or of other bytes)
/// first: a digit run against another digit run by value, anything else by
/// bytes. So: other runs starting below '0', then digit runs by value, then
/// other runs starting above '9'. The key holds that class and the first
/// token's value or first 7 bytes.
std::uint64_t lf_name_key(const Item& f) {
    const std::string s = lf::normalize(f->name);
    if (s.empty()) {
        return 0;
    }
    const bool digit = lf::is_digit(s[0]);
    std::size_t hi = 0;
    while (hi < s.size() && lf::is_digit(s[hi]) == digit) {
        ++hi;
    }
    const std::string_view token(s.data(), hi);
    constexpr std::uint64_t kMaxValue = (std::uint64_t{1} << 62) - 1;
    if (digit) {
        long long v = 0;
        const std::uint64_t value = lf::atoi(token, v) ? std::min<std::uint64_t>(static_cast<std::uint64_t>(v), kMaxValue) : kMaxValue;
        return (std::uint64_t{1} << 62) | value;
    }
    const std::uint64_t cls = static_cast<unsigned char>(token[0]) < '0' ? 0 : 2;
    std::uint64_t prefix = 0;
    for (std::size_t i = 0; i < 7; ++i) {
        prefix = (prefix << 8) | (i < token.size() ? static_cast<unsigned char>(token[i]) : 0);
    }
    return (cls << 62) | prefix;
}

Case<Item> lf_case(Role role, bool reverse) {
    Case<Item> c;
    c.project = "lf (C++ port)";
    c.role = role_name(role);
    c.descending = reverse;
    c.path = path_of;
    c.compare = [role, reverse](const Item& a, const Item& b) { return lf::compare(role, reverse, a, b); };
    c.resort = [role, reverse](const std::vector<Item>& previous, std::vector<Item>& out) {
        out = previous;
        const auto t = Clock::now();
        lf::dir_sort(out, role, reverse);
        return ms_since(t);
    };
    c.make_key = [role] {
        std::function<std::uint64_t(const Item&)> key;
        if (role == kName) {
            key = lf_name_key;
        } else if (role == kSize) {
            key = [](const Item& f) { return bz::keys::from_signed(static_cast<std::int64_t>(f->size)); };
        } else {
            key = [](const Item& f) { return bz::keys::from_signed(f->mtime_ns); };
        }
        return bz::SortKey<Item>::of_key(role_name(role), key,
                                         [role](const Item& a, const Item& b) { return lf::compare(role, false, a, b); })
            .with_groups(2, folders_first);
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
    std::printf("Terminal family: nnn (glibc qsort), yazi and lf ports; locale %s, page %zu\n", opt.locale.c_str(), opt.page);
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
            f.name = entries[i].name;
            f.is_dir = entries[i].is_dir;
            f.is_hidden = entries[i].is_hidden;
            f.size = entries[i].size;
            f.mtime_ns = entries[i].mtime_ns;
            listing.push_back(&f);
        }
        std::printf("\n%s: %zu entries\n", dataset.c_str(), listing.size());
        auto nnn_stack = std::make_shared<NnnStack>(listing);
        auto yazi_stack = std::make_shared<YaziStack>(listing);

        auto run = [&](const Case<Item>& c, const Case<Item>& previous_case) {
            const std::vector<Item> previous = fm_bench::sorted_by(listing, previous_case.compare);
            rows.push_back(fm_bench::run_case(c, dataset, listing, previous, opt));
            fm_bench::print_row(rows.back());
        };
        for (int r = 0; r < 3; ++r) {
            const Role role = static_cast<Role>(r);
            const Role previous_role = static_cast<Role>((r + 1) % 3);
            for (int d = 0; d < 2; ++d) {
                if (opt.wants("nnn")) run(nnn_case(role, d == 1, nnn_stack), nnn_case(previous_role, false, nnn_stack));
                if (opt.wants("yazi")) run(yazi_case(role, d == 1, yazi_stack), yazi_case(previous_role, false, yazi_stack));
                if (opt.wants("lf")) run(lf_case(role, d == 1), lf_case(previous_role, false));
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
