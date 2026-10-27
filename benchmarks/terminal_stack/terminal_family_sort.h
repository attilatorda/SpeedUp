#pragma once
// The sort paths of nnn (C), and C++ ports of yazi's (Rust) and lf's (Go)
// comparisons, for benchmarking. The native Rust and Go programs next to this
// file (yazi_bench, lf_bench) time yazi's and lf's real sorts; the ports give
// Bucketization the same comparisons, and their own sort times show how C++
// compares with the native code on the same work. Snapshots and licenses:
//   nnn  d1ee163  src/nnn.c (entrycmp, reventrycmp, xstricmp)        BSD-2-Clause
//   yazi 40a9142  yazi-fs/src/sorter.rs                               MIT
//   lf   664d7c8  nav.go (dir.sort), misc.go (naturalCmp), diacritics.go  MIT
// Each project's defaults: nnn sorts names with xstricmp (numbers first, then
// strcoll), folders first; yazi alphabetical, case-insensitive, folders first,
// alphabetical fallback; lf natural, ignoring case and diacritics, folders
// first. Hidden files are shown.

#include <sys/types.h>

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace terminal_family {

/// One file of a dataset.
struct File {
    int row = 0;  // position in the listing
    std::string path;
    std::string name;
    bool is_dir = false;
    bool is_hidden = false;
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
};
using Item = File*;

enum Role { kName, kSize, kModified };
inline const char* role_name(int role) { return role == kName ? "name" : role == kSize ? "size" : "modified"; }

// ---------------------------------------------------------------------------
// nnn (src/nnn.c)
// ---------------------------------------------------------------------------
namespace nnn {

#define DIR_OR_DIRLNK 0x01
#define IS_DIR_OR_DIRLNK(ent) (((ent)->flags & DIR_OR_DIRLNK) != 0)

typedef unsigned int uint_t;
typedef unsigned long long ullong_t;

typedef struct entry {
    char* name;  /* 8 bytes */
    time_t sec;  /* 8 bytes */
    uint_t nsec; /* 4 bytes (enough to store nanosec) */
    mode_t mode; /* 4 bytes */
    off_t size;  /* 8 bytes */
    struct {
        ullong_t blocks : 40; /* 5 bytes (enough for 512 TiB in 512B blocks allocated) */
        ullong_t nlen : 16;   /* 2 bytes (length of file name) */
        ullong_t flags : 8;   /* 1 byte (flags specific to the file) */
    };
    uid_t uid; /* 4 bytes */
    gid_t gid; /* 4 bytes */
} *pEntry;

/// The settings entrycmp () reads (nnn's global cfg).
struct Config {
    unsigned timeorder : 1;
    unsigned sizeorder : 1;
    unsigned blkorder : 1;
    unsigned extnorder : 1;
    unsigned reverse : 1;
};
inline Config cfg{};

static int xstricmp(const char* const s1, const char* const s2) {
    char *p1, *p2;

    long long v1 = strtoll(s1, &p1, 10);
    long long v2 = strtoll(s2, &p2, 10);

    /* Check if at least 1 string is numeric */
    if (s1 != p1 || s2 != p2) {
        /* Handle both pure numeric */
        if (s1 != p1 && s2 != p2) {
            if (v2 > v1)
                return -1;

            if (v1 > v2)
                return 1;
        }

        /* Only first string non-numeric */
        if (s1 == p1)
            return 1;

        /* Only second string non-numeric */
        if (s2 == p2)
            return -1;
    }

    /* Handle 1. all non-numeric and 2. both same numeric value cases */
    return strcoll(s1, s2);
}

static int (*namecmpfn)(const char* const s1, const char* const s2) = &xstricmp;

static int entrycmp(const void* va, const void* vb) {
    const struct entry* pa = (pEntry)va;
    const struct entry* pb = (pEntry)vb;

    if (IS_DIR_OR_DIRLNK(pb) != IS_DIR_OR_DIRLNK(pa))
        return IS_DIR_OR_DIRLNK(pb) ? 1 : -1;

    /* Sort based on specified order */
    if (cfg.timeorder) {
        if (pb->sec > pa->sec)
            return 1;
        if (pb->sec < pa->sec)
            return -1;
        /* If sec matches, comare nsec */
        if (pb->nsec > pa->nsec)
            return 1;
        if (pb->nsec < pa->nsec)
            return -1;
    } else if (cfg.sizeorder) {
        if (pb->size > pa->size)
            return 1;
        if (pb->size < pa->size)
            return -1;
    }

    return namecmpfn(pa->name, pb->name);
}

static int reventrycmp(const void* va, const void* vb) {
    if (IS_DIR_OR_DIRLNK((pEntry)vb) != IS_DIR_OR_DIRLNK((pEntry)va))
        return IS_DIR_OR_DIRLNK((pEntry)vb) ? 1 : -1;

    return -entrycmp(va, vb);
}

inline void set_order(Role role, bool reverse) {
    cfg.timeorder = role == kModified;
    cfg.sizeorder = role == kSize;
    cfg.reverse = reverse;
}

inline entry make_entry(File& f) {
    entry e{};
    e.name = f.name.data();
    e.sec = static_cast<time_t>(f.mtime_ns / 1000000000);
    e.nsec = static_cast<uint_t>(f.mtime_ns % 1000000000);
    e.mode = f.is_dir ? 040755 : 0100644;
    e.size = static_cast<off_t>(f.size);
    e.blocks = (f.size + 511) / 512;
    e.nlen = f.name.size() + 1;
    e.flags = f.is_dir ? DIR_OR_DIRLNK : 0;
    return e;
}

#undef DIR_OR_DIRLNK
#undef IS_DIR_OR_DIRLNK

}  // namespace nnn

// ---------------------------------------------------------------------------
// yazi (yazi-fs/src/sorter.rs), ported
// ---------------------------------------------------------------------------
namespace yazi {

/// Like the native benchmark's File (yazi's File reduced): 152 bytes in Rust.
struct YFile {
    std::string url;
    std::size_t urn_start = 0;
    std::uint8_t kind = 0;  // 1: directory
    std::uint16_t mode = 0644;
    std::uint64_t len = 0;
    std::optional<std::int64_t> atime, btime, ctime, mtime;
    std::uint64_t dev = 2049;
    std::uint32_t uid = 1000, gid = 1000;
    std::uint64_t nlink = 1;
    File* file = nullptr;  // back pointer for the benchmark

    bool is_dir() const { return kind == 1; }
    std::string_view urn() const { return std::string_view(url).substr(urn_start); }
};

enum class SortBy { Mtime, Alphabetical, Size };

template <class T>
int cmp_values(const T& a, const T& b) {
    return a < b ? -1 : (b < a ? 1 : 0);
}

inline int cmp_bytes(std::string_view a, std::string_view b) {
    const int c = a.compare(b);
    return (c > 0) - (c < 0);
}

struct FilesSorter {
    SortBy by = SortBy::Alphabetical;
    bool sensitive = false;
    bool reverse = false;
    bool dir_first = true;

    int cmp_int(std::int64_t a, std::int64_t b) const { return reverse ? cmp_values(b, a) : cmp_values(a, b); }
    int cmp_u64(std::uint64_t a, std::uint64_t b) const { return reverse ? cmp_values(b, a) : cmp_values(a, b); }
    int cmp_str(std::string_view a, std::string_view b) const { return reverse ? cmp_bytes(b, a) : cmp_bytes(a, b); }
    /// Option<T>::cmp: None before Some.
    int cmp_opt(const std::optional<std::int64_t>& a, const std::optional<std::int64_t>& b) const {
        int c = a.has_value() != b.has_value() ? (a.has_value() ? 1 : -1) : (a ? cmp_values(*a, *b) : 0);
        return reverse ? -c : c;
    }

    int cmp_insensitive(std::string_view a, std::string_view b) const {
        const std::size_t l = std::min(a.size(), b.size());
        std::string_view lhs = reverse ? b.substr(0, l) : a.substr(0, l);
        std::string_view rhs = reverse ? a.substr(0, l) : b.substr(0, l);
        for (std::size_t i = 0; i < l; ++i) {
            const unsigned char x = static_cast<unsigned char>(lhs[i]);
            const unsigned char y = static_cast<unsigned char>(rhs[i]);
            const unsigned char lx = (x >= 'A' && x <= 'Z') ? x + 32 : x;
            const unsigned char ly = (y >= 'A' && y <= 'Z') ? y + 32 : y;
            if (lx != ly) return lx < ly ? -1 : 1;
        }
        return reverse ? cmp_values(b.size(), a.size()) : cmp_values(a.size(), b.size());
    }

    int fallback(const YFile& a, const YFile& b, int ord) const {
        return ord != 0 ? ord : cmp_str(a.urn(), b.urn());
    }

    int compare(const YFile& a, const YFile& b) const {
        if (dir_first && a.is_dir() != b.is_dir()) {
            return a.is_dir() ? -1 : 1;  // b.is_dir ().cmp (&a.is_dir ())
        }
        switch (by) {
            case SortBy::Mtime: return fallback(a, b, cmp_opt(a.mtime, b.mtime));
            case SortBy::Alphabetical:
                return fallback(a, b, sensitive ? cmp_str(a.urn(), b.urn()) : cmp_insensitive(a.urn(), b.urn()));
            case SortBy::Size: return fallback(a, b, cmp_u64(a.len, b.len));
        }
        return 0;
    }

    /// items.sort_unstable_by (...): std::sort is C++'s unstable sort.
    void sort(std::vector<YFile>& items) const {
        std::sort(items.begin(), items.end(), [this](const YFile& a, const YFile& b) { return compare(a, b) < 0; });
    }
};

inline FilesSorter sorter(Role role, bool reverse) {
    FilesSorter s;
    s.by = role == kName ? SortBy::Alphabetical : role == kSize ? SortBy::Size : SortBy::Mtime;
    s.reverse = reverse;
    return s;
}

inline YFile make_file(File& f) {
    YFile y;
    y.url = "/bench/" + f.path;
    y.urn_start = y.url.size() - f.name.size();
    y.kind = f.is_dir ? 1 : 0;
    y.len = f.size;
    y.atime = y.btime = y.ctime = y.mtime = f.mtime_ns;
    y.file = &f;
    return y;
}

}  // namespace yazi

// ---------------------------------------------------------------------------
// lf (nav.go, misc.go, diacritics.go), ported
// ---------------------------------------------------------------------------
namespace lf {

/// lf's normMap (diacritics.go): accented letters to their base letters.
inline const std::unordered_map<char32_t, char32_t>& norm_map() {
    static const std::unordered_map<char32_t, char32_t> map = {
    {0x0049, 0x0049}, {0x004F, 0x004F}, {0x0059, 0x0059}, {0x0069, 0x0069}, {0x006F, 0x006F}, {0x0079, 0x0079},
    {0x00C0, 0x0041}, {0x00C1, 0x0041}, {0x00C2, 0x0041}, {0x00C3, 0x0041}, {0x00C4, 0x0041}, {0x00C5, 0x0041},
    {0x00C7, 0x0043}, {0x00C8, 0x0045}, {0x00C9, 0x0045}, {0x00CA, 0x0045}, {0x00CB, 0x0045}, {0x00CC, 0x0049},
    {0x00CD, 0x0049}, {0x00CE, 0x0049}, {0x00CF, 0x0049}, {0x00D1, 0x004E}, {0x00D2, 0x004F}, {0x00D3, 0x004F},
    {0x00D4, 0x004F}, {0x00D5, 0x004F}, {0x00D6, 0x004F}, {0x00D8, 0x004F}, {0x00D9, 0x0055}, {0x00DA, 0x0055},
    {0x00DB, 0x0055}, {0x00DC, 0x0055}, {0x00DD, 0x0059}, {0x00E0, 0x0061}, {0x00E1, 0x0061}, {0x00E2, 0x0061},
    {0x00E3, 0x0061}, {0x00E4, 0x0061}, {0x00E5, 0x0061}, {0x00E7, 0x0063}, {0x00E8, 0x0065}, {0x00E9, 0x0065},
    {0x00EA, 0x0065}, {0x00EB, 0x0065}, {0x00EC, 0x0069}, {0x00ED, 0x0069}, {0x00EE, 0x0069}, {0x00EF, 0x0069},
    {0x00F1, 0x006E}, {0x00F2, 0x006F}, {0x00F3, 0x006F}, {0x00F4, 0x006F}, {0x00F5, 0x006F}, {0x00F6, 0x006F},
    {0x00F8, 0x006F}, {0x00F9, 0x0075}, {0x00FA, 0x0075}, {0x00FB, 0x0075}, {0x00FC, 0x0075}, {0x00FD, 0x0079},
    {0x00FF, 0x0079}, {0x0100, 0x0041}, {0x0101, 0x0061}, {0x0102, 0x0041}, {0x0103, 0x0061}, {0x0104, 0x0041},
    {0x0105, 0x0061}, {0x0106, 0x0043}, {0x0107, 0x0063}, {0x0108, 0x0043}, {0x0109, 0x0063}, {0x010C, 0x0043},
    {0x010D, 0x0063}, {0x010E, 0x0044}, {0x010F, 0x0064}, {0x0110, 0x0044}, {0x0111, 0x0064}, {0x0112, 0x0045},
    {0x0113, 0x0065}, {0x0116, 0x0045}, {0x0117, 0x0065}, {0x0118, 0x0045}, {0x0119, 0x0065}, {0x011A, 0x0045},
    {0x011B, 0x0065}, {0x011C, 0x0047}, {0x011D, 0x0067}, {0x011E, 0x0047}, {0x011F, 0x0067}, {0x0122, 0x0047},
    {0x0123, 0x0067}, {0x0124, 0x0048}, {0x0125, 0x0068}, {0x0128, 0x0049}, {0x0129, 0x0069}, {0x012A, 0x0049},
    {0x012B, 0x0069}, {0x012E, 0x0049}, {0x012F, 0x0069}, {0x0134, 0x004A}, {0x0135, 0x006A}, {0x0136, 0x004B},
    {0x0137, 0x006B}, {0x0139, 0x004C}, {0x013A, 0x006C}, {0x013B, 0x004C}, {0x013C, 0x006C}, {0x013D, 0x004C},
    {0x013E, 0x006C}, {0x0141, 0x004C}, {0x0142, 0x006C}, {0x0143, 0x004E}, {0x0144, 0x006E}, {0x0145, 0x004E},
    {0x0146, 0x006E}, {0x0147, 0x004E}, {0x0148, 0x006E}, {0x0150, 0x004F}, {0x0151, 0x006F}, {0x0154, 0x0052},
    {0x0155, 0x0072}, {0x0158, 0x0052}, {0x0159, 0x0072}, {0x015A, 0x0053}, {0x015B, 0x0073}, {0x015C, 0x0053},
    {0x015D, 0x0073}, {0x015E, 0x0053}, {0x015F, 0x0073}, {0x0160, 0x0053}, {0x0161, 0x0073}, {0x0164, 0x0054},
    {0x0165, 0x0074}, {0x0168, 0x0055}, {0x0169, 0x0075}, {0x016A, 0x0055}, {0x016B, 0x0075}, {0x016C, 0x0055},
    {0x016D, 0x0075}, {0x016E, 0x0055}, {0x016F, 0x0075}, {0x0170, 0x0055}, {0x0171, 0x0075}, {0x0172, 0x0055},
    {0x0173, 0x0075}, {0x0178, 0x0059}, {0x0179, 0x005A}, {0x017A, 0x007A}, {0x017B, 0x005A}, {0x017C, 0x007A},
    {0x017D, 0x005A}, {0x017E, 0x007A}, {0x01A0, 0x004F}, {0x01A1, 0x006F}, {0x01AF, 0x0055}, {0x01B0, 0x0075},
    {0x0218, 0x0053}, {0x0219, 0x0073}, {0x021A, 0x0054}, {0x021B, 0x0074}, {0x1EA0, 0x0041}, {0x1EA1, 0x0061},
    {0x1EA2, 0x0041}, {0x1EA3, 0x0061}, {0x1EA4, 0x0041}, {0x1EA5, 0x0061}, {0x1EA6, 0x0041}, {0x1EA7, 0x0061},
    {0x1EA8, 0x0041}, {0x1EA9, 0x0061}, {0x1EAA, 0x0041}, {0x1EAB, 0x0061}, {0x1EAC, 0x0041}, {0x1EAD, 0x0061},
    {0x1EAE, 0x0041}, {0x1EAF, 0x0061}, {0x1EB0, 0x0041}, {0x1EB1, 0x0061}, {0x1EB2, 0x0041}, {0x1EB3, 0x0061},
    {0x1EB4, 0x0041}, {0x1EB5, 0x0061}, {0x1EB6, 0x0041}, {0x1EB7, 0x0061}, {0x1EB8, 0x0045}, {0x1EB9, 0x0065},
    {0x1EBA, 0x0045}, {0x1EBB, 0x0065}, {0x1EBC, 0x0045}, {0x1EBD, 0x0065}, {0x1EBE, 0x0045}, {0x1EBF, 0x0065},
    {0x1EC0, 0x0045}, {0x1EC1, 0x0065}, {0x1EC2, 0x0045}, {0x1EC3, 0x0065}, {0x1EC4, 0x0045}, {0x1EC5, 0x0065},
    {0x1EC6, 0x0045}, {0x1EC7, 0x0065}, {0x1EC8, 0x0049}, {0x1EC9, 0x0069}, {0x1ECA, 0x0049}, {0x1ECB, 0x0069},
    {0x1ECC, 0x004F}, {0x1ECD, 0x006F}, {0x1ECE, 0x004F}, {0x1ECF, 0x006F}, {0x1ED0, 0x004F}, {0x1ED1, 0x006F},
    {0x1ED2, 0x004F}, {0x1ED3, 0x006F}, {0x1ED4, 0x004F}, {0x1ED5, 0x006F}, {0x1ED6, 0x004F}, {0x1ED7, 0x006F},
    {0x1ED8, 0x004F}, {0x1ED9, 0x006F}, {0x1EDA, 0x004F}, {0x1EDB, 0x006F}, {0x1EDC, 0x004F}, {0x1EDD, 0x006F},
    {0x1EDE, 0x004F}, {0x1EDF, 0x006F}, {0x1EE0, 0x004F}, {0x1EE1, 0x006F}, {0x1EE2, 0x004F}, {0x1EE3, 0x006F},
    {0x1EE4, 0x0055}, {0x1EE5, 0x0075}, {0x1EE6, 0x0055}, {0x1EE7, 0x0075}, {0x1EE8, 0x0055}, {0x1EE9, 0x0075},
    {0x1EEA, 0x0055}, {0x1EEB, 0x0075}, {0x1EEC, 0x0055}, {0x1EED, 0x0075}, {0x1EEE, 0x0055}, {0x1EEF, 0x0075},
    {0x1EF0, 0x0055}, {0x1EF1, 0x0075}, {0x1EF2, 0x0059}, {0x1EF3, 0x0079}, {0x1EF4, 0x0059}, {0x1EF5, 0x0079},
    {0x1EF6, 0x0059}, {0x1EF7, 0x0079}, {0x1EF8, 0x0059}, {0x1EF9, 0x0079},
    };
    return map;
}

inline void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

/// Decodes one UTF-8 rune like Go's range loop: invalid bytes become U+FFFD.
inline char32_t next_rune(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
    char32_t r = 0xFFFD;
    std::size_t len = 1;
    if (c < 0x80) {
        r = c;
    } else if (c >= 0xC2 && c < 0xE0 && cont(1)) {
        r = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F);
        len = 2;
    } else if (c >= 0xE0 && c < 0xF0 && cont(1) && cont(2)) {
        r = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
        len = 3;
        if (r < 0x800 || (r >= 0xD800 && r < 0xE000)) {
            r = 0xFFFD;
            len = 1;
        }
    } else if (c >= 0xF0 && c < 0xF5 && cont(1) && cont(2) && cont(3)) {
        r = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
        len = 4;
        if (r < 0x10000 || r > 0x10FFFF) {
            r = 0xFFFD;
            len = 1;
        }
    }
    i += len;
    return r;
}

/// normalize (): strings.ToLower, then removeDiacritics. Like lf, this runs
/// on every comparison and allocates new strings.
inline std::string normalize(std::string_view s) {
    bool ascii = true;
    for (char c : s) {
        ascii = ascii && static_cast<unsigned char>(c) < 0x80;
    }
    std::string lower;
    lower.reserve(s.size());
    if (ascii) {
        for (char c : s) {
            lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c);
        }
    } else {
        for (std::size_t i = 0; i < s.size();) {
            append_utf8(lower, static_cast<char32_t>(std::towlower(static_cast<wint_t>(next_rune(s, i)))));
        }
    }
    std::string out;
    out.reserve(lower.size());
    const auto& map = norm_map();
    for (std::size_t i = 0; i < lower.size();) {
        const char32_t r = next_rune(lower, i);
        const auto it = map.find(r);
        append_utf8(out, it != map.end() ? it->second : r);
    }
    return out;
}

