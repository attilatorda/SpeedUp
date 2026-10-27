#pragma once
// Loader for directory listings captured with benchmarks/datasets/capture_listing.py.

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace speedup_bench {

struct Entry {
    std::string path;  // relative to the captured root, unique
    std::string name;  // basename: what a file manager sorts by
    bool is_dir = false;
    bool is_hidden = false;
    std::uint64_t size = 0;
    std::int64_t mtime_ns = 0;
    std::int64_t atime_ns = 0;
};

inline std::string unescape(const std::string& field) {
    std::string out;
    out.reserve(field.size());
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size()) {
            const char c = field[++i];
            out.push_back(c == 't' ? '\t' : c == 'n' ? '\n' : c);
        } else {
            out.push_back(field[i]);
        }
    }
    return out;
}

/// Reads a TSV listing. Throws std::runtime_error on a missing file or bad row.
inline std::vector<Entry> load_listing(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open dataset " + file);
    }
    std::vector<Entry> entries;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t tab; (tab = line.find('\t', start)) != std::string::npos; start = tab + 1) {
            fields.push_back(line.substr(start, tab - start));
        }
        fields.push_back(line.substr(start));
        if (fields.size() != 7) {
            throw std::runtime_error("bad row in " + file + ": " + line);
        }
        Entry e;
        e.path = unescape(fields[0]);
        e.name = unescape(fields[1]);
        e.is_dir = fields[2] == "1";
        e.is_hidden = fields[3] == "1";
        e.size = std::stoull(fields[4]);
        e.mtime_ns = std::stoll(fields[5]);
        e.atime_ns = std::stoll(fields[6]);
        entries.push_back(std::move(e));
    }
    return entries;
}

/// Dataset name from a path: ".../usr-lib.tsv" -> "usr-lib".
inline std::string dataset_name(const std::string& file) {
    std::size_t slash = file.find_last_of("/\\");
    std::string base = slash == std::string::npos ? file : file.substr(slash + 1);
    std::size_t dot = base.find('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

}  // namespace speedup_bench
