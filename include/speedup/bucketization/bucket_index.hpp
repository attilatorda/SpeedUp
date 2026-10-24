#pragma once
/// @file bucket_index.hpp
/// Reference implementation of the Bucketization interface.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "speedup/bucketization/bucketization.hpp"

namespace speedup::bucketization {

/// Owns the elements and, per sort key, their normalized keys, bucket ids and
/// a cached grouping of the elements by bucket.
///
/// Costs (n elements, k buckets, p page size):
/// - add: O(sort keys * log k), comparator-only keys pay log k comparisons
/// - sort_by: O(n + k), or O(1) when the sort key's grouping is still cached
/// - first page: sorts the bucket(s) covering it, about O(p log p)
/// - remove_if / update_if: O(n) per call, independent of the number of matches
///
/// Use a cheap-to-copy element type (a pointer, an id, a shared handle) when
/// elements are large; range() returns copies.
template <class T>
class BucketIndex final : public Bucketization<T> {
public:
    using typename Bucketization<T>::Predicate;
    using typename Bucketization<T>::Mutator;
    using typename Bucketization<T>::Visitor;

    explicit BucketIndex(BucketPlan plan = BucketPlan::defaults()) : plan_(std::move(plan)) {}

    // --- sort keys -------------------------------------------------------

    void add_sort_key(SortKey<T> key) override {
        if (find_state(key.name()) != npos) {
            throw std::invalid_argument("BucketIndex: duplicate sort key '" + key.name() + "'");
        }
        // Built aside so a throwing key function leaves the index unchanged.
        KeyState state(std::move(key));
        if (state.key.has_key()) {
            state.keys.reserve(items_.size());
            for (const T& item : items_) {
                state.keys.push_back(state.key.key(item));
            }
        }
        relayout(state);
        states_.push_back(std::move(state));
    }

    bool remove_sort_key(std::string_view name) override {
        const std::size_t index = find_state(name);
        if (index == npos) {
            return false;
        }
        states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(index));
        if (active_ == index) {
            active_ = npos;
        } else if (active_ != npos && active_ > index) {
            --active_;
        }
        return true;
    }

    std::vector<std::string> sort_key_names() const override {
        std::vector<std::string> names;
        names.reserve(states_.size());
        for (const KeyState& state : states_) {
            names.push_back(state.key.name());
        }
        return names;
    }

    // --- selecting the order ----------------------------------------------

    void sort_by(std::string_view name, Direction direction = Direction::ascending) override {
        active_ = require_state(name);
        direction_ = direction;
        prepare(states_[active_]);
    }

    void unsort() override { active_ = npos; }

    const SortKey<T>* active_sort_key() const override {
        return active_ == npos ? nullptr : &states_[active_].key;
    }

    Direction direction() const override { return direction_; }

    // --- reading in order ---------------------------------------------------

    std::size_t size() const override { return items_.size(); }

    const T& at(std::size_t position) override {
        if (position >= items_.size()) {
            throw std::out_of_range("BucketIndex::at: position out of range");
        }
        if (active_ == npos) {
            return items_[position];
        }
        KeyState& state = states_[active_];
        prepare(state);
        const std::size_t ascending = to_ascending(position);
        sort_bucket(state, bucket_at(state, ascending));
        return items_[state.order[ascending]];
    }

    std::vector<T> range(std::size_t from, std::size_t to) override {
        to = std::min(to, items_.size());
        std::vector<T> out;
        if (from >= to) {
            return out;
        }
        out.reserve(to - from);
        if (active_ == npos) {
            out.insert(out.end(), items_.begin() + static_cast<std::ptrdiff_t>(from),
                       items_.begin() + static_cast<std::ptrdiff_t>(to));
            return out;
        }
        KeyState& state = states_[active_];
        prepare(state);
        const std::size_t n = items_.size();
        const std::size_t lo = direction_ == Direction::ascending ? from : n - to;
        const std::size_t hi = direction_ == Direction::ascending ? to : n - from;
        const std::size_t last_bucket = bucket_at(state, hi - 1);
        for (std::size_t b = bucket_at(state, lo); b <= last_bucket; ++b) {
            sort_bucket(state, b);
        }
        if (direction_ == Direction::ascending) {
            for (std::size_t i = lo; i < hi; ++i) {
                out.push_back(items_[state.order[i]]);
            }
        } else {
            for (std::size_t i = hi; i-- > lo;) {
                out.push_back(items_[state.order[i]]);
            }
        }
        return out;
    }

