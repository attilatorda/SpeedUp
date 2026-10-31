#pragma once
/// @file sort.hpp
/// C++17 layer over the sorting C ABI (speedup/algorithms/sort.h): typed
/// overloads, std::vector convenience, and a stable radix sort of arbitrary
/// objects by a numeric key. Link against `speedup`.
///
/// Design and the list of sorts the standard libraries lack: docs/sorting/README.md

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "speedup/algorithms/sort.h"
#include "speedup/bucketization/keys.hpp"

namespace speedup::sort {

namespace detail {
template <class T>
inline constexpr bool always_false = false;

/// The C entry points take the six fixed-width types; any other integer
/// type of 4 or 8 bytes (int, long, size_t, ...) goes to the one with its
/// width and signedness.
template <class T>
inline constexpr bool is_word_v = (std::is_integral_v<T> && !std::is_same_v<T, bool> &&
                                   (sizeof(T) == 4 || sizeof(T) == 8)) ||
                                  std::is_same_v<T, float> || std::is_same_v<T, double>;
}  // namespace detail

/// LSD radix sort, ascending. Floating point sorts in IEEE 754 totalOrder
/// (-NaN first, +NaN last). `scratch`, if given, holds n elements.
template <class T>
void radix_sort(T* data, std::size_t n, T* scratch = nullptr) {
    static_assert(detail::is_word_v<T>, "radix_sort: 4- or 8-byte integers, float or double; use radix_sort_by_key");
    if constexpr (std::is_same_v<T, float>) {
        speedup_radix_sort_f32(data, n, scratch);
    } else if constexpr (std::is_same_v<T, double>) {
        speedup_radix_sort_f64(data, n, scratch);
    } else if constexpr (sizeof(T) == 4 && std::is_signed_v<T>) {
        speedup_radix_sort_i32(reinterpret_cast<std::int32_t*>(data), n, scratch);
    } else if constexpr (sizeof(T) == 4) {
        speedup_radix_sort_u32(reinterpret_cast<std::uint32_t*>(data), n, scratch);
    } else if constexpr (std::is_signed_v<T>) {
        speedup_radix_sort_i64(reinterpret_cast<std::int64_t*>(data), n, scratch);
    } else {
        speedup_radix_sort_u64(reinterpret_cast<std::uint64_t*>(data), n, scratch);
    }
}

template <class T>
void radix_sort(std::vector<T>& values) {
    radix_sort(values.data(), values.size());
}

/// Bucket sort, ascending; 8-byte integers and double.
template <class T>
void bucket_sort(T* data, std::size_t n, T* scratch = nullptr) {
    static_assert(detail::is_word_v<T> && sizeof(T) == 8 && !std::is_same_v<T, float>,
                  "bucket_sort: 8-byte integers or double");
    if constexpr (std::is_same_v<T, double>) {
        speedup_bucket_sort_f64(data, n, scratch);
    } else if constexpr (std::is_signed_v<T>) {
        speedup_bucket_sort_i64(reinterpret_cast<std::int64_t*>(data), n, scratch);
    } else {
        speedup_bucket_sort_u64(reinterpret_cast<std::uint64_t*>(data), n, scratch);
    }
}

template <class T>
void bucket_sort(std::vector<T>& values) {
    bucket_sort(values.data(), values.size());
}

/// Bitonic sort (a sorting network), ascending.
template <class T>
void bitonic_sort(T* data, std::size_t n) {
    static_assert(detail::is_word_v<T>, "bitonic_sort: 4- or 8-byte integers, float or double");
    if constexpr (std::is_same_v<T, float>) {
        speedup_bitonic_sort_f32(data, n);
    } else if constexpr (std::is_same_v<T, double>) {
        speedup_bitonic_sort_f64(data, n);
    } else if constexpr (sizeof(T) == 4 && std::is_signed_v<T>) {
        speedup_bitonic_sort_i32(reinterpret_cast<std::int32_t*>(data), n);
    } else if constexpr (sizeof(T) == 4) {
        speedup_bitonic_sort_u32(reinterpret_cast<std::uint32_t*>(data), n);
    } else if constexpr (std::is_signed_v<T>) {
        speedup_bitonic_sort_i64(reinterpret_cast<std::int64_t*>(data), n);
    } else {
        speedup_bitonic_sort_u64(reinterpret_cast<std::uint64_t*>(data), n);
    }
}

template <class T>
void bitonic_sort(std::vector<T>& values) {
    bitonic_sort(values.data(), values.size());
}

/// Bitonic merge sort (bitonic blocks, vectorized merges), ascending.
template <class T>
void bitonic_merge_sort(T* data, std::size_t n, T* scratch = nullptr) {
    static_assert(detail::is_word_v<T>, "bitonic_merge_sort: 4- or 8-byte integers, float or double");
    if constexpr (std::is_same_v<T, float>) {
        speedup_bitonic_merge_sort_f32(data, n, scratch);
    } else if constexpr (std::is_same_v<T, double>) {
        speedup_bitonic_merge_sort_f64(data, n, scratch);
    } else if constexpr (sizeof(T) == 4 && std::is_signed_v<T>) {
        speedup_bitonic_merge_sort_i32(reinterpret_cast<std::int32_t*>(data), n, scratch);
    } else if constexpr (sizeof(T) == 4) {
        speedup_bitonic_merge_sort_u32(reinterpret_cast<std::uint32_t*>(data), n, scratch);
    } else if constexpr (std::is_signed_v<T>) {
        speedup_bitonic_merge_sort_i64(reinterpret_cast<std::int64_t*>(data), n, scratch);
    } else {
        speedup_bitonic_merge_sort_u64(reinterpret_cast<std::uint64_t*>(data), n, scratch);
    }
}

template <class T>
void bitonic_merge_sort(std::vector<T>& values) {
    bitonic_merge_sort(values.data(), values.size());
}

/// Bitonic bucket sort (sampled buckets of about 64 keys, each sorted by the
/// bitonic network), ascending.
template <class T>
void bitonic_bucket_sort(T* data, std::size_t n, T* scratch = nullptr) {
    static_assert(detail::is_word_v<T>, "bitonic_bucket_sort: 4- or 8-byte integers, float or double");
    if constexpr (std::is_same_v<T, float>) {
        speedup_bitonic_bucket_sort_f32(data, n, scratch);
    } else if constexpr (std::is_same_v<T, double>) {
        speedup_bitonic_bucket_sort_f64(data, n, scratch);
    } else if constexpr (sizeof(T) == 4 && std::is_signed_v<T>) {
        speedup_bitonic_bucket_sort_i32(reinterpret_cast<std::int32_t*>(data), n, scratch);
    } else if constexpr (sizeof(T) == 4) {
        speedup_bitonic_bucket_sort_u32(reinterpret_cast<std::uint32_t*>(data), n, scratch);
    } else if constexpr (std::is_signed_v<T>) {
        speedup_bitonic_bucket_sort_i64(reinterpret_cast<std::int64_t*>(data), n, scratch);
    } else {
        speedup_bitonic_bucket_sort_u64(reinterpret_cast<std::uint64_t*>(data), n, scratch);
    }
}

template <class T>
void bitonic_bucket_sort(std::vector<T>& values) {
    bitonic_bucket_sort(values.data(), values.size());
}

/// Stable LSD radix sort of any movable objects by a numeric key:
/// `key(element)` returns an integer, floating-point, bool or enum value,
/// encoded with bucketization::keys::normalize (so NaN keys sort last, as
/// in Bucketization). Elements with equal keys keep their order.
///
/// Sorts (key, index) pairs, then moves every element once. String keys
/// are rejected: an 8-byte prefix is not an order; use name_sort.
template <class T, class KeyFn>
void radix_sort_by_key(std::vector<T>& values, KeyFn key) {
    using K = std::decay_t<decltype(key(values.front()))>;
    static_assert(bucketization::keys::is_exact_v<K>, "radix_sort_by_key: the key must be arithmetic or an enum");
    const std::size_t n = values.size();
    if (n < 2) return;
    struct Item {
        std::uint64_t key;
        std::size_t index;
    };
    std::vector<Item> items(n), other(n);
    std::array<std::array<std::size_t, 256>, 8> hist{};
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t k = bucketization::keys::normalize(key(values[i]));
        items[i] = {k, i};
        for (int d = 0; d < 8; ++d) ++hist[d][(k >> (8 * d)) & 0xFF];
    }
    for (int d = 0; d < 8; ++d) {
        const unsigned shift = 8 * static_cast<unsigned>(d);
        if (hist[d][(items[0].key >> shift) & 0xFF] == n) continue;
        std::size_t offset = 0;
        for (std::size_t& c : hist[d]) offset += std::exchange(c, offset);
        for (const Item& item : items) other[hist[d][(item.key >> shift) & 0xFF]++] = item;
        items.swap(other);
    }
    std::vector<T> sorted;
    sorted.reserve(n);
    for (const Item& item : items) sorted.push_back(std::move(values[item.index]));
    values = std::move(sorted);
}

