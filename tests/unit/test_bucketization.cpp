// Tests for speedup/bucketization.hpp. Checks run in every build type
// (assert() would vanish under NDEBUG).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "speedup/bucketization.hpp"

namespace bz = speedup::bucketization;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

template <class F>
bool throws(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

struct Rec {
    int id = 0;
    std::string name;
    std::int64_t size = 0;
    double score = 0.0;
    bool operator==(const Rec& o) const { return id == o.id; }
};

std::string random_name(std::mt19937_64& rng) {
    static const char* prefixes[] = {"IMG_", "IMG_2024", "doc", "Doc", "notes", "", "a", "zz", "\xc3\xa9t\xc3\xa9"};
    std::uniform_int_distribution<int> pick(0, 8);
    std::uniform_int_distribution<int> len(0, 6);
    std::uniform_int_distribution<int> ch(0, 61);
    std::string s = prefixes[pick(rng)];
    for (int i = len(rng); i > 0; --i) {
        const int c = ch(rng);
        s.push_back(static_cast<char>(c < 10 ? '0' + c : c < 36 ? 'a' + c - 10 : 'A' + c - 36));
    }
    return s;
}

Rec random_rec(std::mt19937_64& rng, int id) {
    Rec r;
    r.id = id;
    r.name = random_name(rng);
    std::uniform_int_distribution<int> kind(0, 3);
    std::uniform_int_distribution<std::int64_t> big(-1000000, 1000000);
    switch (kind(rng)) {
        case 0: r.size = 0; break;                 // heavy duplicate
        case 1: r.size = big(rng) % 10; break;     // few distinct values
        default: r.size = big(rng); break;
    }
    std::normal_distribution<double> normal(0.0, 100.0);
    r.score = normal(rng);
    return r;
}

std::vector<bz::SortKey<Rec>> make_keys() {
    std::vector<bz::SortKey<Rec>> keys;
    keys.push_back(bz::SortKey<Rec>::of("name", &Rec::name));
    keys.push_back(bz::SortKey<Rec>::of("size", [](const Rec& r) { return r.size; }));
    keys.push_back(bz::SortKey<Rec>::of("score", &Rec::score));
    // Comparator-only: by id modulo 97 descending, then name.
    keys.push_back(bz::SortKey<Rec>::of_comparator("mod97", [](const Rec& a, const Rec& b) {
        const int x = a.id % 97;
        const int y = b.id % 97;
        if (x != y) {
            return x > y ? -1 : 1;
        }
        const int c = a.name.compare(b.name);
        return (c > 0) - (c < 0);
    }));
    // Keyed with a tie-breaker: size, then score.
    keys.push_back(bz::SortKey<Rec>::of_key(
        "size_then_score", [](const Rec& r) { return bz::keys::from_signed(r.size); },
        [](const Rec& a, const Rec& b) { return (a.score > b.score) - (a.score < b.score); }));
    // Groups ("folders first" in both directions), keyed and comparator-only.
    keys.push_back(bz::SortKey<Rec>::of("name_folders_first", &Rec::name)
                       .with_groups(2, [](const Rec& r) { return r.id % 3 == 0 ? std::size_t{0} : std::size_t{1}; }));
    keys.push_back(bz::SortKey<Rec>::of_comparator("score_in_3_groups",
                                                  [](const Rec& a, const Rec& b) {
                                                      return (a.score > b.score) - (a.score < b.score);
                                                  })
                       .with_groups(3, [](const Rec& r) { return static_cast<std::size_t>(r.id % 3); }));
    return keys;
}

/// Reference order: stable sort of the elements in insertion order.
/// Descending reverses the order inside each group (the whole list when ungrouped).
std::vector<int> reference_ids(const std::vector<Rec>& items, const bz::SortKey<Rec>& key, bz::Direction dir) {
    std::vector<Rec> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const Rec& a, const Rec& b) { return key.compare(a, b) < 0; });
    if (dir == bz::Direction::descending) {
        for (auto first = sorted.begin(); first != sorted.end();) {
            const std::size_t g = key.group(*first);
            auto last = std::find_if(first, sorted.end(), [&](const Rec& r) { return key.group(r) != g; });
            std::reverse(first, last);
            first = last;
        }
    }
    std::vector<int> ids;
    for (const Rec& r : sorted) {
        ids.push_back(r.id);
    }
    return ids;
}

