#pragma once
// The Godot editor's FileSystem dock sort path, extracted for benchmarking.
//
// Godot c8256a8 (MIT License, Copyright (c) 2014-present Godot Engine
// contributors; (c) 2007-2014 Juan Linietsky, Ariel Manzur):
//   editor/file_system/file_info.{h,cpp}   FileInfo, comparators, sort_file_info_list()
//   editor/docks/filesystem_dock.cpp       FileSystemDock::_update_file_list()
//   core/templates/sort_list.h             SortList (linked-list merge sort)
//   core/templates/list.h                  List::sort_custom(), List::reverse()
//   core/string/ustring.cpp                filenocasecmp_to() and its helpers
//
// Godot's String is a char32_t string; std::u32string stands in for it, with
// the same allocations where Godot's code allocates (get_extension(),
// get_basename(), operator+, path_join()). _find_upper() binary-searches a
// table of simple uppercase mappings like Godot's caps_table (1,525 entries);
// the table here is built from the C library's towupper().

#include <cwctype>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace godot {

using String = std::u32string;

// --- core/string ------------------------------------------------------------

inline const std::vector<std::pair<int, int>>& caps_table() {
    static const std::vector<std::pair<int, int>> table = [] {
        std::vector<std::pair<int, int>> t;
        for (int c = 0; c < 0x1F000; ++c) {
            const int u = static_cast<int>(std::towupper(static_cast<wint_t>(c)));
            if (u != c) {
                t.emplace_back(c, u);
            }
        }
        return t;
    }();
    return table;
}

inline int _find_upper(int p_char) {
    const auto& table = caps_table();
    int low = 0;
    int high = static_cast<int>(table.size()) - 1;
    while (low <= high) {
        const int middle = (low + high) / 2;
        if (p_char < table[middle].first) {
            high = middle - 1;
        } else if (table[middle].first < p_char) {
            low = middle + 1;
        } else {
            return table[middle].second;
        }
    }
    return p_char;
}

inline bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }

inline signed char natural_cmp_common(const char32_t*& r_this_str, const char32_t*& r_that_str) {
    // Keep ptrs to start of numerical sequences.
    const char32_t* this_substr = r_this_str;
    const char32_t* that_substr = r_that_str;

    // Compare lengths of both numerical sequences, ignoring leading zeros.
    while (is_digit(*r_this_str)) {
        r_this_str++;
    }
    while (is_digit(*r_that_str)) {
        r_that_str++;
    }
    while (*this_substr == '0') {
        this_substr++;
    }
    while (*that_substr == '0') {
        that_substr++;
    }
    const auto this_len = r_this_str - this_substr;
    const auto that_len = r_that_str - that_substr;

    if (this_len < that_len) {
        return -1;
    } else if (this_len > that_len) {
        return 1;
    }

    // If lengths equal, compare lexicographically.
    while (this_substr != r_this_str && that_substr != r_that_str) {
        if (*this_substr < *that_substr) {
            return -1;
        } else if (*this_substr > *that_substr) {
            return 1;
        }
        this_substr++;
        that_substr++;
    }

    return 0;
}

inline signed char naturalnocasecmp_to_base(const char32_t* p_this_str, const char32_t* p_that_str) {
    if (p_this_str && p_that_str) {
        while (*p_this_str == '.' || *p_that_str == '.') {
            if (*p_this_str++ != '.') {
                return 1;
            }
            if (*p_that_str++ != '.') {
                return -1;
            }
            if (!*p_that_str) {
                return 1;
            }
            if (!*p_this_str) {
                return -1;
            }
        }

        while (*p_this_str) {
            if (!*p_that_str) {
                return 1;
            } else if (is_digit(*p_this_str)) {
                if (!is_digit(*p_that_str)) {
                    return -1;
                }

                signed char ret = natural_cmp_common(p_this_str, p_that_str);
                if (ret) {
                    return ret;
                }
            } else if (is_digit(*p_that_str)) {
                return 1;
            } else {
                if (_find_upper(*p_this_str) < _find_upper(*p_that_str)) {
                    return -1;
                } else if (_find_upper(*p_this_str) > _find_upper(*p_that_str)) {
                    return 1;
                }

                p_this_str++;
                p_that_str++;
            }
        }
        if (*p_that_str) {
            return -1;
        }
    }

    return 0;
}

