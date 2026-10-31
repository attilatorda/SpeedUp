---
title: C++ API reference
nav: C++ API
section: Reference
order: 21
---

Header-only C++17. The sorting layer forwards to the C ABI, so link
`speedup`; Bucketization needs nothing but its headers.

## Sorting

Header `speedup/sort.hpp`, namespace `speedup::sort`.

```api
template <class T> void radix_sort(T* data, std::size_t n, T* scratch = nullptr);
template <class T> void radix_sort(std::vector<T>& values);
Any 4- or 8-byte integer type (int, long, size_t, ...), float or double. Picks the C function with the matching width and signedness.
```

```api
template <class T> void bucket_sort(T* data, std::size_t n, T* scratch = nullptr);
template <class T> void bucket_sort(std::vector<T>& values);
8-byte integers or double.
```

```api
template <class T> void bitonic_sort(T* data, std::size_t n);
template <class T> void bitonic_sort(std::vector<T>& values);
4- or 8-byte integers, float or double.
```

```api
template <class T, class KeyFn> void radix_sort_by_key(std::vector<T>& values, KeyFn key);
Stable LSD radix sort of any movable objects by a numeric key (integer, floating-point, bool or enum), encoded like Bucketization's normalized keys, so NaN keys sort last. Sorts (key, index) pairs, then moves every element once.
```

```api
void name_sort(std::vector<std::string_view>& names, std::uint32_t flags = SPEEDUP_NAME_SORT_BYTES, speedup_name_sort_stats_t* stats = nullptr);
void name_sort(std::vector<std::string>& names, std::uint32_t flags = SPEEDUP_NAME_SORT_BYTES, speedup_name_sort_stats_t* stats = nullptr);
Name sort over views (the views are reordered) or strings (every string is moved once).
```

## Bucketization

Header `speedup/bucketization.hpp`, namespace `speedup::bucketization`.

```api
template <class T> class BucketIndex : public BucketedList<T>;
The implementation. Construct it with a BucketPlan: BucketIndex<File> files(BucketPlan::defaults().with_page_size(rows));
```

```api
void add_sort_key(SortKey<T> key);
bool remove_sort_key(std::string_view name);
std::vector<std::string> sort_key_names() const;
Registers a sort key and assigns every element to one of its buckets; throws std::invalid_argument if the name is taken. Removing the active key reverts to insertion order.
```

```api
void sort_by(std::string_view name, Direction direction = Direction::ascending);
void unsort();
Switches the order: O(n) grouping, no comparisons; a key used before reuses its cached grouping. unsort() returns to insertion order.
```

```api
std::size_t size() const;
const T& at(std::size_t position);
std::vector<T> range(std::size_t from, std::size_t to);
std::vector<T> first_page();
void for_each(const Visitor& visit);
std::optional<std::size_t> position_if(const Predicate& match);
Read in the current order; a bucket is sorted when a position inside it is first read. at() throws std::out_of_range; range() clamps to size() and returns copies.
```

```api
std::size_t bucket_of(const T& value, std::string_view key) const;
std::vector<std::size_t> bucket_sizes(std::string_view key) const;
bool needs_rebalance(std::string_view key) const;
void rebalance(std::string_view key);
void set_plan(BucketPlan plan);
Inspect and maintain the layout: the bucket an element belongs to, current bucket sizes, and re-estimation when the data drifted from the sample.
```

```api
void sort_all();
Fully sorts every bucket of the active key now, for example when the application is idle.
```

```api
void add(T value);
void add_all(std::vector<T> values);
std::size_t remove_if(const Predicate& match);
std::size_t update_if(const Predicate& match, const Mutator& mutate = {});
void clear();
Changes. update_if re-reads the keys of the matching elements; call it after anything that changes an element's order.
```

```api
static SortKey of(std::string name, Projection projection);
static SortKey of_key(std::string name, KeyFunction key, CompareFunction tie_breaker = {});
static SortKey of_comparator(std::string name, CompareFunction compare);
SortKey with_groups(std::size_t groups, GroupFunction group) const;
Sort keys (class SortKey<T>): from a member or projection (numbers exactly, strings by an 8-byte prefix plus a full comparison), from a 64-bit key function with an optional tie-breaker, or from a comparator only. with_groups (1 to 256 groups) keeps groups such as "folders first" in both directions.
```

```api
static BucketPlan defaults();
static BucketPlan first_page(std::size_t page_size);
static BucketPlan geometric(std::size_t page_size, double growth);
static BucketPlan uniform(std::size_t buckets);
static BucketPlan of_sizes(std::vector<std::size_t> sizes);
Bucket plans (class BucketPlan, immutable). defaults() is geometric(64, 2.0). Options: with_page_size, with_max_buckets, with_symmetric, with_sample_size, with_seed, with_rebalance_skew, with_auto_rebalance.
```

The complete interface, with the Java names alongside: `docs/bucketization/README.md`.