void test_keys() {
    std::mt19937_64 rng(1);
    std::uniform_int_distribution<std::int64_t> any(std::numeric_limits<std::int64_t>::min(),
                                                    std::numeric_limits<std::int64_t>::max());
    for (int i = 0; i < 10000; ++i) {
        const std::int64_t a = any(rng) >> (i % 60);
        const std::int64_t b = any(rng) >> (i % 61);
        CHECK((a < b) == (bz::keys::from_signed(a) < bz::keys::from_signed(b)));
    }

    std::vector<double> doubles = {-std::numeric_limits<double>::infinity(), -1e300, -1.5, -1e-300, -0.0, 0.0,
                                   1e-300, 1.0, 2.0, 1e300, std::numeric_limits<double>::infinity()};
    for (std::size_t i = 0; i + 1 < doubles.size(); ++i) {
        CHECK(bz::keys::from_double(doubles[i]) < bz::keys::from_double(doubles[i + 1]));
    }
    CHECK(bz::keys::from_double(std::nan("")) == bz::keys::canonical_nan);
    CHECK(bz::keys::from_double(-std::nan("")) == bz::keys::canonical_nan);
    CHECK(bz::keys::from_double(std::numeric_limits<double>::infinity()) < bz::keys::canonical_nan);
    CHECK(bz::keys::from_double(1.0) == 0xBFF0000000000000ULL);

    // String prefix keys never contradict std::string order.
    for (int i = 0; i < 20000; ++i) {
        const std::string a = random_name(rng);
        const std::string b = random_name(rng);
        const std::uint64_t ka = bz::keys::from_string(a);
        const std::uint64_t kb = bz::keys::from_string(b);
        if (ka < kb) {
            CHECK(a < b);
        }
        if (a < b) {
            CHECK(ka <= kb);
        }
    }
    CHECK(bz::keys::from_string("") == 0);
    CHECK(bz::keys::from_string("A") == 0x4100000000000000ULL);
    CHECK(bz::keys::from_string("ABCDEFGHIJ") == bz::keys::from_string("ABCDEFGH"));
    CHECK(bz::keys::from_string("\xff") > bz::keys::from_string("\x7f"));
    CHECK(bz::keys::from_string("\xc3\xa9t\xc3\xa9") == 0xC3A974C3A9000000ULL);  // same as Java fromString("été")
    CHECK(bz::keys::from_double(-2.5) == 0x3FFBFFFFFFFFFFFFULL);
    CHECK(bz::keys::from_signed(-1) == 0x7FFFFFFFFFFFFFFFULL);
    CHECK(bz::keys::reverse(1) > bz::keys::reverse(2));
    CHECK(bz::keys::normalize(true) == 1);
    CHECK(bz::keys::normalize(-1) < bz::keys::normalize(0));
    CHECK(bz::keys::normalize(std::uint64_t{5}) == 5);
}

std::size_t sum(const std::vector<std::size_t>& v) {
    std::size_t s = 0;
    for (std::size_t x : v) {
        s += x;
    }
    return s;
}

