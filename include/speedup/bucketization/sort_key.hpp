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
///
/// Either kind can have **groups** (`with_groups`): a small number of
/// classes, such as folders and files, that stay in ascending group order in
/// both directions. Descending order then reverses the order inside each
/// group only, which is how file managers keep folders first.
template <class T>
class SortKey {
public:
    using KeyFunction = std::function<std::uint64_t(const T&)>;
    using CompareFunction = std::function<int(const T&, const T&)>;
    using GroupFunction = std::function<std::size_t(const T&)>;

    /// Most groups a sort key can have.
    static constexpr std::size_t max_groups = 256;

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

    /// Copy of this sort key whose elements are first ordered by
    /// `group(element)`, a value in [0, groups), in both directions.
    SortKey with_groups(std::size_t groups, GroupFunction group) const {
        if (groups == 0 || groups > max_groups) {
            throw std::invalid_argument("SortKey::with_groups: groups must be in [1, 256]");
        }
        if (!group) {
            throw std::invalid_argument("SortKey::with_groups: group function is empty");
        }
        SortKey copy = *this;
        copy.groups_ = groups;
        copy.group_ = std::move(group);
        return copy;
    }

    const std::string& name() const noexcept { return name_; }

    /// Number of groups; 1 when the sort key has none.
    std::size_t group_count() const noexcept { return groups_; }

    /// Group of `value`. Throws std::out_of_range if the group function
    /// returns a value outside [0, group_count()).
    std::size_t group(const T& value) const {
        if (!group_) {
            return 0;
        }
        const std::size_t g = group_(value);
        if (g >= groups_) {
            throw std::out_of_range("SortKey '" + name_ + "': group out of range");
        }
        return g;
    }

    /// True for keyed sort keys.
    bool has_key() const noexcept { return static_cast<bool>(key_); }

    /// Normalized key of `value`. Precondition: `has_key()`.
    std::uint64_t key(const T& value) const { return key_(value); }

    /// Orders elements with equal keys (keyed), or is the whole order
    /// (comparator-only). Returns 0 when there is no tie-breaker.
    int tie_break(const T& a, const T& b) const { return compare_ ? compare_(a, b) : 0; }

    /// The exact (ascending) order this sort key defines: group, then key,
    /// then tie-breaker.
    int compare(const T& a, const T& b) const {
        if (group_) {
            const std::size_t ga = group(a);
            const std::size_t gb = group(b);
            if (ga != gb) {
                return ga < gb ? -1 : 1;
            }
        }
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
    std::size_t groups_ = 1;
    GroupFunction group_;
};

}  // namespace speedup::bucketization
