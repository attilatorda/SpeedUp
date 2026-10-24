#pragma once
/// @file bucket_layout.hpp
/// Bucket boundaries estimated from the data distribution of one sort key.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "speedup/bucketization/bucket_plan.hpp"
#include "speedup/bucketization/sort_key.hpp"

namespace speedup::bucketization {

namespace detail {

/// SplitMix64: tiny deterministic generator shared by every language port,
/// so the same data, plan and seed produce the same layout everywhere.
struct SplitMix64 {
    std::uint64_t state;

    std::uint64_t next() noexcept {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

/// Jittered systematic sample: one random index from each of `s` equal
/// strides. Every index when `n <= sample_size`.
inline std::vector<std::size_t> sample_indices(std::size_t n, std::size_t sample_size, std::uint64_t seed) {
    const std::size_t s = std::min(n, sample_size);
    std::vector<std::size_t> out(s);
    if (s == n) {
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = i;
        }
        return out;
    }
    SplitMix64 rng{seed};
    for (std::size_t j = 0; j < s; ++j) {
        const std::size_t lo = j * n / s;
        const std::size_t hi = (j + 1) * n / s;
        out[j] = lo + static_cast<std::size_t>(rng.next() % (hi - lo));
    }
    return out;
}

/// Turns desired split positions in a sorted sample into actual ones.
///
/// A split must not fall inside a run of equal values (equal elements share a
/// bucket), so each desired position `q` moves to the start or the end of its
/// run, whichever is closer. Positions are strictly increasing and in (0, s).
template <class RunStart, class RunEnd>
std::vector<std::size_t> choose_split_positions(const std::vector<std::size_t>& desired, std::size_t s,
                                                RunStart run_start, RunEnd run_end) {
    std::vector<std::size_t> out;
    std::size_t previous = 0;
    for (std::size_t q : desired) {
        const std::size_t lo = run_start(q);
        const std::size_t hi = run_end(q);
        std::size_t best = 0;
        if (lo > previous) {
            best = lo;
        }
        if (hi < s && hi > previous && (best == 0 || hi - q < q - lo)) {
            best = hi;
        }
        if (best != 0) {
            out.push_back(best);
            previous = best;
        }
    }
    return out;
}

}  // namespace detail

/// Where the bucket boundaries of one sort key are, and how many elements
/// each bucket is expected to hold. This is the index's model of the data
/// distribution.
///
/// The layout is a strictly increasing list of split points; bucket `b` holds
/// the elements `x` with `split[b-1] <= x < split[b]`. Bucket ids therefore
/// follow ascending order, and elements that compare equal always share a
/// bucket.
///
/// - Comparator-only sort keys split on sampled elements (`splitters()`).
/// - Keyed sort keys split on normalized keys (`boundaries()`). When a split
///   falls inside a run of equal keys (names sharing their first 8 bytes, such
///   as "IMG_2024..."), the split point also stores the sampled element and
///   elements with that key are placed with the tie-breaker. This keeps
///   buckets page-sized when many keys collide.
template <class T>
class BucketLayout {
public:
    /// A single bucket holding everything.
    BucketLayout() : expected_(1, 0) {}

    std::size_t bucket_count() const noexcept { return expected_.size(); }

    /// Key of every split point, non-decreasing (keyed sort keys only).
    const std::vector<std::uint64_t>& boundaries() const noexcept { return boundaries_; }

    /// Elements stored with split points: every split point of a
    /// comparator-only key, and the split points of a keyed key that fall
    /// inside a run of equal keys.
    const std::vector<T>& splitters() const noexcept { return splitters_; }

    /// Where each splitter was taken from: positions in the `items` passed to estimate().
    const std::vector<std::size_t>& splitter_positions() const noexcept { return splitter_positions_; }

    /// Estimated number of elements per bucket when the layout was made.
    const std::vector<std::size_t>& expected_sizes() const noexcept { return expected_; }

    /// Number of elements the layout was estimated from.
    std::size_t estimated_from() const noexcept { return estimated_from_; }