void test_plan() {
    const bz::BucketPlan geo = bz::BucketPlan::defaults();
    for (std::size_t n : {0u, 1u, 63u, 64u, 65u, 128u, 129u, 1000u, 100000u, 1000000u}) {
        const auto sizes = geo.target_sizes(n);
        CHECK(sum(sizes) == n);
        CHECK(!sizes.empty());
        CHECK(sizes.size() <= geo.max_buckets());
        if (n >= 3 * 64) {
            CHECK(sizes.front() == 64);
            CHECK(sizes.back() == 64);  // symmetric: descending first page is small too
        }
    }
    const auto big = geo.target_sizes(1000000);
    CHECK(big[1] == 128 && big[2] == 256 && big[3] == 512);
    CHECK(big[big.size() - 2] == 128 && big[big.size() - 3] == 256);

    const auto asym = geo.with_symmetric(false).target_sizes(1000);
    CHECK(asym == (std::vector<std::size_t>{64, 128, 256, 512, 40}));

    CHECK(bz::BucketPlan::first_page(50).target_sizes(1000) == (std::vector<std::size_t>{50, 900, 50}));
    CHECK(bz::BucketPlan::first_page(50).with_symmetric(false).target_sizes(1000) ==
          (std::vector<std::size_t>{50, 950}));
    CHECK(bz::BucketPlan::first_page(50).target_sizes(30) == (std::vector<std::size_t>{30}));
    CHECK(bz::BucketPlan::first_page(50).target_sizes(80) == (std::vector<std::size_t>{50, 30}));
    CHECK(bz::BucketPlan::uniform(4).target_sizes(10) == (std::vector<std::size_t>{3, 3, 2, 2}));
    CHECK(bz::BucketPlan::uniform(4).target_sizes(2) == (std::vector<std::size_t>{1, 1}));
    CHECK(bz::BucketPlan::of_sizes({10, 20}).target_sizes(100) == (std::vector<std::size_t>{10, 20, 70}));
    CHECK(bz::BucketPlan::of_sizes({10, 20, 30}).target_sizes(25) == (std::vector<std::size_t>{10, 15}));
    CHECK(bz::BucketPlan::of_sizes({10, 20, 30}).with_max_buckets(2).target_sizes(100) ==
          (std::vector<std::size_t>{10, 90}));
    CHECK(geo.with_max_buckets(1).target_sizes(500) == (std::vector<std::size_t>{500}));
    CHECK(geo.with_max_buckets(5).target_sizes(100000).size() == 5);
    CHECK(throws([] { bz::BucketPlan::geometric(0, 2.0); }));
    CHECK(throws([] { bz::BucketPlan::geometric(10, 0.5); }));
    CHECK(throws([] { bz::BucketPlan::of_sizes({}); }));
    CHECK(throws([] { bz::BucketPlan::defaults().with_rebalance_skew(1.0); }));
}

void test_layout() {
    // Exact layout (n <= sample size): expected sizes equal actual sizes.
    std::vector<Rec> items;
    std::vector<std::uint64_t> keys;
    for (int i = 0; i < 1000; ++i) {
        Rec r;
        r.id = i;
        r.size = (i * 7919) % 1000;  // a permutation of 0..999
        items.push_back(r);
        keys.push_back(bz::keys::from_signed(r.size));
    }
    const auto size_key = bz::SortKey<Rec>::of("size", &Rec::size);
    const auto plan = bz::BucketPlan::defaults();
    const auto layout = bz::BucketLayout<Rec>::estimate(size_key, items, keys, plan);
    CHECK(layout.expected_sizes() == plan.target_sizes(1000));
    CHECK(layout.boundaries().size() + 1 == layout.bucket_count());
    std::vector<std::size_t> counts(layout.bucket_count(), 0);
    for (const Rec& r : items) {
        ++counts[layout.bucket_of(r, size_key)];
    }
    CHECK(counts == layout.expected_sizes());

    // Heavy duplicates: equal keys never straddle a boundary.
    std::vector<Rec> dup;
    std::vector<std::uint64_t> dup_keys;
    for (int i = 0; i < 5000; ++i) {
        Rec r;
        r.id = i;
        r.size = i < 3000 ? 0 : i;
        dup.push_back(r);
        dup_keys.push_back(bz::keys::from_signed(r.size));
    }
    const auto dup_layout = bz::BucketLayout<Rec>::estimate(size_key, dup, dup_keys, plan);
    CHECK(dup_layout.bucket_of(dup[0], size_key) == dup_layout.bucket_of(dup[2999], size_key));
    for (std::size_t i = 1; i < dup_layout.boundaries().size(); ++i) {
        CHECK(dup_layout.boundaries()[i - 1] < dup_layout.boundaries()[i]);
    }
    CHECK(sum(dup_layout.expected_sizes()) == 5000);

    // Sampled layout is deterministic for a given seed.
    std::mt19937_64 rng(7);
    std::vector<Rec> many;
    std::vector<std::uint64_t> many_keys;
    for (int i = 0; i < 50000; ++i) {
        many.push_back(random_rec(rng, i));
        many_keys.push_back(bz::keys::from_signed(many.back().size));
    }
    const auto a = bz::BucketLayout<Rec>::estimate(size_key, many, many_keys, plan);
    const auto b = bz::BucketLayout<Rec>::estimate(size_key, many, many_keys, plan);
    CHECK(a.boundaries() == b.boundaries());
    CHECK(a.estimated_from() == 50000);

    // Golden values shared with the Java tests: every port samples and splits identically.
    CHECK(bz::detail::sample_indices(100000, 8, 42) ==
          (std::vector<std::size_t>{413, 17291, 26358, 43264, 50750, 64062, 87425, 88408}));
    std::vector<int> ints(10000);
    std::vector<std::uint64_t> int_keys(10000);
    for (std::size_t i = 0; i < ints.size(); ++i) {
        ints[i] = static_cast<int>(i);
        int_keys[i] = bz::keys::from_signed(static_cast<std::int64_t>(i * 7919) % 10000);
    }
    const auto int_key = bz::SortKey<int>::of("v", [](int i) { return (i * 7919LL) % 10000; });
    const auto golden = bz::BucketLayout<int>::estimate(int_key, ints, int_keys, plan);
    std::vector<std::int64_t> golden_bounds;
    for (std::uint64_t k : golden.boundaries()) {
        golden_bounds.push_back(static_cast<std::int64_t>(k ^ bz::keys::sign_bit));
    }
    CHECK(golden_bounds ==
          (std::vector<std::int64_t>{75, 183, 412, 918, 1963, 3964, 5712, 7785, 8936, 9548, 9769, 9926}));
    CHECK(golden.expected_sizes() ==
          (std::vector<std::size_t>{63, 127, 254, 513, 1025, 2046, 1938, 2046, 1025, 513, 254, 127, 68}));

    // Comparator-only layouts split on elements.
    const auto by_cmp = bz::SortKey<Rec>::of_comparator(
        "size_cmp", [](const Rec& x, const Rec& y) { return (x.size > y.size) - (x.size < y.size); });
    const auto cmp_layout = bz::BucketLayout<Rec>::estimate(by_cmp, items, {}, plan);
    CHECK(cmp_layout.expected_sizes() == plan.target_sizes(1000));
    CHECK(cmp_layout.splitters().size() + 1 == cmp_layout.bucket_count());
    CHECK(cmp_layout.boundaries().empty());
}