/// Name sort over views: byte order, or ASCII-caseless with
/// SPEEDUP_NAME_SORT_ASCII_CASELESS. The views are reordered; the bytes they
/// point to are not touched.
inline void name_sort(std::vector<std::string_view>& names, std::uint32_t flags = SPEEDUP_NAME_SORT_BYTES,
                      speedup_name_sort_stats_t* stats = nullptr) {
    std::vector<speedup_str_t> views(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) views[i] = {names[i].data(), names[i].size()};
    speedup_name_sort_ex(views.data(), views.size(), flags, nullptr, stats);
    for (std::size_t i = 0; i < names.size(); ++i) names[i] = std::string_view(views[i].data, views[i].len);
}

/// Name sort of strings. Sorts views, then moves every string once; a
/// string is found again by its buffer address. Should two strings share a
/// buffer (copy-on-write implementations), the sorted strings are copied
/// from the views instead.
inline void name_sort(std::vector<std::string>& names, std::uint32_t flags = SPEEDUP_NAME_SORT_BYTES,
                      speedup_name_sort_stats_t* stats = nullptr) {
    const std::size_t n = names.size();
    std::vector<speedup_str_t> views(n);
    std::unordered_map<const char*, std::size_t> where;
    where.reserve(n);
    bool unique = true;
    for (std::size_t i = 0; i < n; ++i) {
        views[i] = {names[i].data(), names[i].size()};
        unique = where.emplace(names[i].data(), i).second && unique;
    }
    speedup_name_sort_ex(views.data(), n, flags, nullptr, stats);
    std::vector<std::string> sorted;
    sorted.reserve(n);
    for (const speedup_str_t& v : views) {
        if (unique) {
            sorted.push_back(std::move(names[where.at(v.data)]));
        } else {
            sorted.emplace_back(v.data ? v.data : "", v.len);
        }
    }
    names = std::move(sorted);
}

}  // namespace speedup::sort
