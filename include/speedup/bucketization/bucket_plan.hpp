#pragma once
/// @file bucket_plan.hpp
/// How many buckets to cut and how large each one should be.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace speedup::bucketization {

/// Target bucket sizes. Buckets do not have to be equal: the default plan
/// makes the buckets at both ends one page long (what a view shows first, in
/// ascending and in descending order) and lets them grow towards the middle,
/// so showing the first page only needs a page-sized sort.
///
/// Plans are immutable values; the `with_*` methods return modified copies.
class BucketPlan {
public:
    enum class Kind {
        geometric,   ///< page, page*g, page*g^2, ... (from both ends when symmetric)
        first_page,  ///< [page, remainder] or [page, remainder, page] when symmetric
        uniform,     ///< N equal buckets
        sizes        ///< caller-provided sizes; the last bucket absorbs the remainder
    };

    /// Bucket ids are stored as 16-bit integers.
    static constexpr std::size_t max_bucket_limit = 65535;
    static constexpr std::size_t max_sample_size = std::size_t{1} << 20;
    static constexpr std::size_t default_page_size = 64;

    /// geometric(64, 2.0): symmetric, at most 64 buckets.
    static BucketPlan defaults() { return geometric(default_page_size, 2.0); }

    /// The first bucket holds what fits on screen, the rest is the remainder.
    static BucketPlan first_page(std::size_t page_size) {
        BucketPlan plan(Kind::first_page);
        plan.page_size_ = require_positive(page_size, "page_size");
        return plan;
    }

    /// Buckets start one page long and grow by `growth` (>= 1) towards the middle.
    static BucketPlan geometric(std::size_t page_size, double growth) {
        if (!(growth >= 1.0) || !std::isfinite(growth)) {
            throw std::invalid_argument("BucketPlan: growth must be a finite number >= 1");
        }
        BucketPlan plan(Kind::geometric);
        plan.page_size_ = require_positive(page_size, "page_size");
        plan.growth_ = growth;
        return plan;
    }

    /// `buckets` buckets of (nearly) equal size.
    static BucketPlan uniform(std::size_t buckets) {
        BucketPlan plan(Kind::uniform);
        plan.max_buckets_ = std::min(require_positive(buckets, "buckets"), max_bucket_limit);
        return plan;
    }

    /// Explicit target sizes, e.g. {50, 200, 1000}. The last bucket absorbs
    /// whatever is left. The first size doubles as the page size.
    static BucketPlan of_sizes(std::vector<std::size_t> sizes) {
        if (sizes.empty()) {
            throw std::invalid_argument("BucketPlan: sizes must not be empty");
        }
        for (std::size_t size : sizes) {
            require_positive(size, "sizes[i]");
        }
        BucketPlan plan(Kind::sizes);
        plan.page_size_ = sizes.front();
        plan.sizes_ = std::move(sizes);
        return plan;
    }

    BucketPlan with_page_size(std::size_t page_size) const {
        BucketPlan copy = *this;
        copy.page_size_ = require_positive(page_size, "page_size");
        return copy;
    }

    BucketPlan with_max_buckets(std::size_t max_buckets) const {
        BucketPlan copy = *this;
        copy.max_buckets_ = std::min(require_positive(max_buckets, "max_buckets"), max_bucket_limit);
        return copy;
    }

    /// Symmetric plans keep small buckets at both ends so descending order is
    /// as cheap as ascending. Ignored by uniform and explicit-size plans.
    BucketPlan with_symmetric(bool symmetric) const {
        BucketPlan copy = *this;
        copy.symmetric_ = symmetric;
        return copy;
    }

    /// How many elements are sampled to estimate the key distribution.
    BucketPlan with_sample_size(std::size_t sample_size) const {
        BucketPlan copy = *this;
        copy.sample_size_ = std::min(require_positive(sample_size, "sample_size"), max_sample_size);
        return copy;
    }

    /// Seed of the deterministic sampler.
    BucketPlan with_seed(std::uint64_t seed) const {
        BucketPlan copy = *this;
        copy.seed_ = seed;
        return copy;
    }

    /// A bucket holding more than `skew` times its expected size (expected
    /// sizes below one page count as one page) asks for a rebalance.
    BucketPlan with_rebalance_skew(double skew) const {
        if (!(skew > 1.0)) {
            throw std::invalid_argument("BucketPlan: rebalance skew must be > 1");
        }
        BucketPlan copy = *this;
        copy.rebalance_skew_ = skew;
        return copy;
    }