void check_matches(bz::BucketIndex<Rec>& index, const std::vector<Rec>& model, std::mt19937_64& rng) {
    const auto& key = *index.active_sort_key();
    const std::vector<int> expected = reference_ids(model, key, index.direction());
    CHECK(index.size() == expected.size());
    if (expected.empty()) {
        CHECK(index.first_page().empty());
        return;
    }
    std::uniform_int_distribution<std::size_t> pos(0, expected.size() - 1);
    for (int i = 0; i < 20; ++i) {
        const std::size_t p = pos(rng);
        CHECK(index.at(p).id == expected[p]);
    }
    const std::size_t from = pos(rng);
    const std::size_t to = from + pos(rng) % 150;
    const auto window = index.range(from, to);
    for (std::size_t i = 0; i < window.size(); ++i) {
        CHECK(window[i].id == expected[from + i]);
    }
    const auto page = index.first_page();
    CHECK(page.size() == std::min(expected.size(), index.plan().page_size()));
    for (std::size_t i = 0; i < page.size(); ++i) {
        CHECK(page[i].id == expected[i]);
    }
    const Rec& probe = model[pos(rng)];
    const auto found = index.position_of(probe);
    CHECK(found.has_value() && expected[*found] == probe.id);
}

void check_full(bz::BucketIndex<Rec>& index, const std::vector<Rec>& model) {
    const std::vector<int> expected = reference_ids(model, *index.active_sort_key(), index.direction());
    std::vector<int> got;
    index.for_each([&](const Rec& r) { got.push_back(r.id); });
    CHECK(got == expected);
}