    std::vector<T> first_page() override { return range(0, plan_.page_size()); }

    std::optional<std::size_t> position_if(const Predicate& match) override {
        std::size_t slot = 0;
        while (slot < items_.size() && !match(items_[slot])) {
            ++slot;
        }
        if (slot == items_.size()) {
            return std::nullopt;
        }
        if (active_ == npos) {
            return slot;
        }
        KeyState& state = states_[active_];
        prepare(state);
        const std::size_t bucket = state.buckets[slot];
        sort_bucket(state, bucket);
        const auto begin = state.order.begin() + static_cast<std::ptrdiff_t>(state.start[bucket]);
        const auto end = state.order.begin() + static_cast<std::ptrdiff_t>(state.start[bucket + 1]);
        const auto it = std::find(begin, end, static_cast<Slot>(slot));
        return to_ascending(static_cast<std::size_t>(it - state.order.begin()));
    }

    void for_each(const Visitor& visit) override {
        if (active_ == npos) {
            for (const T& item : items_) {
                visit(item);
            }
            return;
        }
        KeyState& state = states_[active_];
        prepare(state);
        const std::size_t k = state.layout.bucket_count();
        for (std::size_t i = 0; i < k; ++i) {
            const std::size_t b = direction_ == Direction::ascending ? i : k - 1 - i;
            sort_bucket(state, b);
            const std::size_t lo = state.start[b];
            const std::size_t hi = state.start[b + 1];
            if (direction_ == Direction::ascending) {
                for (std::size_t p = lo; p < hi; ++p) {
                    visit(items_[state.order[p]]);
                }
            } else {
                for (std::size_t p = hi; p-- > lo;) {
                    visit(items_[state.order[p]]);
                }
            }
        }
    }

    void sort_all() override {
        if (active_ == npos) {
            return;
        }
        KeyState& state = states_[active_];
        prepare(state);
        for (std::size_t b = 0; b < state.layout.bucket_count(); ++b) {
            sort_bucket(state, b);
        }
    }

    // --- buckets and distribution -------------------------------------------

    std::size_t bucket_of(const T& value, std::string_view key) const override {
        const KeyState& state = states_[require_state(key)];
        return state.layout.bucket_of(value, state.key);
    }

    std::size_t bucket_count(std::string_view key) const override {
        return states_[require_state(key)].layout.bucket_count();
    }

    std::vector<std::size_t> bucket_sizes(std::string_view key) const override {
        return states_[require_state(key)].counts;
    }

    const BucketLayout<T>& layout(std::string_view key) const override {
        return states_[require_state(key)].layout;
    }

    BucketStats stats(std::string_view key) const override {
        const KeyState& state = states_[require_state(key)];
        BucketStats out;
        out.key_name = state.key.name();
        out.size = items_.size();
        out.bucket_count = state.layout.bucket_count();
        out.actual_sizes = state.counts;
        out.expected_sizes = state.layout.expected_sizes();
        for (std::size_t b = 0; b < out.bucket_count; ++b) {
            if (state.counts[b] == 0) {
                ++out.empty_buckets;
            }
            if (state.has_order && !state.order_stale && state.sorted_len[b] == state.counts[b]) {
                ++out.sorted_buckets;
            }
            out.max_skew = std::max(out.max_skew, static_cast<double>(state.counts[b]) /
                                                      static_cast<double>(skew_floor(state, b)));
        }
        const std::size_t page = std::min(plan_.page_size(), items_.size());
        for (std::size_t b = 0; b < out.bucket_count && out.first_page_cost_ascending < page; ++b) {
            out.first_page_cost_ascending += state.counts[b];
        }
        for (std::size_t b = out.bucket_count; b-- > 0 && out.first_page_cost_descending < page;) {
            out.first_page_cost_descending += state.counts[b];
        }
        out.needs_rebalance = is_stale(state);
        return out;
    }