inline signed char file_cmp_common(const char32_t*& r_this_str, const char32_t*& r_that_str) {
    // Compare leading `_` sequences.
    while ((*r_this_str == '_' && *r_that_str) || (*r_this_str && *r_that_str == '_')) {
        // Sort `_` lower than everything except `.`
        if (*r_this_str != '_') {
            return *r_this_str == '.' ? -1 : 1;
        } else if (*r_that_str != '_') {
            return *r_that_str == '.' ? 1 : -1;
        }
        r_this_str++;
        r_that_str++;
    }

    return 0;
}

/// String::filenocasecmp_to ().
inline signed char filenocasecmp_to(const String& a, const String& b) {
    const char32_t* this_str = a.c_str();
    const char32_t* that_str = b.c_str();

    signed char ret = file_cmp_common(this_str, that_str);
    if (ret) {
        return ret;
    }

    return naturalnocasecmp_to_base(this_str, that_str);
}

struct FileNoCaseComparator {
    bool operator()(const String& p_a, const String& p_b) const { return filenocasecmp_to(p_a, p_b) < 0; }
};

inline String get_extension(const String& s) {
    const auto pos = s.rfind(U'.');
    const auto slash = s.find_last_of(U"/\\");
    if (pos == String::npos || (slash != String::npos && pos < slash)) {
        return String();
    }
    return s.substr(pos + 1);
}

inline String get_basename(const String& s) {
    const auto pos = s.rfind(U'.');
    const auto slash = s.find_last_of(U"/\\");
    if (pos == String::npos || (slash != String::npos && pos < slash)) {
        return s;
    }
    return s.substr(0, pos);
}

inline String path_join(const String& base, const String& file) {
    if (base.empty()) {
        return file;
    }
    if (base.back() == U'/' || (!file.empty() && file.front() == U'/')) {
        return base + file;
    }
    return base + U"/" + file;
}

// --- editor/file_system/file_info.h --------------------------------------------

enum class FileSortOption {
    FILE_SORT_NAME = 0,
    FILE_SORT_NAME_REVERSE = 1,
    FILE_SORT_TYPE = 2,
    FILE_SORT_TYPE_REVERSE = 3,
    FILE_SORT_MODIFIED_TIME = 4,
    FILE_SORT_MODIFIED_TIME_REVERSE = 5,
};

struct FileInfo {
    String name;
    String path;
    String icon_path;
    const String* type = nullptr;  // StringName: an interned string
    std::vector<String> sources;
    bool import_broken = false;
    uint64_t modified_time = 0;
    const void* source = nullptr;  // the benchmark's file (not in Godot)

    bool operator<(const FileInfo& p_fi) const { return FileNoCaseComparator()(name, p_fi.name); }
};

struct FileInfoTypeComparator {
    bool operator()(const FileInfo& p_a, const FileInfo& p_b) const {
        return FileNoCaseComparator()(get_extension(p_a.name) + *p_a.type + get_basename(p_a.name),
                                      get_extension(p_b.name) + *p_b.type + get_basename(p_b.name));
    }
};

struct FileInfoModifiedTimeComparator {
    bool operator()(const FileInfo& p_a, const FileInfo& p_b) const { return p_a.modified_time > p_b.modified_time; }
};

struct FileInfoNameComparator {
    bool operator()(const FileInfo& p_a, const FileInfo& p_b) const { return p_a < p_b; }
};

// --- core/templates/list.h and sort_list.h ----------------------------------------

/// Godot's List<FileInfo>: a doubly linked list of heap-allocated elements.
class FileList {
public:
    struct Element {
        FileInfo value;
        Element* next_ptr = nullptr;
        Element* prev_ptr = nullptr;
    };

    FileList() = default;
    FileList(const FileList&) = delete;
    FileList& operator=(const FileList&) = delete;
    ~FileList() { clear(); }

    void push_back(FileInfo value) {
        auto* e = new Element{std::move(value)};
        e->prev_ptr = last_;
        if (last_) {
            last_->next_ptr = e;
        } else {
            first_ = e;
        }
        last_ = e;
        ++size_;
    }
    void clear() {
        while (first_) {
            Element* next = first_->next_ptr;
            delete first_;
            first_ = next;
        }
        last_ = nullptr;
        size_ = 0;
    }
    int size() const { return size_; }
    Element* front() const { return first_; }

    void reverse() {
        int s = size() / 2;
        Element* F = first_;
        Element* B = last_;
        for (int i = 0; i < s; i++) {
            std::swap(F->value, B->value);
            F = F->next_ptr;
            B = B->prev_ptr;
        }
    }

    template <class Comparator>
    void sort_custom() {
        if (size() < 2) {
            return;
        }
        SortList<Comparator> sorter;
        sorter.sort(first_, last_);
    }

private:
    template <class Comparator>
    struct SortList {
        Comparator compare;