void test_index_randomized(const bz::BucketPlan& plan, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    bz::BucketIndex<Rec> index(plan);
    for (auto& key : make_keys()) {
        index.add_sort_key(key);
    }
    const auto names = index.sort_key_names();
    std::vector<Rec> model;
    int next_id = 0;
    std::uniform_int_distribution<int> op(0, 9);
    for (int round = 0; round < 60; ++round) {
        switch (op(rng)) {
            case 0:
            case 1: {  // bulk insert
                std::vector<Rec> batch;
                const int count = static_cast<int>(rng() % 3000);
                for (int i = 0; i < count; ++i) {
                    batch.push_back(random_rec(rng, next_id++));
                }
                model.insert(model.end(), batch.begin(), batch.end());
                index.add_all(batch);
                break;
            }
            case 2:
            case 3: {  // single inserts
                for (int i = static_cast<int>(rng() % 200); i > 0; --i) {
                    model.push_back(random_rec(rng, next_id++));
                    index.add(model.back());
                }
                break;
            }
            case 4: {  // remove a random subset
                const int mod = 2 + static_cast<int>(rng() % 9);
                const int rem = static_cast<int>(rng() % static_cast<std::uint64_t>(mod));
                auto match = [&](const Rec& r) { return r.id % mod == rem; };
                const auto expected = static_cast<std::size_t>(std::count_if(model.begin(), model.end(), match));
                model.erase(std::remove_if(model.begin(), model.end(), match), model.end());
                CHECK(index.remove_if(match) == expected);
                break;
            }
            case 5: {  // remove one
                if (!model.empty()) {
                    const std::size_t victim = rng() % model.size();
                    CHECK(index.remove(model[victim]));
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(victim));
                }
                break;
            }
            case 6: {  // update a few elements in place
                const int mod = 5 + static_cast<int>(rng() % 50);
                std::mt19937_64 mutation_rng(rng());
                std::vector<Rec> replacements;
                for (Rec& r : model) {
                    if (r.id % mod == 0) {
                        Rec fresh = random_rec(mutation_rng, r.id);
                        r = fresh;
                        replacements.push_back(fresh);
                    }
                }
                std::size_t next = 0;
                const std::size_t changed = index.update_if([&](const Rec& r) { return r.id % mod == 0; },
                                                            [&](Rec& r) { r = replacements[next++]; });
                CHECK(changed == replacements.size());
                break;
            }
            case 7: {
                if (rng() % 4 == 0) {
                    model.clear();
                    index.clear();
                } else {
                    index.rebalance(names[rng() % names.size()]);
                }
                break;
            }
            default: break;
        }
        const std::string& name = names[rng() % names.size()];
        const auto dir = rng() % 2 ? bz::Direction::ascending : bz::Direction::descending;
        index.sort_by(name, dir);
        check_matches(index, model, rng);
        if (round % 10 == 0) {
            check_full(index, model);
        }
        const auto stats = index.stats(name);
        CHECK(sum(stats.actual_sizes) == model.size());
        CHECK(stats.bucket_count == stats.actual_sizes.size());
    }
    for (const auto& name : names) {
        index.sort_by(name, bz::Direction::descending);
        check_full(index, model);
    }
}

void test_laziness() {
    long comparisons = 0;
    bz::BucketIndex<Rec> index;
    index.add_sort_key(bz::SortKey<Rec>::of_comparator("name_cmp", [&](const Rec& a, const Rec& b) {
        ++comparisons;
        const int c = a.name.compare(b.name);
        return c != 0 ? (c > 0) - (c < 0) : (a.id > b.id) - (a.id < b.id);
    }));
    index.add_sort_key(bz::SortKey<Rec>::of("size", &Rec::size));
    std::mt19937_64 rng(3);
    std::vector<Rec> batch;
    const int n = 100000;
    for (int i = 0; i < n; ++i) {
        batch.push_back(random_rec(rng, i));
        batch.back().name += std::to_string(i);  // distinct names
    }
    index.add_all(batch);
    CHECK(!index.needs_rebalance("name_cmp"));

    comparisons = 0;
    index.sort_by("name_cmp");
    CHECK(comparisons == 0);  // switching only groups by bucket id
    const auto page = index.first_page();
    CHECK(page.size() == 64);
    // A full sort needs about n log2 n = 1.7 million comparisons.
    CHECK(comparisons < n / 10);
    std::printf("  first page after switching to comparator key: %ld comparisons (full sort ~%ld)\n",
                comparisons, static_cast<long>(n * std::log2(n)));

    index.sort_by("size");
    index.sort_by("name_cmp", bz::Direction::descending);
    comparisons = 0;
    index.first_page();
    CHECK(comparisons < n / 10);
    comparisons = 0;
    index.sort_by("name_cmp", bz::Direction::ascending);
    index.first_page();
    CHECK(comparisons == 0);  // cached grouping and sorted bucket were kept

    const auto stats = index.stats("name_cmp");
    CHECK(stats.first_page_cost_ascending < 1000);
    CHECK(stats.first_page_cost_descending < 1000);
    CHECK(stats.sorted_buckets >= 2);
}