    std::vector<std::size_t> estimate_bucket_sizes(std::string_view key, const BucketPlan& plan) const override {
        const KeyState& state = states_[require_state(key)];
        return BucketLayout<T>::estimate(state.key, items_, state.keys, plan).expected_sizes();
    }

    bool needs_rebalance(std::string_view key) const override { return is_stale(states_[require_state(key)]); }

    void rebalance(std::string_view key) override { relayout(states_[require_state(key)]); }

    void rebalance_all() override {
        for (KeyState& state : states_) {
            relayout(state);
        }
    }

    const BucketPlan& plan() const override { return plan_; }

    void set_plan(BucketPlan plan) override {
        plan_ = std::move(plan);
        rebalance_all();
    }

    // --- changing the collection ---------------------------------------------

    void add(T value) override {
        append(std::move(value));
    }

    void add_all(std::vector<T> values) override {
        items_.reserve(items_.size() + values.size());
        for (KeyState& state : states_) {
            if (state.key.has_key()) {
                state.keys.reserve(items_.capacity());
            }
            state.buckets.reserve(items_.capacity());
        }
        for (T& value : values) {
            append(std::move(value));
        }
        for (KeyState& state : states_) {
            maybe_auto_rebalance(state);
        }
    }

    std::size_t remove_if(const Predicate& match) override {
        const std::size_t n = items_.size();
        std::vector<Slot> remap(n);
        std::size_t kept = 0;
        for (std::size_t slot = 0; slot < n; ++slot) {
            if (match(items_[slot])) {
                remap[slot] = removed_slot;
            } else {
                remap[slot] = static_cast<Slot>(kept++);
            }
        }
        const std::size_t removed = n - kept;
        if (removed == 0) {
            return 0;
        }
        for (KeyState& state : states_) {
            if (state.has_order && state.order_stale) {
                regroup(state);
            }
        }
        for (std::size_t slot = 0; slot < n; ++slot) {
            if (remap[slot] != removed_slot && remap[slot] != slot) {
                items_[remap[slot]] = std::move(items_[slot]);
            }
        }
        items_.resize(kept);
        for (KeyState& state : states_) {
            compact(state, remap, kept);
        }
        return removed;
    }

    std::size_t update_if(const Predicate& match, const Mutator& mutate = {}) override {
        std::size_t updated = 0;
        for (std::size_t slot = 0; slot < items_.size(); ++slot) {
            if (!match(items_[slot])) {
                continue;
            }
            if (mutate) {
                mutate(items_[slot]);
            }
            for (KeyState& state : states_) {
                rekey(state, slot);
            }
            ++updated;
        }
        for (KeyState& state : states_) {
            if (state.splitter_changed) {
                relayout(state);
            }
        }
        return updated;
    }

    void clear() override {
        items_.clear();
        for (KeyState& state : states_) {
            state.keys.clear();
            state.buckets.clear();
            relayout(state);
        }
    }

private:
    using Slot = std::uint32_t;
    using BucketId = std::uint16_t;
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();
    static constexpr Slot removed_slot = std::numeric_limits<Slot>::max();

    /// Everything the index keeps per sort key. Slots are positions in items_.
    struct KeyState {
        explicit KeyState(SortKey<T> k) : key(std::move(k)) {}

        SortKey<T> key;
        std::vector<std::uint64_t> keys;  ///< normalized key per slot (keyed sort keys only)
        std::vector<BucketId> buckets;    ///< bucket id per slot
        BucketLayout<T> layout;
        std::vector<std::size_t> counts;  ///< elements per bucket
        std::size_t churn = 0;            ///< adds, removals and updates since the layout was made
        std::size_t stale_checked_at = npos;  ///< churn value of the last automatic staleness check

