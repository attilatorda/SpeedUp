#pragma once
/// @file bucketization.hpp
/// The Bucketization interface: fast re-sorting of a collection that can be
/// ordered in several ways.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "speedup/bucketization/bucket_layout.hpp"
#include "speedup/bucketization/bucket_plan.hpp"
#include "speedup/bucketization/sort_key.hpp"

namespace speedup::bucketization {

enum class Direction { ascending, descending };

/// Snapshot of how one sort key's elements are spread over its buckets.
struct BucketStats {
    std::string key_name;
    std::size_t size = 0;
    std::size_t bucket_count = 0;
    std::vector<std::size_t> actual_sizes;    ///< elements per bucket now
    std::vector<std::size_t> expected_sizes;  ///< elements per bucket when the layout was estimated
    std::size_t empty_buckets = 0;
    std::size_t sorted_buckets = 0;  ///< buckets already fully sorted in the cached order
    double max_skew = 0.0;           ///< max of actual / max(expected, page size)
    std::size_t first_page_cost_ascending = 0;   ///< elements to sort before the first ascending page shows
    std::size_t first_page_cost_descending = 0;  ///< same for descending order
    bool needs_rebalance = false;
};

/// A collection whose elements can be sorted by any of several sort keys,
/// where switching between them is cheap.
///
/// Every element is assigned to a bucket per sort key once, when it is added
/// ("pre-sorting"). Selecting a sort key then only groups the elements by
/// bucket id (a counting pass, no comparisons), and a bucket is fully sorted
/// the first time a position inside it is read. A view showing the first
/// rows therefore pays for O(n + page log page) work instead of O(n log n).
///
/// Positions are indexes into the current order (sort key and direction).
/// Bucket ids always follow ascending key order. Descending order is the
/// exact reverse of ascending order. Equal elements keep insertion order in
/// ascending order.
///
/// Reads may sort lazily, so they are not const and implementations are not
/// thread-safe. Elements must not be changed in a way that affects their
/// order except through `update_if`.
template <class T>
class Bucketization {
public:
    using Predicate = std::function<bool(const T&)>;
    using Mutator = std::function<void(T&)>;
    using Visitor = std::function<void(const T&)>;

    virtual ~Bucketization() = default;

    // --- sort keys -------------------------------------------------------

    /// Registers a sort key and assigns every element to one of its buckets.
    /// Throws std::invalid_argument if the name is already taken.
    virtual void add_sort_key(SortKey<T> key) = 0;

    /// Removes a sort key. If it was active the order reverts to insertion order.
    virtual bool remove_sort_key(std::string_view name) = 0;

    virtual std::vector<std::string> sort_key_names() const = 0;

    // --- selecting the order ----------------------------------------------

    /// Switches the order: O(n) grouping, no comparisons. Switching back to a
    /// sort key used before reuses its cached grouping and sorted buckets.
    /// Throws std::invalid_argument for an unknown name.
    virtual void sort_by(std::string_view name, Direction direction = Direction::ascending) = 0;

    /// Back to insertion order.
    virtual void unsort() = 0;

    /// The active sort key, or nullptr in insertion order. The pointer stays
    /// valid until a sort key is added or removed.
    virtual const SortKey<T>* active_sort_key() const = 0;

    virtual Direction direction() const = 0;

    // --- reading in order ---------------------------------------------------

    virtual std::size_t size() const = 0;

    /// Element at `position` in the current order (sorts its bucket if needed).
    /// Throws std::out_of_range.
    virtual const T& at(std::size_t position) = 0;

    /// Elements at positions [from, to), clamped to size(). Typically the
    /// visible rows of a view.
    virtual std::vector<T> range(std::size_t from, std::size_t to) = 0;

    /// The first `plan().page_size()` elements.
    virtual std::vector<T> first_page() = 0;

    /// Position of the first element (in insertion order) matching `match`.
    virtual std::optional<std::size_t> position_if(const Predicate& match) = 0;

    /// Visits every element in order, sorting buckets as they are reached.
    /// `visit` must not modify the collection.
    virtual void for_each(const Visitor& visit) = 0;

    /// Fully sorts every bucket of the active sort key now (e.g. when idle).
    virtual void sort_all() = 0;

    // --- buckets and distribution -------------------------------------------

    /// Bucket `value` belongs to under the named sort key's current layout.
    /// `value` does not have to be in the collection.
    virtual std::size_t bucket_of(const T& value, std::string_view key) const = 0;

    virtual std::size_t bucket_count(std::string_view key) const = 0;

    /// Current number of elements per bucket.
    virtual std::vector<std::size_t> bucket_sizes(std::string_view key) const = 0;

    /// Current boundaries and expected sizes.
    virtual const BucketLayout<T>& layout(std::string_view key) const = 0;

    virtual BucketStats stats(std::string_view key) const = 0;

    /// Bucket sizes `plan` would produce on the current data (sample based,
    /// nothing is changed).
    virtual std::vector<std::size_t> estimate_bucket_sizes(std::string_view key, const BucketPlan& plan) const = 0;

    /// True when the collection grew past twice the size the layout was
    /// estimated from, or a bucket exceeds the plan's skew limit.
    virtual bool needs_rebalance(std::string_view key) const = 0;

    /// Re-estimates the layout from the current data and reassigns buckets.
    virtual void rebalance(std::string_view key) = 0;

    virtual void rebalance_all() = 0;

    virtual const BucketPlan& plan() const = 0;

    /// Replaces the plan and rebalances every sort key.
    virtual void set_plan(BucketPlan plan) = 0;

    // --- changing the collection ---------------------------------------------

    virtual void add(T value) = 0;

    /// Bulk insert; stale layouts are re-estimated once at the end.
    virtual void add_all(std::vector<T> values) = 0;

    /// Removes every matching element; returns how many were removed.
    virtual std::size_t remove_if(const Predicate& match) = 0;

    /// Applies `mutate` (optional) to every matching element and re-reads
    /// its keys. Call it after anything that changes an element's order.
    virtual std::size_t update_if(const Predicate& match, const Mutator& mutate = {}) = 0;

    virtual void clear() = 0;

    // --- conveniences (need operator==) ----------------------------------------
    // update() and position_of() are meant for handle element types
    // (pointers, ids, shared handles) whose target was changed elsewhere.

    bool remove(const T& value) {
        return remove_if([&value](const T& x) { return x == value; }) > 0;
    }

    bool update(const T& value) {
        return update_if([&value](const T& x) { return x == value; }) > 0;
    }

    std::optional<std::size_t> position_of(const T& value) {
        return position_if([&value](const T& x) { return x == value; });
    }
};

}  // namespace speedup::bucketization