/// Names sharing their first 8 bytes all get the same prefix key; the layout
/// must still cut them into page-sized buckets using the tie-breaker.
void test_colliding_prefix_keys() {
    bz::BucketIndex<Rec> index;
    index.add_sort_key(bz::SortKey<Rec>::of("name", &Rec::name));
    std::mt19937_64 rng(5);
    std::vector<Rec> batch;
    for (int i = 0; i < 50000; ++i) {
        Rec r;
        r.id = i;
        r.name = (i % 2 ? "Document " : "IMG_20240101_") + std::to_string(rng() % 1000000);
        batch.push_back(r);
    }
    index.add_all(batch);
    const auto stats = index.stats("name");
    CHECK(stats.first_page_cost_ascending < 300);
    CHECK(stats.first_page_cost_descending < 300);
    CHECK(!index.layout("name").splitters().empty());
    CHECK(stats.max_skew < 2.0);  // buckets follow the plan despite the collisions
    index.sort_by("name");
    const std::vector<int> expected = reference_ids(batch, *index.active_sort_key(), bz::Direction::ascending);
    const auto page = index.first_page();
    for (std::size_t i = 0; i < page.size(); ++i) {
        CHECK(page[i].id == expected[i]);
    }
    check_full(index, batch);
}

void test_rebalance_and_skew() {
    // Sample everything so the layout is exact and the sizes below are too.
    bz::BucketIndex<Rec> index(bz::BucketPlan::defaults().with_auto_rebalance(false).with_sample_size(1 << 16));
    index.add_sort_key(bz::SortKey<Rec>::of("size", &Rec::size));
    std::vector<Rec> batch;
    for (int i = 0; i < 10000; ++i) {
        Rec r;
        r.id = i;
        r.size = i;
        batch.push_back(r);
    }
    index.add_all(batch);
    CHECK(index.needs_rebalance("size"));  // estimated from an empty collection
    index.rebalance("size");
    CHECK(!index.needs_rebalance("size"));
    const std::size_t buckets = index.bucket_count("size");
    CHECK(buckets > 10);
    // New, larger values all land in the last bucket (think: new files sorted by date).
    for (int i = 0; i < 2000; ++i) {
        Rec r;
        r.id = 10000 + i;
        r.size = 1000000 + i;
        index.add(r);
    }
    CHECK(index.bucket_sizes("size").back() == 64 + 2000);
    CHECK(index.needs_rebalance("size"));
    CHECK(index.stats("size").max_skew > 4.0);
    const auto estimate = index.estimate_bucket_sizes("size", index.plan());
    CHECK(estimate.back() == 64);
    index.rebalance("size");
    CHECK(index.bucket_sizes("size").back() == 64);

    // With automatic rebalancing the next read fixes it.
    bz::BucketIndex<Rec> automatic;
    automatic.add_sort_key(bz::SortKey<Rec>::of("size", &Rec::size));
    automatic.add_all(batch);
    CHECK(!automatic.needs_rebalance("size"));  // add_all re-estimated once at the end
    for (int i = 0; i < 2000; ++i) {
        Rec r;
        r.id = 10000 + i;
        r.size = 1000000 + i;
        automatic.add(r);
    }
    automatic.sort_by("size", bz::Direction::descending);
    CHECK(automatic.first_page().front().size == 1000000 + 1999);
    CHECK(!automatic.needs_rebalance("size"));
}

