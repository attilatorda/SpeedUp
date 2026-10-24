# Ticket: lf normalizes names on every comparison

- **File at:** <https://github.com/gokcehan/lf/issues/new>
- **Search first:** issues for "sort slow", "natural sort performance". On
  2026-10-24 only #942 ("Sorting behaves really weird", closed) and #21 came up,
  and neither is about this
- **Re-checked:** 2026-10-24 on lf master `664d7c8` (`nav.go` and `misc.go`
  unchanged since the measurement)
- **Type:** performance, not a correctness bug. lf has no AI policy; disclose anyway

---

**Title:** Natural sort normalizes both names on every comparison (2–3 s for 100k files; 4–8× faster if done once per sort)

**Body:**

Hi! With the default `sortby natural`, `sortignorecase true` and
`sortignoredia true`, the comparison in `dir.sort()` calls `normalize()` on
both names, every time:

```go
case naturalSort:
    applySort(func(f1, f2 *file) int {
        return naturalCmp(normalize(f1.Name()), normalize(f2.Name()))
    })
```

`normalize()` is `strings.ToLower` plus `removeDiacritics`, so every
comparison allocates new strings. A sort does about n·log₂n comparisons, so a
folder of 100,000 files normalizes about 3.3 million names per sort. `name`,
`ext` and `custom` sorting do the same.

**Measured** with an exact copy of `dir.sort()` from 664d7c8 (Go 1.24, real
directory listings, hidden files shown, medians of 5, 4-vCPU Xeon @ 2.1 GHz).
"Normalized once" computes `normalize(f.Name())` for each file before
`applySort` and compares those strings. The resulting order was identical in
every case:

| folder | files | name sort today | names normalized once | faster by |
|---|---:|---:|---:|---:|
| /usr/bin | 1,172 | 7.6–12 ms | 1.5–1.7 ms | 4.6–7.6× |
| /usr/lib/x86_64-linux-gnu | 1,533 | 16–17 ms | 2.5–3.7 ms | 4.4–6.6× |
| node_modules, flattened | 36,069 | 599–651 ms | 110–114 ms | 5.5–5.7× |
| /usr, flattened | 97,974 | 1.86–2.00 s | 391–404 ms | 4.6–5.1× |
| 100,000 camera-roll names | 100,000 | 2.97–3.05 s | 449–481 ms | 6.3–6.6× |

A minimal change would be something like:

```go
case naturalSort:
    keys := make(map[*file]string, len(dir.files))
    for _, f := range dir.files {
        keys[f] = normalize(f.Name())
    }
    applySort(func(f1, f2 *file) int {
        return naturalCmp(keys[f1], keys[f2])
    })
```

The same pattern applies to `nameSort`, `extSort` and `customSort`. A slice
parallel to `dir.files` would avoid the map, but it would have to move with
the files during the sort.

A smaller, related point: when hidden files are hidden (the default),
`applyFilter` also calls `isHidden()`, a glob match per pattern, inside the
`SortStableFunc` comparison. Evaluating it once per file would remove that
from the comparisons.

I'd be happy to send a PR for the first part if this is welcome.

(Measured with a standalone copy of lf's sort code. An AI coding assistant
helped with the analysis and the harness; the numbers are from running it.)