        // Layouts may split on copies of elements (see BucketLayout). When T
        // is a handle (pointer, id) the copy aliases the element, so changing
        // or removing a splitter element invalidates the layout.
        std::vector<Slot> splitter_slots;  ///< sorted slots the splitters were taken from
        bool splitter_changed = false;

        // Cached grouping by bucket; built on first use, kept across sort_by calls.
        bool has_order = false;
        bool order_stale = false;           ///< adds or updates are not placed yet
        std::vector<Slot> order;            ///< slots grouped by bucket, ascending bucket order
        std::vector<std::size_t> start;     ///< bucket b occupies order[start[b], start[b+1])
        std::vector<std::size_t> sorted_len;  ///< length of each bucket's sorted prefix
        std::size_t ordered_slots = 0;      ///< slots below this are present in `order`
        std::vector<bool> moved;            ///< slot was updated and must be re-placed
    };

    std::size_t find_state(std::string_view name) const {
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (states_[i].key.name() == name) {
                return i;
            }
        }
        return npos;
    }

    std::size_t require_state(std::string_view name) const {
        const std::size_t index = find_state(name);
        if (index == npos) {
            throw std::invalid_argument("BucketIndex: unknown sort key '" + std::string(name) + "'");
        }
        return index;
    }

    std::size_t to_ascending(std::size_t position) const {
        return direction_ == Direction::ascending ? position : items_.size() - 1 - position;
    }

    static std::size_t bucket_at(const KeyState& state, std::size_t ascending_position) {
        const auto it = std::upper_bound(state.start.begin(), state.start.end(), ascending_position);
        return static_cast<std::size_t>(it - state.start.begin()) - 1;
    }

    std::size_t skew_floor(const KeyState& state, std::size_t bucket) const {
        return std::max<std::size_t>({state.layout.expected_sizes()[bucket], plan_.page_size(), 1});
    }

    bool is_stale(const KeyState& state) const {
        const std::size_t n = items_.size();
        if (n > 2 * std::max(state.layout.estimated_from(), plan_.page_size())) {
            return true;
        }
        for (std::size_t b = 0; b < state.counts.size(); ++b) {
            if (static_cast<double>(state.counts[b]) > plan_.rebalance_skew() * static_cast<double>(skew_floor(state, b))) {
                return true;
            }
        }
        return false;
    }

    /// Automatic rebalancing waits until enough has changed to pay for itself.
    void maybe_auto_rebalance(KeyState& state) {
        if (!plan_.auto_rebalance() || state.churn == state.stale_checked_at) {
            return;
        }
        state.stale_checked_at = state.churn;
        if (state.churn >= std::max(plan_.page_size(), state.layout.estimated_from() / 8) && is_stale(state)) {
            relayout(state);
        }
    }

    /// Makes the grouping of `state` current. Called before every sorted read.
    void prepare(KeyState& state) {
        maybe_auto_rebalance(state);
        if (!state.has_order) {
            build_order(state);
        } else if (state.order_stale) {
            regroup(state);
        }
    }

    bool is_splitter(const KeyState& state, std::size_t slot) const {
        return std::binary_search(state.splitter_slots.begin(), state.splitter_slots.end(), static_cast<Slot>(slot));
    }

    void relayout(KeyState& state) {
        state.layout = BucketLayout<T>::estimate(state.key, items_, state.keys, plan_);
        state.splitter_slots.assign(state.layout.splitter_positions().begin(), state.layout.splitter_positions().end());
        std::sort(state.splitter_slots.begin(), state.splitter_slots.end());
        state.splitter_changed = false;
        state.counts.assign(state.layout.bucket_count(), 0);
        state.buckets.resize(items_.size());
        for (std::size_t slot = 0; slot < items_.size(); ++slot) {
            const std::size_t b = state.key.has_key()
                                      ? state.layout.bucket_of(items_[slot], state.keys[slot], state.key)
                                      : state.layout.bucket_of(items_[slot], state.key);
            state.buckets[slot] = static_cast<BucketId>(b);
            ++state.counts[b];
        }
        state.churn = 0;
        state.stale_checked_at = npos;
        state.has_order = false;
        state.order_stale = false;
        state.order.clear();
        state.start.clear();
        state.sorted_len.clear();
        state.moved.clear();
        state.ordered_slots = 0;
    }

    void append(T value) {
        const std::size_t slot = items_.size();
        if (slot >= removed_slot) {
            throw std::length_error("BucketIndex: too many elements");
        }
        items_.push_back(std::move(value));
        const T& item = items_.back();
        for (KeyState& state : states_) {
            std::size_t b = 0;
            if (state.key.has_key()) {
                const std::uint64_t k = state.key.key(item);
                state.keys.push_back(k);
                b = state.layout.bucket_of(item, k, state.key);
            } else {
                b = state.layout.bucket_of(item, state.key);
            }
            state.buckets.push_back(static_cast<BucketId>(b));
            ++state.counts[b];
            ++state.churn;
            if (state.has_order) {
                state.order_stale = true;
            }
        }
    }

    void rekey(KeyState& state, std::size_t slot) {
        std::size_t b = 0;
        if (state.key.has_key()) {
            state.keys[slot] = state.key.key(items_[slot]);
            b = state.layout.bucket_of(items_[slot], state.keys[slot], state.key);
        } else {
            b = state.layout.bucket_of(items_[slot], state.key);
        }
        --state.counts[state.buckets[slot]];
        state.buckets[slot] = static_cast<BucketId>(b);
        ++state.counts[b];
        ++state.churn;
        if (is_splitter(state, slot)) {
            state.splitter_changed = true;
        }
        if (state.has_order && slot < state.ordered_slots) {
            state.moved[slot] = true;
            state.order_stale = true;
        }
    }

    /// Counting sort of all slots by bucket id. Nothing is sorted yet.
    void build_order(KeyState& state) {
        const std::size_t n = items_.size();
        const std::size_t k = state.layout.bucket_count();
        state.start.assign(k + 1, 0);
        for (std::size_t b = 0; b < k; ++b) {
            state.start[b + 1] = state.start[b] + state.counts[b];
        }
        std::vector<std::size_t> fill(state.start.begin(), state.start.end() - 1);
        state.order.assign(n, 0);
        for (std::size_t slot = 0; slot < n; ++slot) {
            state.order[fill[state.buckets[slot]]++] = static_cast<Slot>(slot);
        }
        state.sorted_len.assign(k, 0);
        state.moved.assign(n, false);
        state.ordered_slots = n;
        state.has_order = true;
        state.order_stale = false;
    }

    /// Places added and updated slots without losing the sorted prefixes.
    ///
    /// Slots that stayed keep their relative order at the front of their
    /// bucket, so the part of a sorted prefix that survived is still sorted.
    /// Added and updated slots go to the (unsorted) back of their bucket.
    void regroup(KeyState& state) {
        const std::size_t n = items_.size();
        const std::size_t k = state.layout.bucket_count();
        std::vector<std::size_t> start(k + 1, 0);
        for (std::size_t b = 0; b < k; ++b) {
            start[b + 1] = start[b] + state.counts[b];
        }
        std::vector<std::size_t> fill(start.begin(), start.end() - 1);
        std::vector<std::size_t> sorted_len(k, 0);
        std::vector<Slot> order(n);
        std::vector<Slot> deferred;
        for (std::size_t b = 0; b < k; ++b) {
            const std::size_t prefix_end = state.start[b] + state.sorted_len[b];
            for (std::size_t i = state.start[b]; i < state.start[b + 1]; ++i) {
                const Slot slot = state.order[i];
                if (state.moved[slot]) {
                    deferred.push_back(slot);
                    continue;
                }
                order[fill[b]++] = slot;
                if (i < prefix_end) {
                    ++sorted_len[b];
                }
            }
        }
        for (Slot slot : deferred) {
            order[fill[state.buckets[slot]]++] = slot;
        }
        for (std::size_t slot = state.ordered_slots; slot < n; ++slot) {
            order[fill[state.buckets[slot]]++] = static_cast<Slot>(slot);
        }
        state.order = std::move(order);
        state.start = std::move(start);
        state.sorted_len = std::move(sorted_len);
        state.moved.assign(n, false);
        state.ordered_slots = n;
        state.order_stale = false;
    }

    /// Applies a removal to one sort key. The grouping is filtered in place,
    /// which keeps every bucket's surviving sorted prefix sorted.
    void compact(KeyState& state, const std::vector<Slot>& remap, std::size_t kept) {
        const std::size_t n = remap.size();
        std::size_t write = 0;
        for (std::size_t slot = 0; slot < n; ++slot) {
            if (remap[slot] == removed_slot) {
                --state.counts[state.buckets[slot]];
                continue;
            }
            if (state.key.has_key()) {
                state.keys[write] = state.keys[slot];
            }
            state.buckets[write] = state.buckets[slot];
            ++write;
        }
        state.keys.resize(state.key.has_key() ? kept : 0);
        state.buckets.resize(kept);
        state.churn += n - kept;
        for (Slot& slot : state.splitter_slots) {
            if (remap[slot] == removed_slot) {
                relayout(state);
                return;
            }
            slot = remap[slot];
        }
        if (!state.has_order) {
            return;
        }
        const std::size_t k = state.layout.bucket_count();
        std::size_t out = 0;
        std::vector<std::size_t> start(k + 1, 0);
        for (std::size_t b = 0; b < k; ++b) {
            const std::size_t prefix_end = state.start[b] + state.sorted_len[b];
            std::size_t sorted = 0;
            for (std::size_t i = state.start[b]; i < state.start[b + 1]; ++i) {
                const Slot mapped = remap[state.order[i]];
                if (mapped == removed_slot) {
                    continue;
                }
                state.order[out++] = mapped;
                if (i < prefix_end) {
                    ++sorted;
                }
            }
            state.sorted_len[b] = sorted;
            start[b + 1] = out;
        }
        state.order.resize(kept);
        state.start = std::move(start);
        state.moved.assign(kept, false);
        state.ordered_slots = kept;
    }

    /// Sorts the unsorted tail of a bucket and merges it into the sorted prefix.
    /// Order: key, tie-breaker, insertion order (slot numbers keep insertion order).
    void sort_bucket(KeyState& state, std::size_t bucket) {
        const std::size_t length = state.start[bucket + 1] - state.start[bucket];
        const std::size_t sorted = state.sorted_len[bucket];
        if (sorted == length) {
            return;
        }
        const auto first = state.order.begin() + static_cast<std::ptrdiff_t>(state.start[bucket]);
        const auto middle = first + static_cast<std::ptrdiff_t>(sorted);
        const auto last = first + static_cast<std::ptrdiff_t>(length);
        if (state.key.has_key()) {
            const std::vector<std::uint64_t>& keys = state.keys;
            const SortKey<T>& key = state.key;
            auto less = [&](Slot a, Slot b) {
                if (keys[a] != keys[b]) {
                    return keys[a] < keys[b];
                }
                const int c = key.tie_break(items_[a], items_[b]);
                return c != 0 ? c < 0 : a < b;
            };
            std::sort(middle, last, less);
            std::inplace_merge(first, middle, last, less);
        } else {
            const SortKey<T>& key = state.key;
            auto less = [&](Slot a, Slot b) {
                const int c = key.compare(items_[a], items_[b]);
                return c != 0 ? c < 0 : a < b;
            };
            std::sort(middle, last, less);
            std::inplace_merge(first, middle, last, less);
        }
        state.sorted_len[bucket] = length;
    }

    BucketPlan plan_;
    std::vector<T> items_;
    std::vector<KeyState> states_;
    std::size_t active_ = npos;
    Direction direction_ = Direction::ascending;
};

}  // namespace speedup::bucketization