/// Handle elements: comparator-only layouts hold copies of the pointers, so
/// changing or deleting a splitter's target must trigger a re-estimate (ASan
/// reports a use-after-free otherwise).
void test_handle_elements() {
    std::mt19937_64 rng(21);
    std::vector<std::unique_ptr<Rec>> storage;
    bz::BucketIndex<Rec*> index(bz::BucketPlan::defaults().with_page_size(8));
    index.add_sort_key(bz::SortKey<Rec*>::of_comparator(
        "size", [](Rec* const& a, Rec* const& b) { return (a->size > b->size) - (a->size < b->size); }));
    std::vector<Rec*> batch;
    for (int i = 0; i < 5000; ++i) {
        storage.push_back(std::make_unique<Rec>(random_rec(rng, i)));
        batch.push_back(storage.back().get());
    }
    index.add_all(batch);
    const auto& splitters = index.layout("size").splitters();
    CHECK(!splitters.empty());

    // Change every splitter's target, then tell the index.
    std::vector<Rec*> changed(splitters.begin(), splitters.end());
    for (Rec* r : changed) {
        r->size = -r->size - 7;
        CHECK(index.update(r));
    }
    // Remove and delete the new splitters' targets.
    std::vector<Rec*> doomed(index.layout("size").splitters().begin(), index.layout("size").splitters().end());
    for (Rec* r : doomed) {
        CHECK(index.remove(r));
    }
    storage.erase(std::remove_if(storage.begin(), storage.end(),
                                 [&](const std::unique_ptr<Rec>& p) {
                                     return std::find(doomed.begin(), doomed.end(), p.get()) != doomed.end();
                                 }),
                  storage.end());
    for (int i = 0; i < 500; ++i) {
        storage.push_back(std::make_unique<Rec>(random_rec(rng, 5000 + i)));
        index.add(storage.back().get());
    }
    std::vector<std::int64_t> expected;
    for (const auto& p : storage) {
        expected.push_back(p->size);
    }
    std::sort(expected.begin(), expected.end());
    index.sort_by("size");
    std::vector<std::int64_t> got;
    index.for_each([&](Rec* const& r) { got.push_back(r->size); });
    CHECK(got == expected);
}

/// Folders first in both directions, with one pre-sort per key.
void test_groups() {
    struct Entry {
        std::string name;
        bool dir;
    };
    bz::BucketIndex<Entry> index(bz::BucketPlan::defaults().with_page_size(4));
    index.add_sort_key(bz::SortKey<Entry>::of("name", &Entry::name).with_groups(2, [](const Entry& e) {
        return e.dir ? std::size_t{0} : std::size_t{1};
    }));
    std::vector<Entry> entries;
    for (int i = 0; i < 300; ++i) {
        entries.push_back({"f" + std::to_string(1000 + i), false});
        if (i % 10 == 0) {
            entries.push_back({"d" + std::to_string(1000 + i), true});
        }
    }
    index.add_all(entries);
    CHECK(index.layout("name", 0).estimated_from() == 30);
    CHECK(index.layout("name", 1).estimated_from() == 300);
    CHECK(throws([&] { index.layout("name", 2); }));
    index.sort_by("name", bz::Direction::descending);
    CHECK(index.at(0).name == "d1290");   // folders still first, newest name first
    CHECK(index.at(29).name == "d1000");
    CHECK(index.at(30).name == "f1299");
    CHECK(index.at(329).name == "f1000");
    const auto page = index.range(28, 32);
    CHECK(page.size() == 4 && page[0].name == "d1010" && page[1].name == "d1000" && page[2].name == "f1299" &&
          page[3].name == "f1298");
    const auto stats = index.stats("name");
    CHECK(stats.first_page_cost_descending < 40);  // the end of the folder group, not the middle of everything
    index.sort_by("name", bz::Direction::ascending);
    CHECK(index.at(0).name == "d1000");
    CHECK(index.at(30).name == "f1000");
    std::size_t found = *index.position_if([](const Entry& e) { return e.name == "f1001"; });
    CHECK(found == 31);
    index.sort_by("name", bz::Direction::descending);
    found = *index.position_if([](const Entry& e) { return e.name == "f1001"; });
    CHECK(found == 328);
}

