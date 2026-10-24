#pragma once
/// @file sort_key.hpp
/// One way of ordering the elements of a collection (name, size, modified...).

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "speedup/bucketization/keys.hpp"

namespace speedup::bucketization {

/// A sort criterion.
///
/// There are two kinds:
/// - **keyed** (`of_key`, `of`): every element maps to a normalized 64-bit key
///   (see keys.hpp). The order is "key first, then the optional tie-breaker".
///   Buckets are cut on the key, so assigning an element to its bucket costs a
///   binary search over a few integers.
/// - **comparator-only** (`of_comparator`): any three-way comparator, e.g.
///   natural ("file2" < "file10") or locale-aware ordering. Buckets are cut on
///   sampled splitter elements, so assigning an element costs
///   O(log buckets) comparator calls, paid once when the element is added.
///
/// Comparators are three-way: negative, zero or positive, like `strcmp`.
template <class T>
class SortKey {
public:
    using KeyFunction = std::function<std::uint64_t(const T&)>;
    using CompareFunction = std::function<int(const T&, const T&)>;

    /// Keyed sort key. `key` must return a normalized key; `tie_breaker`
    /// (optional) orders elements whose keys are equal, which is where a
    /// prefix key such as `keys::from_string` needs the full comparison.
    static SortKey of_key(std::string name, KeyFunction key, CompareFunction tie_breaker = {}) {
        if (!key) {
            throw std::invalid_argument("SortKey::of_key: key function is empty");
        }
        return SortKey(std::move(name), std::move(key), std::move(tie_breaker));
    }

    /// Comparator-only sort key.
    static SortKey of_comparator(std::string name, CompareFunction compare) {
        if (!compare) {
            throw std::invalid_argument("SortKey::of_comparator: comparator is empty");
        }
        return SortKey(std::move(name), KeyFunction{}, std::move(compare));
    }

    /// Keyed sort key from a projection (lambda or member pointer) returning
    /// an integer, enum, floating point or string. Numbers are encoded
    /// exactly; strings get a prefix key plus a full-string tie-breaker.
    template <class Projection>
    static SortKey of(std::string name, Projection projection) {
        using V = std::decay_t<std::invoke_result_t<Projection&, const T&>>;
        static_assert(keys::is_exact_v<V> || keys::is_string_like_v<V>,
                      "SortKey::of: projection must return a number, enum or string");
        KeyFunction key = [projection](const T& value) {
            return keys::normalize(std::invoke(projection, value));
        };
        if constexpr (keys::is_exact_v<V>) {
            return of_key(std::move(name), std::move(key));
        } else {
            CompareFunction full = [projection](const T& a, const T& b) {
                decltype(auto) x = std::invoke(projection, a);
                decltype(auto) y = std::invoke(projection, b);
                const int c = std::string_view(x).compare(std::string_view(y));
                return (c > 0) - (c < 0);
            };
            return of_key(std::move(name), std::move(key), std::move(full));
        }
    }

    const std::string& name() const noexcept { return name_; }

    /// True for keyed sort keys.
    bool has_key() const noexcept { return static_cast<bool>(key_); }

    /// Normalized key of `value`. Precondition: `has_key()`.
    std::uint64_t key(const T& value) const { return key_(value); }

    /// Orders elements with equal keys (keyed), or is the whole order
    /// (comparator-only). Returns 0 when there is no tie-breaker.
    int tie_break(const T& a, const T& b) const { return compare_ ? compare_(a, b) : 0; }

    /// The exact order this sort key defines.
    int compare(const T& a, const T& b) const {
        if (key_) {
            const std::uint64_t ka = key_(a);
            const std::uint64_t kb = key_(b);
            if (ka != kb) {
                return ka < kb ? -1 : 1;
            }
        }
        return tie_break(a, b);
    }

private:
    SortKey(std::string name, KeyFunction key, CompareFunction compare)
        : name_(std::move(name)), key_(std::move(key)), compare_(std::move(compare)) {
        if (name_.empty()) {
            throw std::invalid_argument("SortKey: name must not be empty");
        }
    }

    std::string name_;
    KeyFunction key_;
    CompareFunction compare_;
};

}  // namespace speedup::bucketization