    /// Lowest bucket an element with normalized key `key` can be in. This is
    /// its bucket unless a split point with the same key stores an element.
    std::size_t bucket_of_key(std::uint64_t key) const noexcept {
        std::size_t lo = 0;
        std::size_t hi = boundaries_.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            if (key < boundaries_[mid] || (key == boundaries_[mid] && split_element_[mid] != no_element)) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    /// Bucket of an element under `key` (the sort key the layout was made for).
    std::size_t bucket_of(const T& value, const SortKey<T>& key) const {
        if (key.has_key()) {
            return bucket_of(value, key.key(value), key);
        }
        auto it = std::upper_bound(splitters_.begin(), splitters_.end(), value,
                                   [&key](const T& v, const T& split) { return key.compare(v, split) < 0; });
        return static_cast<std::size_t>(it - splitters_.begin());
    }

    /// Bucket of an element of a keyed sort key whose normalized key
    /// `value_key` is already known.
    std::size_t bucket_of(const T& value, std::uint64_t value_key, const SortKey<T>& key) const {
        if (splitters_.empty()) {
            return static_cast<std::size_t>(std::upper_bound(boundaries_.begin(), boundaries_.end(), value_key) -
                                            boundaries_.begin());
        }
        std::size_t lo = 0;
        std::size_t hi = boundaries_.size();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            bool below = value_key < boundaries_[mid];
            if (value_key == boundaries_[mid] && split_element_[mid] != no_element) {
                below = key.tie_break(value, splitters_[split_element_[mid]]) < 0;
            }
            if (below) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    /// Estimates a layout for `items` under `key`.
    ///
    /// `keys` holds the normalized key of every item for keyed sort keys and
    /// is ignored otherwise. Cost: sorting a sample of at most
    /// `plan.sample_size()` elements.
    static BucketLayout estimate(const SortKey<T>& key, const std::vector<T>& items,
                                 const std::vector<std::uint64_t>& keys, const BucketPlan& plan) {
        const std::size_t n = items.size();
        BucketLayout layout;
        layout.estimated_from_ = n;
        const std::vector<std::size_t> targets = plan.target_sizes(n);
        if (targets.size() <= 1) {
            layout.expected_ = {n};
            return layout;
        }

        const std::vector<std::size_t> sample = detail::sample_indices(n, plan.sample_size(), plan.seed());
        const std::size_t s = sample.size();
        std::vector<std::size_t> desired;
        std::size_t cumulative = 0;
        for (std::size_t j = 0; j + 1 < targets.size(); ++j) {
            cumulative += targets[j];
            desired.push_back(cumulative * s / n);
        }

        // Sort the sample by the exact order: key (if any), then tie-breaker.
        const bool keyed = key.has_key();
        auto less = [&](std::size_t a, std::size_t b) {
            if (keyed && keys[a] != keys[b]) {
                return keys[a] < keys[b];
            }
            return (keyed ? key.tie_break(items[a], items[b]) : key.compare(items[a], items[b])) < 0;
        };
        std::vector<std::size_t> sorted = sample;
        std::sort(sorted.begin(), sorted.end(), less);
        std::vector<std::size_t> positions = detail::choose_split_positions(
            desired, s,
            [&](std::size_t q) {
                return static_cast<std::size_t>(std::lower_bound(sorted.begin(), sorted.end(), sorted[q], less) -
                                                sorted.begin());
            },
            [&](std::size_t q) {
                return static_cast<std::size_t>(std::upper_bound(sorted.begin(), sorted.end(), sorted[q], less) -
                                                sorted.begin());
            });

        for (std::size_t p : positions) {
            const std::size_t item = sorted[p];
            // A keyed split at the start of a key run needs no element.
            const bool needs_element = !keyed || keys[sorted[p - 1]] == keys[item];
            if (keyed) {
                layout.boundaries_.push_back(keys[item]);
                layout.split_element_.push_back(needs_element ? layout.splitters_.size() : no_element);
            }
            if (needs_element) {
                layout.splitters_.push_back(items[item]);
                layout.splitter_positions_.push_back(item);
            }
        }

        layout.expected_.clear();
        std::size_t previous = 0;
        positions.push_back(s);
        for (std::size_t p : positions) {
            layout.expected_.push_back(((p - previous) * n + s / 2) / s);
            previous = p;
        }
        return layout;
    }

private:
    static constexpr std::size_t no_element = static_cast<std::size_t>(-1);

    std::vector<std::uint64_t> boundaries_;
    std::vector<std::size_t> split_element_;  ///< per keyed split point: index into splitters_ or no_element
    std::vector<T> splitters_;
    std::vector<std::size_t> splitter_positions_;
    std::vector<std::size_t> expected_;
    std::size_t estimated_from_ = 0;
};

}  // namespace speedup::bucketization