/// A comparator with cycles (rock-paper-scissors) must not crash the index or
/// lose elements. Real comparators have such bugs: Dolphin's natural compare
/// orders kernel-6.10 < kernel-6.9 < kernel-6.9.1 < kernel-6.10.
void test_inconsistent_comparator() {
    auto rps = [](const Rec& a, const Rec& b) {
        const int x = a.id % 3;
        const int y = b.id % 3;
        if (x == y) {
            return (a.id > b.id) - (a.id < b.id);
        }
        return (x + 1) % 3 == y ? -1 : 1;
    };
    // Antisymmetric but otherwise random per pair: cycles everywhere.
    auto chaos = [](const Rec& a, const Rec& b) {
        if (a.id == b.id) {
            return 0;
        }
        const auto lo = static_cast<std::uint64_t>(std::min(a.id, b.id));
        const auto hi = static_cast<std::uint64_t>(std::max(a.id, b.id));
        std::uint64_t h = (lo * 0x9E3779B97F4A7C15ULL) ^ (hi * 0xC2B2AE3D27D4EB4FULL);
        h ^= h >> 29;
        const int sign = (h & 1) == 0 ? -1 : 1;
        return a.id < b.id ? sign : -sign;
    };
    bz::BucketIndex<Rec> index(bz::BucketPlan::defaults().with_page_size(8));
    index.add_sort_key(bz::SortKey<Rec>::of_comparator("rps", rps));
    index.add_sort_key(bz::SortKey<Rec>::of_key("rps_tie", [](const Rec&) { return std::uint64_t{7}; }, rps));
    index.add_sort_key(bz::SortKey<Rec>::of_comparator("chaos", chaos));
    index.add_sort_key(bz::SortKey<Rec>::of_key("chaos_tie", [](const Rec&) { return std::uint64_t{7}; }, chaos));
    std::vector<Rec> batch;
    for (int i = 0; i < 5000; ++i) {
        Rec r;
        r.id = i;
        batch.push_back(r);
    }
    index.add_all(batch);
    for (const char* key : {"rps", "rps_tie", "chaos", "chaos_tie"}) {
        for (auto dir : {bz::Direction::ascending, bz::Direction::descending}) {
            index.sort_by(key, dir);
            std::vector<int> ids;
            index.for_each([&](const Rec& r) { ids.push_back(r.id); });
            std::sort(ids.begin(), ids.end());
            bool permutation = ids.size() == batch.size();
            for (std::size_t i = 0; permutation && i < ids.size(); ++i) {
                permutation = ids[i] == static_cast<int>(i);
            }
            CHECK(permutation);
        }
    }
}

void test_edges() {
    bz::BucketIndex<Rec> index;
    CHECK(index.size() == 0);
    CHECK(index.first_page().empty());
    CHECK(throws([&] { index.at(0); }));
    CHECK(throws([&] { index.sort_by("missing"); }));
    index.add_sort_key(bz::SortKey<Rec>::of("name", &Rec::name));
    CHECK(throws([&] { index.add_sort_key(bz::SortKey<Rec>::of("name", &Rec::name)); }));
    CHECK(throws([] { bz::SortKey<Rec>::of_key("", [](const Rec&) { return std::uint64_t{0}; }); }));
    CHECK(throws([] { bz::SortKey<Rec>::of("x", &Rec::size).with_groups(0, [](const Rec&) { return std::size_t{0}; }); }));
    {
        bz::BucketIndex<Rec> bad;
        bad.add_sort_key(bz::SortKey<Rec>::of("g", &Rec::size).with_groups(2, [](const Rec&) { return std::size_t{7}; }));
        CHECK(throws([&] { bad.add(Rec{1, "a", 0, 0}); }));
    }
    index.sort_by("name");
    CHECK(index.first_page().empty());
    CHECK(index.range(5, 2).empty());

    Rec b;
    b.id = 1;
    b.name = "b";
    Rec a;
    a.id = 2;
    a.name = "a";
    index.add(b);
    index.add(a);
    CHECK(index.at(0).name == "a");
    CHECK(index.range(0, 100).size() == 2);
    CHECK(index.bucket_of(a, "name") == 0);
    CHECK(index.remove_sort_key("name"));
    CHECK(index.active_sort_key() == nullptr);
    CHECK(index.at(0).name == "b");  // insertion order
    CHECK(!index.remove_sort_key("name"));
    CHECK(!index.position_of(Rec{99, "", 0, 0}).has_value());
    CHECK(index.position_of(a) == std::optional<std::size_t>(1));
}

}  // namespace

int main() {
    test_keys();
    test_plan();
    test_layout();
    test_index_randomized(bz::BucketPlan::defaults(), 11);
    test_index_randomized(bz::BucketPlan::defaults().with_page_size(16).with_sample_size(300), 12);
    test_index_randomized(bz::BucketPlan::first_page(40), 13);
    test_index_randomized(bz::BucketPlan::uniform(7).with_auto_rebalance(false), 14);
    test_index_randomized(bz::BucketPlan::of_sizes({5, 10, 400}).with_symmetric(false), 15);
    test_laziness();
    test_colliding_prefix_keys();
    test_rebalance_and_skew();
    test_handle_elements();
    test_groups();
    test_inconsistent_comparator();
    test_edges();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("bucketization tests passed\n");
    return 0;
}
