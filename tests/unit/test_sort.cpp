// Tests the C++ layer (speedup/sort.hpp) against the standard library:
// every overload on the integer types it maps, radix_sort_by_key's
// stability and key encodings, and name_sort on strings and views.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "speedup/sort.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        if (failures < 20) std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

std::mt19937_64 rng(7);

template <class T>
std::vector<T> random_values(std::size_t n) {
    std::vector<T> v(n);
    for (T& x : v) {
        if constexpr (std::is_floating_point_v<T>) {
            x = static_cast<T>(std::ldexp(static_cast<double>(rng() >> 11) / 9007199254740992.0 - 0.5,
                                          static_cast<int>(rng() % 200) - 100));
        } else {
            x = static_cast<T>(rng());
        }
    }
    return v;
}

template <class T>
void test_numeric(const char* name) {
    for (std::size_t n : {0u, 1u, 7u, 300u, 5000u, 100000u}) {
        std::vector<T> expect = random_values<T>(n);
        std::vector<T> radix = expect, bucket = expect, bitonic = expect;
        std::sort(expect.begin(), expect.end());
        speedup::sort::radix_sort(radix);
        speedup::sort::bitonic_sort(bitonic);
        check(radix == expect, name);
        check(bitonic == expect, name);
        if constexpr (sizeof(T) == 8) {
            speedup::sort::bucket_sort(bucket);
            check(bucket == expect, name);
        }
    }
}

struct Row {
    int key;
    int seq;
};

enum class Level : std::uint8_t { low, mid, high };

void test_by_key() {
    std::vector<Row> rows(50000);
    for (std::size_t i = 0; i < rows.size(); ++i) rows[i] = {static_cast<int>(rng() % 100) - 50, static_cast<int>(i)};
    std::vector<Row> expect = rows;
    std::stable_sort(expect.begin(), expect.end(), [](const Row& a, const Row& b) { return a.key < b.key; });
    speedup::sort::radix_sort_by_key(rows, [](const Row& r) { return r.key; });
    bool same = true;
    for (std::size_t i = 0; i < rows.size(); ++i) same = same && rows[i].key == expect[i].key && rows[i].seq == expect[i].seq;
    check(same, "radix_sort_by_key: stable like std::stable_sort");

    // Move-only elements and floating-point keys, NaN last (keys::normalize).
    std::vector<std::unique_ptr<double>> boxes;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (double d : {3.5, -0.0, nan, -1e300, 0.0, 2.0, -std::numeric_limits<double>::infinity()}) {
        boxes.push_back(std::make_unique<double>(d));
    }
    speedup::sort::radix_sort_by_key(boxes, [](const std::unique_ptr<double>& p) { return *p; });
    check(*boxes[0] == -std::numeric_limits<double>::infinity() && *boxes[1] == -1e300 && std::signbit(*boxes[2]) &&
              *boxes[3] == 0.0 && !std::signbit(*boxes[3]) && *boxes[4] == 2.0 && *boxes[5] == 3.5 &&
              std::isnan(*boxes[6]),
          "radix_sort_by_key: double keys, -0 before +0, NaN last");

    std::vector<Level> levels = {Level::high, Level::low, Level::mid, Level::low};
    speedup::sort::radix_sort_by_key(levels, [](Level l) { return l; });
    check(levels == std::vector<Level>{Level::low, Level::low, Level::mid, Level::high}, "radix_sort_by_key: enum keys");
}

std::string random_name() {
    static const char* bases[] = {"Smith", "Szabo", "McLean", "Mac", "Nagy", "nagy", "\xC3\x81" "brah\xC3\xA1m", ""};
    std::string s = bases[rng() % 8];
    for (int i = static_cast<int>(rng() % 5); i > 0; --i) s.push_back(static_cast<char>('a' + rng() % 26));
    return s;
}

void test_names() {
    for (std::size_t n : {0u, 5u, 1000u, 60000u, 200000u}) {
        std::vector<std::string> names(n);
        for (std::string& s : names) s = random_name();
        std::vector<std::string> expect = names;
        std::sort(expect.begin(), expect.end());  // char_traits<char> compares as unsigned char
        std::vector<std::string_view> views(names.begin(), names.end());
        speedup::sort::name_sort(views);
        bool same = true;
        for (std::size_t i = 0; i < n; ++i) same = same && views[i] == expect[i];
        check(same, "name_sort(string_view): byte order like std::sort");
        speedup_name_sort_stats_t stats{};
        speedup::sort::name_sort(names, SPEEDUP_NAME_SORT_BYTES, &stats);
        check(names == expect, "name_sort(string): byte order like std::sort");
        if (n >= 200000) check(stats.buckets > 100 && stats.max_bucket < n, "name_sort: the plan cut buckets");
    }
}

}  // namespace

int main() {
    test_numeric<std::int32_t>("int32_t");
    test_numeric<std::uint32_t>("uint32_t");
    test_numeric<std::int64_t>("int64_t");
    test_numeric<std::uint64_t>("uint64_t");
    test_numeric<int>("int");
    test_numeric<long>("long");
    test_numeric<unsigned long long>("unsigned long long");
    test_numeric<std::size_t>("size_t");
    test_numeric<float>("float");
    test_numeric<double>("double");
    test_by_key();
    test_names();
    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("sort (C++): all checks passed\n");
    return 0;
}