        void sort(Element*& r_head, Element*& r_tail) {
            Element* sorted_until;
            if (_is_sorted(r_head, r_tail, sorted_until)) {
                return;
            }
            Element* head_prev = r_head->prev_ptr;
            r_head->prev_ptr = nullptr;
            Element* tail_next = r_tail->next_ptr;
            r_tail->next_ptr = nullptr;
            Element* head2 = sorted_until->next_ptr;
            _split(sorted_until, head2);
            _merge_sort(head2, r_tail);
            _merge(r_head, sorted_until, head2, r_tail, r_head, r_tail);
            if (head_prev) {
                _connect(head_prev, r_head);
            }
            if (tail_next) {
                _connect(r_tail, tail_next);
            }
        }

        bool _is_sorted(Element* p_head, Element* p_tail, Element*& r_sorted_until) {
            r_sorted_until = p_head;
            while (r_sorted_until != p_tail) {
                if (compare(r_sorted_until->next_ptr->value, r_sorted_until->value)) {
                    return false;
                }
                r_sorted_until = r_sorted_until->next_ptr;
            }
            return true;
        }

        void _merge_sort(Element*& r_head, Element*& r_tail) {
            if (r_head == r_tail) {
                return;
            }
            Element* tail1 = _get_mid(r_head);
            Element* head2 = tail1->next_ptr;
            _split(tail1, head2);
            _merge_sort(r_head, tail1);
            _merge_sort(head2, r_tail);
            _merge(r_head, tail1, head2, r_tail, r_head, r_tail);
        }

        void _merge(Element* p_head1, Element* p_tail1, Element* p_head2, Element* p_tail2, Element*& r_head,
                    Element*& r_tail) {
            if (compare(p_head2->value, p_head1->value)) {
                r_head = p_head2;
                p_head2 = p_head2->next_ptr;
            } else {
                r_head = p_head1;
                p_head1 = p_head1->next_ptr;
            }
            Element* curr = r_head;
            while (p_head1 && p_head2) {
                if (compare(p_head2->value, p_head1->value)) {
                    _connect(curr, p_head2);
                    p_head2 = p_head2->next_ptr;
                } else {
                    _connect(curr, p_head1);
                    p_head1 = p_head1->next_ptr;
                }
                curr = curr->next_ptr;
            }
            if (p_head1) {
                _connect(curr, p_head1);
                r_tail = p_tail1;
            } else {
                _connect(curr, p_head2);
                r_tail = p_tail2;
            }
        }

        Element* _get_mid(Element* p_head) {
            Element* end = p_head;
            Element* mid = p_head;
            while (end->next_ptr && end->next_ptr->next_ptr) {
                end = end->next_ptr->next_ptr;
                mid = mid->next_ptr;
            }
            return mid;
        }

        static void _connect(Element* p_a, Element* p_b) {
            p_a->next_ptr = p_b;
            p_b->prev_ptr = p_a;
        }
        static void _split(Element* p_a, Element* p_b) {
            p_a->next_ptr = nullptr;
            p_b->prev_ptr = nullptr;
        }
    };

    Element* first_ = nullptr;
    Element* last_ = nullptr;
    int size_ = 0;
};

// --- editor/file_system/file_info.cpp ------------------------------------------

inline void sort_file_info_list(FileList& r_file_list, FileSortOption p_file_sort_option) {
    switch (p_file_sort_option) {
        case FileSortOption::FILE_SORT_TYPE:
            r_file_list.sort_custom<FileInfoTypeComparator>();
            break;
        case FileSortOption::FILE_SORT_TYPE_REVERSE:
            r_file_list.sort_custom<FileInfoTypeComparator>();
            r_file_list.reverse();
            break;
        case FileSortOption::FILE_SORT_MODIFIED_TIME:
            r_file_list.sort_custom<FileInfoModifiedTimeComparator>();
            break;
        case FileSortOption::FILE_SORT_MODIFIED_TIME_REVERSE:
            r_file_list.sort_custom<FileInfoModifiedTimeComparator>();
            r_file_list.reverse();
            break;
        case FileSortOption::FILE_SORT_NAME_REVERSE:
            r_file_list.sort_custom<FileInfoNameComparator>();
            r_file_list.reverse();
            break;
        case FileSortOption::FILE_SORT_NAME:
            r_file_list.sort_custom<FileInfoNameComparator>();
            break;
    }
}

}  // namespace godot