inline bool is_digit(char b) { return '0' <= b && b <= '9'; }

/// strconv.Atoi on a run of digits: fails on overflow.
inline bool atoi(std::string_view tok, long long& out) {
    long long v = 0;
    for (char c : tok) {
        const int d = c - '0';
        if (v > (9223372036854775807LL - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

inline int natural_cmp(std::string_view s1, std::string_view s2) {
    const std::size_t s1len = s1.size(), s2len = s2.size();
    std::size_t lo1, lo2, hi1 = 0, hi2 = 0;
    for (;;) {
        if (hi1 >= s1len && hi2 >= s2len) return 0;
        if (hi1 >= s1len && hi2 < s2len) return -1;
        if (hi1 < s1len && hi2 >= s2len) return 1;

        lo1 = hi1;
        const bool isDigit1 = is_digit(s1[hi1]);
        while (hi1 < s1len && is_digit(s1[hi1]) == isDigit1) hi1++;
        const std::string_view tok1 = s1.substr(lo1, hi1 - lo1);

        lo2 = hi2;
        const bool isDigit2 = is_digit(s2[hi2]);
        while (hi2 < s2len && is_digit(s2[hi2]) == isDigit2) hi2++;
        const std::string_view tok2 = s2.substr(lo2, hi2 - lo2);

        if (isDigit1 && isDigit2) {
            long long num1 = 0, num2 = 0;
            if (atoi(tok1, num1) && atoi(tok2, num2)) {
                if (num1 != num2) {
                    return num1 < num2 ? -1 : 1;
                } else if (tok1.size() != tok2.size()) {
                    return tok1.size() < tok2.size() ? -1 : 1;
                }
            }
        }
        if (tok1 != tok2) {
            return tok1 < tok2 ? -1 : 1;
        }
    }
}

/// lf's comparison for a column (applySort's fn), on lf's values.
inline int column_compare(Role role, const File* f1, const File* f2) {
    switch (role) {
        case kName: return natural_cmp(normalize(f1->name), normalize(f2->name));
        case kSize: {
            // sizeVal (): dirSize is -1 (not calculated), so the file size.
            const auto a = static_cast<std::int64_t>(f1->size), b = static_cast<std::int64_t>(f2->size);
            return a < b ? -1 : (a > b ? 1 : 0);
        }
        case kModified: return f1->mtime_ns < f2->mtime_ns ? -1 : (f1->mtime_ns > f2->mtime_ns ? 1 : 0);
    }
    return 0;
}

/// The order dir.sort () produces: folders first (a stable pass after the
/// column sort), then the column, reversed by swapping the arguments.
inline int compare(Role role, bool reverse, const File* a, const File* b) {
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return reverse ? column_compare(role, b, a) : column_compare(role, a, b);
}

/// dir.sort () with hidden files shown: SortStableFunc by the column, then
/// SortStableFunc folders first.
inline void dir_sort(std::vector<File*>& files, Role role, bool reverse) {
    if (!reverse) {
        std::stable_sort(files.begin(), files.end(), [role](File* a, File* b) { return column_compare(role, a, b) < 0; });
    } else {
        std::stable_sort(files.begin(), files.end(), [role](File* a, File* b) { return column_compare(role, b, a) < 0; });
    }
    std::stable_sort(files.begin(), files.end(), [](File* a, File* b) { return a->is_dir && !b->is_dir; });
}

}  // namespace lf

}  // namespace terminal_family
