---
title: Bucketization
section: Algorithms
order: 11
---

Fast re-sorting for collections that can be ordered in several ways, such as
a file manager's name, size and date columns. Header-only C++17
(`include/speedup/bucketization.hpp`, target `speedup::bucketization`) and
pure Java 17 (`bindings/java/bucketization/`).

## How it works

1. **Sort keys.** Each way of ordering is a sort key. It maps every element to
   a 64-bit order-preserving key (numbers exactly, strings by their first 8
   bytes), or it is just a comparator.
2. **Bucket layout.** For each key a sample is sorted and cut into buckets of
   planned, unequal sizes. The first and last buckets are one page long, so
   the first screen in either direction is cheap.
3. **Pre-sorting.** When an element is added, it picks its bucket for every
   key once: a binary search over a few split points.
4. **Switching the order** is a counting pass over the bucket ids: `O(n)`,
   with no comparisons. Switching back to a key used before costs `O(1)`.
5. **Lazy sorting.** A bucket is sorted the first time a position inside it is
   read. Showing the first page sorts about one page of elements.

The order is exact. Buckets only decide when an element gets sorted, never
where it ends up.

## Example

```cpp
#include "speedup/bucketization.hpp"
namespace bz = speedup::bucketization;

struct File { std::string name; std::int64_t size; std::int64_t modified; };

bz::BucketIndex<File> files(bz::BucketPlan::defaults().with_page_size(visible_rows));
files.add_sort_key(bz::SortKey<File>::of("name", &File::name));
files.add_sort_key(bz::SortKey<File>::of("size", &File::size));
files.add_sort_key(bz::SortKey<File>::of("modified", &File::modified));
files.add_all(load_directory());            // pre-sorting happens here

files.sort_by("modified", bz::Direction::descending);   // O(n), no comparisons
for (const File& f : files.range(first, first + visible_rows)) draw(f);
```

## Costs

| Operation | Cost (n elements, k buckets, p page size) |
|-----------|-------------------------------------------|
| add one element | `O(keys × log k)` |
| `sort_by` | `O(n + k)`, or `O(1)` when the grouping is cached |
| first page | about `O(p log p)` |
| `remove_if` / `update_if` | `O(n)` per call |
| memory per key and element | 8 B key + 2 B bucket id, plus 4 B for a cached grouping |

## Limits

- Not thread-safe.
- Below about 2,000 items a plain sort is just as fast.
- One bucket plan per index, not per sort key.

Full design and the cross-language specification:
`docs/bucketization/README.md`. Measured in the sort code of KDE Dolphin,
the GNOME file managers, Qt file managers, terminal file managers and the
Godot editor: `docs/bucketization/benchmarks.md` and the paper.
