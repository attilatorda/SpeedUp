# Bucketization

Fast re-sorting for collections that can be ordered in several ways, based on
bucket sort.

A file manager is the motivating example. A directory listing can be sorted by
name, size, date modified, last used and so on. Every click on a column header
normally re-sorts the whole list (`O(n log n)` comparisons, often slow
locale-aware string comparisons) before the first row can be shown. With
bucketization, each element picks its bucket for every sort key once, when it
is loaded. A click then only regroups the elements by bucket id and sorts the
one small bucket that is on screen.

Implementations:

| Language | Location | Notes |
|----------|----------|-------|
| C++17 | `include/speedup/bucketization.hpp` | header-only, CMake target `speedup::bucketization` |
| Java 17 | `bindings/java/bucketization/` | pure Java, Maven module, no dependencies |

The last part of this document is the cross-language specification that
other ports (Python, C#, Rust, Kotlin, ...) follow.

Related documents:

- [benchmarks.md](benchmarks.md): measured against Dolphin's and GNOME's real sorting code on real folders
- [file-manager-survey.md](file-manager-survey.md): how 12 file managers sort today, and where Bucketization fits
- [kde-contribution-plan.md](kde-contribution-plan.md): steps towards a Dolphin contribution

> **Name.** The interface is called `Bucketization`. "Bucketisation" (British
> spelling) is just as correct; the `-ize` form matches the conventions of
> Java, Qt/KDE and most libraries that use the term (TensorFlow's
> `bucketize`, Spark's `Bucketizer`).

---

## How it works

1. **Sort keys.** Each way of ordering is a *sort key*: "name", "size",
   "modified". A sort key either maps every element to a 64-bit
   order-preserving *normalized key* (numbers exactly, strings by their
   first 8 bytes), or is just a comparator.
2. **Bucket layout.** For each sort key, a sample of the data is sorted and
   cut into buckets of *planned, unequal sizes*. The default plan makes the
   first and the last bucket one page long, which is what a view shows first
   in ascending and in descending order. Buckets grow geometrically towards
   the middle.
3. **Pre-sorting.** When an element is added, it picks its bucket for every
   sort key: a binary search over a few split points. Bucket ids are stored
   as 16-bit integers per element per key.
4. **Switching the order** (`sort_by`) is a counting pass over the bucket ids,
   `O(n)` with no comparisons. The grouping is cached per sort key, so
   switching back to a key used before costs `O(1)`.
5. **Lazy sorting.** A bucket is fully sorted the first time a position inside
   it is read. Showing the first page sorts about one page of elements.
   Scrolling sorts the next buckets as they come into view. `sort_all()`
   finishes the rest, for example in an idle callback.

```
 elements ──pre-sort──▶ bucket id per (element, sort key)        (once, at load)
 click "size" ─────────▶ group by bucket id: [b0][ b1 ][   b2   ]…[b1'][b0']   O(n)
 show rows 0..63 ──────▶ sort b0 only                                    O(p log p)
 click "size ↓" ───────▶ same grouping read backwards, sort b0'          O(p log p)
```

### Costs

| Operation | Cost (n elements, k buckets, p page size) |
|-----------|-------------------------------------------|
| add one element | `O(keys × log k)`; comparator-only keys pay `log k` comparisons |
| `sort_by` / `sortBy` | `O(n + k)`, or `O(1)` when the key's grouping is cached |
| first page | sorts the bucket(s) covering it, about `O(p log p)` |
| scrolling | each newly visible bucket is sorted once |
| `remove_if` / `update_if` | `O(n)` per call, however many elements match |
| memory | per key: 8 B normalized key (keyed keys) + 2 B bucket id per element, plus 4 B per element for a cached grouping |

### Ordering guarantees

- The order of a sort key is exact. Buckets only decide *when* elements get
  sorted, never *where* they end up.
- Elements that compare equal keep insertion order (ascending).
- If a comparator is inconsistent (has cycles, a < b < c < a), every element
  is still returned exactly once and nothing crashes or throws; the order is
  then as undefined as with any other sort. Only merge sorts are used inside
  buckets for this reason.
- Descending order is the exact reverse of ascending order, except for a
  sort key's **groups** (see below): groups such as "folders, then files"
  stay in that order, and only the order inside each group is reversed.
- Positions (`at(i)` / `get(i)`) index the current order. Bucket ids always
  follow ascending key order.

---

## API

The interface is the same in both languages, with each language's naming
conventions.

| Purpose | C++ (`speedup::bucketization`) | Java (`io.github.attilatorda.speedup.bucketization`) |
|---------|------|------|
| interface / implementation | `Bucketization<T>` / `BucketIndex<T>` | `Bucketization<T>` / `BucketIndex<T>` |
| register a sort key | `add_sort_key(SortKey<T>)` | `addSortKey(SortKey<T>)` |
| folders first in both directions | `SortKey::with_groups(n, fn)` | `SortKey.withGroups(n, fn)` |
| choose the order | `sort_by(name, Direction)`, `unsort()` | `sortBy(name, Direction)`, `unsort()` |
| read in order | `at(i)`, `range(from, to)`, `first_page()`, `for_each(fn)` | `get(i)`, `range(from, to)`, `firstPage()`, iterator, `asList()` |
| find an element | `position_if(pred)`, `position_of(v)` | `positionIf(pred)`, `positionOf(o)` |
| finish sorting | `sort_all()` | `sortAll()` |
| bucket of an element | `bucket_of(v, key)` | `bucketOf(e, key)` |
| distribution | `bucket_sizes`, `layout`, `stats`, `estimate_bucket_sizes` | `bucketSizes`, `layout`, `stats`, `estimateBucketSizes` |
| rebalancing | `needs_rebalance`, `rebalance`, `rebalance_all`, `set_plan` | `needsRebalance`, `rebalance`, `rebalanceAll`, `setPlan` |
| changes | `add`, `add_all`, `remove_if`, `update_if(pred, mutate)`, `remove`, `update`, `clear` | `add`, `addAll`, `removeIf`, `updateIf(pred, mutator)`, `remove`, `update`, `clear` |

### C++

```cpp
#include "speedup/bucketization.hpp"
namespace bz = speedup::bucketization;

struct File { std::string name; std::int64_t size; std::int64_t modified; };

bz::BucketIndex<File> files(bz::BucketPlan::defaults().with_page_size(visible_rows));
files.add_sort_key(bz::SortKey<File>::of("name", &File::name));      // prefix key + full compare
files.add_sort_key(bz::SortKey<File>::of("size", &File::size));      // exact key
files.add_sort_key(bz::SortKey<File>::of("modified", &File::modified));
files.add_all(load_directory());            // pre-sorting happens here

files.sort_by("modified", bz::Direction::descending);
for (const File& f : files.range(first_visible, first_visible + visible_rows)) draw(f);
```

Store a cheap handle (`const File*`, an id, a shared handle) when elements are
large. `range()` returns copies.

### Java

```java
BucketIndex<File> files = new BucketIndex<>(BucketPlan.defaults().withPageSize(visibleRows));
files.addSortKey(SortKey.ofCollated("name", File::name, Collator.getInstance()));
files.addSortKey(SortKey.ofLong("size", File::size));
files.addSortKey(SortKey.ofLong("modified", File::modified));
files.addAll(loadDirectory());

files.sortBy("modified", Direction.DESCENDING);
List<File> rows = files.range(firstVisible, firstVisible + visibleRows);
```

---

## Sort keys

### Keyed sort keys

A keyed sort key maps each element to a **normalized key**: an unsigned
64-bit value whose numeric order agrees with the element order. The exact
order of a keyed sort key is defined as *normalized key first, then the
optional tie-breaker*. That makes it a valid total order by construction:
key and tie-breaker cannot contradict each other.

| Value | Encoding | C++ | Java |
|-------|----------|-----|------|
| unsigned integer | as is | `keys::from_unsigned` | `Keys.fromUnsigned` |
| signed integer | flip the sign bit | `keys::from_signed` | `Keys.fromSigned`, `SortKey.ofLong` |
| double | IEEE total order; NaN after +inf | `keys::from_double` | `Keys.fromDouble`, `SortKey.ofDouble` |
| byte string | first 8 bytes big-endian, zero padded | `keys::from_bytes`, `keys::from_string` | `Keys.fromBytes` |
| Java string | first UTF-16 code units packed UTF-8 style | n/a | `Keys.fromString`, `SortKey.ofString` |
| case-insensitive | same, after per-char case folding | n/a | `Keys.fromStringIgnoreCase`, `SortKey.ofStringIgnoreCase` |
| locale collation | prefix of the collation key | `keys::from_bytes(strxfrm / ICU sort key)` | `SortKey.ofCollated` |

String keys are *prefix* keys, so names that share their first 8 bytes get
the same normalized key, and the tie-breaker orders them. The bucket layout
handles such collisions (see below), so long shared prefixes like
`IMG_20240101_…` or `Screenshot 2024-…` still give page-sized buckets.

**Groups: folders first in both directions.** File managers keep folders
before files whichever way a column is sorted. `with_groups` / `withGroups`
gives a sort key a few classes that keep ascending order in both
directions; descending order reverses only the inside of each group:

```cpp
auto by_date = bz::SortKey<Entry>::of("modified", &Entry::modified)
                   .with_groups(2, [](const Entry& e) { return e.is_dir ? 0u : 1u; });
files.sort_by("modified", bz::Direction::descending);  // folders first, newest first
```

```java
SortKey<Entry> byDate = SortKey.<Entry>ofLong("modified", Entry::modified)
        .withGroups(2, e -> e.isDir() ? 0 : 1);
```

Each group gets its own layout with page-sized end buckets, so the first
descending page (the end of the folder group) is cheap too, and one
pre-sort serves both directions. Up to 256 groups, e.g. 4 for "folders
first, hidden last".

**Composite keys.** Several criteria can also share one 64-bit key, e.g. a
top bit for "folders first" and the timestamp below it. Prefer groups for
anything that must not flip with the direction.

### Comparator-only sort keys

`of_comparator` / `ofComparator` accepts any comparator, such as natural order
("file2" < "file10"), which has no simple prefix key. Buckets are cut on
sampled *splitter elements*, and assigning an element costs `log k` comparator
calls, once, when it is added. Switching to such a key and showing the first
page needs no comparisons beyond sorting that page (the C++ test measures
about 700 comparisons for 100,000 elements, against about 1.7 million for a
full sort).

---

## Bucket plans

`BucketPlan` sets the target size of every bucket. Buckets do not have to be
equal.

| Plan | Target sizes for n = 1000, page = 50 |
|------|------------------------------------|
| `first_page(50)` / `firstPage(50)` | `[50, 900, 50]`, or `[50, 950]` without symmetry |
| `geometric(50, 2.0)` (default: page 64) | `[50, 100, 200, 300, 200, 100, 50]` |
| `uniform(4)` | `[250, 250, 250, 250]` |
| `of_sizes({50, 200})` / `ofSizes(50, 200)` | `[50, 200, 750]` |

Options: `with_page_size` (one screen of rows), `with_max_buckets` (at most
65535), `with_symmetric` (small buckets at both ends, on by default),
`with_sample_size` (default 2048), `with_seed`, `with_rebalance_skew`
(default 4), `with_auto_rebalance` (on by default).

The page size does not have to track the window size exactly.
`first_page()` and `range()` sort as many buckets as needed.

---

## Distribution, estimation and rebalancing

- `layout(key)` gives the split points and the **expected** size of every
  bucket, i.e. the model of the key's distribution.
- `bucket_sizes(key)` gives the **actual** sizes now.
- `stats(key)` summarizes skew, empty buckets, sorted buckets and the
  **first-page cost**: how many elements must be sorted before the first
  ascending or descending page can be shown.
- `estimate_bucket_sizes(key, plan)` predicts the sizes another plan would
  produce, without changing anything.
- A layout is **stale** (`needs_rebalance`) when the collection grew past
  twice the size the layout was estimated from, or some bucket holds more
  than `skew ×` its expected size. Expected sizes below one page count as
  one page.
- With auto-rebalance on, stale layouts are re-estimated once at the end of
  `add_all`, and before a sorted read once at least `max(page, n/8)` changes
  have accumulated. That keeps the cost amortized: a stream of new files
  that all land in the "newest" bucket triggers an occasional re-estimate,
  not one per file.
- The index tracks which elements its splitters were taken from. If one of
  them is updated or removed, that key's layout is re-estimated, because the
  splitter may alias the element (Java references, C++ pointer handles).

---

## Use in a file manager

The intended integration point is the sort/filter proxy of a directory view,
such as a `QSortFilterProxyModel`-style class in Qt/KDE file managers:

| View event | Bucketization call |
|------------|--------------------|
| directory listed / items added | `add_all(items)` (can run off the UI thread, before the index is shared) |
| item changed (renamed, modified) | `update(item)` or `update_if(pred, mutate)` |
| items deleted | `remove_if(pred)` |
| column header clicked | `sort_by(column, direction)` |
| rows painted | `range(first_visible, last_visible + 1)` |
| idle | `sort_all()` |
| "select the new file and scroll to it" | `position_of(item)` |

The C++ and Java examples (`examples/bucketization/file_browser.cpp`,
`FileBrowserDemo.java`) list a real directory and switch between sort
columns this way.

The index is not thread-safe, not even for concurrent reads, because reads
sort lazily. Guard it with the model's existing lock, or confine it to one
thread.

---

## Benchmarks

**Against real file manager stacks**, see [benchmarks.md](benchmarks.md):
Dolphin's own sorting code on Qt 6/ICU, and the GLib collation keys and
`g_sequence_sort` used by Nautilus, Thunar, Nemo and Caja, on real
directory listings (system folders, `node_modules`, the Linux kernel tree).

**Synthetic micro-benchmark:** `speedup_benchmark_bucketization` (C++) and `BucketizationBenchmark` (Java)
use synthetic directories: a third camera-style names (`IMG_20…`), a third
`Document N.pdf`, a third random words, plus log-normal sizes and random
timestamps. They report the median of several runs. See
[BENCHMARKS.md](../../BENCHMARKS.md) for how to run them and the full tables.

C++ results, release build on a 4-vCPU Xeon @ 2.1 GHz container, 1,000,000
elements:

| Key | full `std::sort` | pre-sort (4 keys, once) | switch + first page | cached switch | switch + sort everything |
|-----|------------------|--------------------------|---------------------|---------------|--------------------------|
| name | 1,052 ms | 135 ms | 2.1 ms | 0.004 ms | 781 ms |
| size | 416 ms | (same) | 1.8 ms | 0.005 ms | 145 ms |
| modified | 448 ms | (same) | 2.1 ms | 0.005 ms | 151 ms |

Showing the first page after a column switch is roughly 210–500× faster. Sorting
everything afterwards still costs less than one full sort, and pre-sorting
all four keys costs less than one full sort of any of them.

---

## Cross-language specification

A port is conforming when, for the same data, plan and seed, it produces the
same normalized keys (for the numeric encodings), the same sample, the same
split points and therefore the same bucket ids and order. The golden values
at the end are checked by both the C++ and the Java tests.

### S1. Normalized keys

Unsigned 64-bit, compared unsigned.

- signed `v`: `v XOR 0x8000000000000000`
- double `d`: NaN → `0xFFF8000000000000`; otherwise `bits = IEEE bits(d)`,
  `bits` has the sign bit set ? `NOT bits` : `bits OR 0x8000000000000000`
- bytes: `b0<<56 | b1<<48 | … | b7`, missing bytes are 0
- strings: a prefix encoding consistent with the language's native string
  comparison (C++: bytes, Java: UTF-16 code units packed as 1–3 UTF-8-style
  bytes). For BMP text without surrogates the Java key equals the C++ key of
  the UTF-8 bytes.

### S2. Exact order

- groups (when the key has them): `group(a)` vs `group(b)` first
- keyed: `cmp_unsigned(key(a), key(b))`, then `tie_break(a, b)` when the keys
  are equal (0 if there is no tie-breaker)
- comparator-only: `comparator(a, b)`
- final tie-break: insertion order (ascending)
- descending order: groups in ascending order, each group's elements in
  reversed ascending order (without groups: the whole order reversed)

### S3. Target sizes `target_sizes(n)`

Always at least one bucket. `n = 0` gives `[0]`.

- **uniform(k)**: `c = min(max_buckets, n)` buckets of `n / c`; the first
  `n mod c` get one more.
- **sizes(s0, s1, …)**: take `min(si, remaining)` while `remaining > 0` and
  fewer than `max_buckets − 1` buckets were taken; append any remainder as
  one bucket.
- **first_page(p)**: `p' = min(p, n)`. `[n]` if `max_buckets = 1` or
  `p' = n`; `[p', n − 2p', p']` if symmetric, `max_buckets ≥ 3` and
  `n > 2p'`; else `[p', n − p']`.
- **geometric(p, g)**: `front = []`, `back = []`, `remaining = n`,
  `budget = max_buckets`, `size = p` (a double).
  While `remaining > 0` and `budget > 1`: `step = ceil(size)`; push
  `min(step, remaining)` to `front`, subtract it, `budget −= 1`. If
  symmetric: stop if `remaining = 0` or `budget ≤ 1`, else push
  `min(step, remaining)` to `back`, subtract it, `budget −= 1`. Then
  `size = min(size × g, n)`.
  Afterwards append `remaining` to `front` if positive, then `back`
  reversed.

### S4. Sample

`s = min(n, sample_size)`. If `s = n` the sample is `0 … n−1`. Otherwise,
with SplitMix64 seeded with `seed`:

```
next(): state += 0x9E3779B97F4A7C15
        z = state
        z = (z XOR (z >> 30)) × 0xBF58476D1CE4E5B9
        z = (z XOR (z >> 27)) × 0x94D049BB133111EB
        return z XOR (z >> 31)
for j in 0 … s−1:
    lo = j·n / s,  hi = (j+1)·n / s          (integer division)
    sample[j] = lo + next() mod (hi − lo)    (unsigned)
```

Default plan values: page 64, growth 2.0, max buckets 64, symmetric, sample
2048, seed `0x5EEDB0C4E7`, skew 4.0, auto-rebalance on.

### S5. Split points

With groups, every group gets its own layout from the elements in that group
(in insertion order), using the plan with `max_buckets` capped at
`65535 / groups`. The steps below run once per group, with `n` the size of
the group and the sample drawn from the group's elements.

1. `targets = target_sizes(n)`. If it has one bucket, the layout has no
   split points and expected sizes `[n]`.
2. Sort the sample indices by the exact order (S2 without the insertion
   order tie-break) into `S[0 … s−1]`.
3. For `j = 1 … len(targets) − 1`: `C = targets[0] + … + targets[j−1]`,
   `q = C·s / n` (integer division).
4. For each `q` in order, with `previous` starting at 0: `lo` = first index
   whose element compares equal to `S[q]`, `hi` = one past the last.
   Candidate `lo` is valid if `lo > previous`; candidate `hi` if
   `hi < s` and `hi > previous`. Pick `lo`, unless `hi` is valid and
   (`lo` is not valid or `hi − q < q − lo`). If one was picked, record it and
   set `previous` to it.
5. Every recorded position `p` becomes a split point from element `S[p]`:
   - comparator-only: the split point is the element;
   - keyed: the split point is `key(S[p])`. It also stores the element when
     `key(S[p−1]) = key(S[p])`, i.e. when the split falls inside a run of
     equal keys.
6. Expected sizes: with recorded positions `p1 < … < pm`, `p0 = 0`,
   `p(m+1) = s`: `expected[i] = ((p(i+1) − p(i))·n + s/2) / s`.

### S6. Bucket assignment

`bucket(x)` = (number of buckets of the groups before `group(x)`) + number of
split points `sp` of `group(x)`'s layout with `sp ≤ x`, where `x < sp` means:

- comparator-only: `compare(x, element(sp)) < 0`
- keyed: `key(x) < key(sp)` (unsigned), or `key(x) = key(sp)` and `sp` stores
  an element and `tie_break(x, element(sp)) < 0`

### S7. Staleness

Stale if `n > 2 · max(estimated_from, page)`, or any bucket `b` has
`actual[b] > skew · max(expected[b], page, 1)`. Automatic rebalancing
additionally requires `changes since layout ≥ max(page, estimated_from / 8)`.

### S8. Golden values

- `sample(n = 100000, sample_size = 8, seed = 42)` =
  `[413, 17291, 26358, 43264, 50750, 64062, 87425, 88408]`
- Values `v(i) = (i · 7919) mod 10000` for `i = 0 … 9999`, signed key,
  default plan: split keys (decoded) `[75, 183, 412, 918, 1963, 3964, 5712,
  7785, 8936, 9548, 9769, 9926]`, expected sizes `[63, 127, 254, 513, 1025,
  2046, 1938, 2046, 1025, 513, 254, 127, 68]`
- `from_double(1.0) = 0xBFF0000000000000`, `from_double(−2.5) =
  0x3FFBFFFFFFFFFFFF`, `from_signed(−1) = 0x7FFFFFFFFFFFFFFF`
- string key of "été" (UTF-8 `C3 A9 74 C3 A9`) = `0xC3A974C3A9000000`

### Porting checklist

1. Implement S1–S7 and the golden tests (S8).
2. Port the randomized test: random adds, bulk adds, removals, updates,
   rebalances and clears, compared against a stable full sort after every
   step, in both directions, for every plan kind.
3. Port the laziness test: switching to a comparator-only key performs no
   comparisons; the first page of 100,000 elements needs fewer than n/10.
4. Port the colliding-prefix test (50,000 names sharing their first 8 bytes
   stay page-sized).
5. Keep the API names in the target language's conventions, mirroring the
   table above.

## Limitations and next steps

- Not thread-safe. Sorting not-yet-visible buckets on a background thread
  would need a copy of the grouping or a lock.
- One plan per index (not per sort key).
- Pre-sorting is eager per registered key. Integrations should register a
  column's key the first time it is used, and below about 2,000 items a
  plain sort is just as fast (see benchmarks.md).
- A C ABI (`speedup_bucket_*`) over numeric keys would let the existing
  bindings (Python, C#, Go, ...) reuse the C++ core. The comparator-only
  path needs callbacks across the ABI.
- Pre-sorting all keys could run in parallel, one thread per key.