    /// When enabled (default), layouts that went stale are re-estimated
    /// automatically after bulk inserts and before sorted reads.
    BucketPlan with_auto_rebalance(bool enabled) const {
        BucketPlan copy = *this;
        copy.auto_rebalance_ = enabled;
        return copy;
    }

    Kind kind() const noexcept { return kind_; }
    std::size_t page_size() const noexcept { return page_size_; }
    double growth() const noexcept { return growth_; }
    std::size_t max_buckets() const noexcept { return max_buckets_; }
    bool symmetric() const noexcept { return symmetric_; }
    std::size_t sample_size() const noexcept { return sample_size_; }
    std::uint64_t seed() const noexcept { return seed_; }
    double rebalance_skew() const noexcept { return rebalance_skew_; }
    bool auto_rebalance() const noexcept { return auto_rebalance_; }
    const std::vector<std::size_t>& sizes() const noexcept { return sizes_; }

    /// Target size of every bucket for `n` elements, in ascending key order.
    /// The sizes add up to `n`; there is always at least one bucket.
    std::vector<std::size_t> target_sizes(std::size_t n) const {
        if (n == 0) {
            return {0};
        }
        switch (kind_) {
            case Kind::uniform: return uniform_sizes(n);
            case Kind::sizes: return explicit_sizes(n);
            case Kind::first_page: return first_page_sizes(n);
            case Kind::geometric: break;
        }
        return geometric_sizes(n);
    }

private:
    explicit BucketPlan(Kind kind) : kind_(kind) {}

    static std::size_t require_positive(std::size_t value, const char* what) {
        if (value == 0) {
            throw std::invalid_argument(std::string("BucketPlan: ") + what + " must be > 0");
        }
        return value;
    }

    std::vector<std::size_t> uniform_sizes(std::size_t n) const {
        const std::size_t count = std::min(max_buckets_, n);
        std::vector<std::size_t> out(count, n / count);
        for (std::size_t i = 0; i < n % count; ++i) {
            ++out[i];
        }
        return out;
    }

    std::vector<std::size_t> explicit_sizes(std::size_t n) const {
        std::vector<std::size_t> out;
        std::size_t remaining = n;
        for (std::size_t size : sizes_) {
            if (remaining == 0 || out.size() + 1 == max_buckets_) {
                break;
            }
            const std::size_t take = std::min(size, remaining);
            out.push_back(take);
            remaining -= take;
        }
        if (remaining > 0) {
            out.push_back(remaining);
        }
        return out;
    }

    std::vector<std::size_t> first_page_sizes(std::size_t n) const {
        const std::size_t page = std::min(page_size_, n);
        if (max_buckets_ == 1 || page == n) {
            return {n};
        }
        if (symmetric_ && max_buckets_ >= 3 && n > 2 * page) {
            return {page, n - 2 * page, page};
        }
        return {page, n - page};
    }

    std::vector<std::size_t> geometric_sizes(std::size_t n) const {
        std::vector<std::size_t> front;
        std::vector<std::size_t> back;
        std::size_t remaining = n;
        std::size_t budget = max_buckets_;
        double size = static_cast<double>(page_size_);
        while (remaining > 0 && budget > 1) {
            const auto step = static_cast<std::size_t>(std::ceil(size));
            std::size_t take = std::min(step, remaining);
            front.push_back(take);
            remaining -= take;
            --budget;
            if (symmetric_) {
                if (remaining == 0 || budget <= 1) {
                    break;
                }
                take = std::min(step, remaining);
                back.push_back(take);
                remaining -= take;
                --budget;
            }
            size = std::min(size * growth_, static_cast<double>(n));
        }
        if (remaining > 0) {
            front.push_back(remaining);
        }
        front.insert(front.end(), back.rbegin(), back.rend());
        return front;
    }

    Kind kind_;
    std::size_t page_size_ = default_page_size;
    double growth_ = 2.0;
    std::size_t max_buckets_ = 64;
    bool symmetric_ = true;
    std::size_t sample_size_ = 2048;
    std::uint64_t seed_ = 0x5EEDB0C4E7ULL;
    double rebalance_skew_ = 4.0;
    bool auto_rebalance_ = true;
    std::vector<std::size_t> sizes_;
};

}  // namespace speedup::bucketization
