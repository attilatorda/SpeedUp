# Ticket: Krusader's descending comparator is not a strict weak ordering

- **File at:** <https://bugs.kde.org/enter_bug.cgi?product=krusader> (component *general*; version *git master*)
- **Search first** (product krusader): "itemGreaterThan", "descending", "sort order", "strict weak"
- **Re-checked:** 2026-10-24 on Krusader master `d20c6d0`, with a standalone copy
  of `krsort.cpp`'s comparison and `KrSort::Sorter` (not in a running Krusader)
- **Severity:** low. The visible effect is only the order of items that compare
  equal; the real issue is undefined behaviour in `std::stable_sort`
- **Best edited into your own words** before posting

---

**Title:** Descending sort uses a comparator that is not a strict weak ordering (undefined behaviour in std::stable_sort)

**Description:**

SUMMARY

In `app/Panel/PanelView/krsort.cpp`, `KrSort::itemGreaterThan(a, b)` is
`!itemLessThan(a, b)`. For two items that compare equal it returns true both
ways, and it returns true for an item compared with itself.

`Sorter::sort()` passes it to `std::stable_sort`, and `Sorter::insertIndex()`
passes it to `std::lower_bound`. Both require a strict weak ordering, so with
such items the result is undefined.

Items compare equal in practice. With the default "case insensitive" option,
`README`, `Readme` and `readme` compare equal, because names are lowercased
before the locale-aware comparison. Equal names also occur in search results
and branch views.

STEPS TO REPRODUCE

1. Build Krusader with `-D_GLIBCXX_DEBUG` (libstdc++ debug mode).
2. Open a folder containing `README` and `readme`.
3. Sort the panel by name, descending.

OBSERVED RESULT

With libstdc++ debug mode, `std::stable_sort` aborts with "comparison doesn't
meet irreflexive requirements, assert(!(a < a))". I reproduced this with a
copy of the comparison and of `KrSort::Sorter`. Without debug mode nothing
crashes, and equal items come out in an order that depends on the listing
order. (That also happens in ascending order, where it is legal, because the
comparison has no tie-breaker.)

EXPECTED RESULT

A strict weak ordering in both directions.

POSSIBLE FIX

```cpp
bool itemGreaterThan(SortProps *sp, SortProps *sp2)
{
    return !itemLessThan(sp, sp2) && itemLessThan(sp2, sp);
}
```

It returns the same as today for every pair except equal items (and an item
with itself), for which it returns false. I checked it on all triples of a
test set (files and folders, case variants, equal sizes and times). It has no
strict-weak-ordering violations for name, size and modified sorting, with
and without "always sort dirs by name". The current function has 6–12
violations on that set.

The simpler `return itemLessThan(sp2, sp);` does not work. With "always sort
dirs by name" in descending mode, `itemLessThan()` XORs the name comparison
with true. So `itemLessThan(sp2, sp)` becomes "less or equal" for folders,
which breaks the folder ordering.

SOFTWARE/OS VERSIONS

Krusader git master d20c6d0 (2026-10-23).

ADDITIONAL INFORMATION

Found while checking sort comparisons for consistency with a standalone copy
of the code. An AI coding assistant helped with the analysis; the results
above are from running the copied code.
