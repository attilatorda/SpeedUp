---
title: Name sort
section: Algorithms
order: 13
---

A bucket sort for person names. The bucket boundaries come from
character-frequency tables of real names, one for each of the first three
positions, built from US Census data and refined on a sample of the input.

```c
speedup_str_t names[M];          /* {data, len}; need not be NUL-terminated */
speedup_name_sort(names, M, SPEEDUP_NAME_SORT_BYTES);
speedup_name_sort(names, M, SPEEDUP_NAME_SORT_ASCII_CASELESS);
```

## Why it can work

Names are far from uniform. About 9.6 % of US surnames begin with M and
0.04 % with X, and prefixes such as *Ma*, *Mc* and *Wil* hold more than one
percent each. Knowing where the dense regions are lets the sort cut buckets
of equal expected size in one step.

## Algorithm

1. **Sample** `n/128` names (256 to 16,384) and sort them.
2. **Plan** a trie of at most three 257-entry lookup tables per path. A
   prefix expected to hold more than 32 names gets a child table for the
   next position; light symbols share leaves. The leaves are packed into
   buckets of about 32 names.
3. **Distribute** in two linear passes, with at most three lookups per name.
4. **Finish** each bucket: insertion sort or introsort up to 32 names, MSD
   radix sort above. Both skip the bytes the bucket's prefix fixes.

Below 131,072 names, planning does not pay, and name sort is its MSD radix
sort alone.

## What the measurements say

- On a million synthetic "Surname Firstname" names it is about 3× faster than
  `std::sort` and faster than a plain MSD radix sort.
- On the 162,253 real Census surnames it is about 3× faster than `std::sort`
  but slower than plain MSD radix sort, because short unique surnames suit
  MSD radix sort.
- The frequency tables did not measurably beat the input sample alone, and
  deeper tries, with more even buckets, were slower.

## Unicode

UTF-8 already sorts correctly, in code point order. For language order
(Hungarian *cs* after *c*, German *ä* with *a*), sort ICU or `strxfrm`
collation keys with the same function. The sample then does the work the
ASCII-only tables cannot.

Design notes: `docs/sorting/name-sort.md`. Study: `docs/papers/sorting/`.
