# Ticket: lf normalizes names on every comparison

- **App:** lf ("list files"), a terminal file manager written in Go, by
  Gökçehan Kara and contributors. MIT license
- **File at:** <https://github.com/gokcehan/lf/issues/new/choose>, then pick
  **Bug report** ("Report a problem"). Blank issues are turned off
- **Search first:** issues for "sort slow", "natural sort performance". On
  2026-10-24 only #942 ("Sorting behaves really weird", closed) and #21 came up,
  and neither is about this
- **Re-checked:** 2026-10-27 on lf master `78f781b`. `dir.sort()`, `naturalCmp`
  and `removeDiacritics` are the same as in `664d7c8`, the commit that was
  measured
- **Type:** performance, not a correctness bug
- **Verify it yourself first.** lf's form says: "Verify bugs yourself. AI
  claims alone aren't proof." Run the benchmark below (it needs Go and a clone
  of lf) and paste your own output. The other numbers came from a standalone
  copy of lf's sort code that an AI assistant ran

## Verifying

```sh
git clone https://github.com/gokcehan/lf && cd lf
cp /path/to/SpeedUp/docs/bucketization/launch/tickets/lf-sort-benchmark_test.go sort_bench_test.go
go test -run '^$' -bench BenchmarkSort -benchtime 5x
```

Then apply the "normalized once" change from the ticket text to the
`naturalSort` case in `nav.go`, run the same command again, and run
`go test ./...` to make sure nothing else breaks. Keep both outputs for the
ticket, and delete `sort_bench_test.go` afterwards (`git checkout nav.go` to
undo the change).

To see it in lf itself (optional):

```sh
mkdir /tmp/lf-sort && cd /tmp/lf-sort
seq 1 100000 | sed 's/^/IMG_/; s/$/.JPG/' | xargs touch
lf -config /dev/null /tmp/lf-sort
```

In lf, type `:set reverse!` and Enter a few times, and note how long the list
takes to update.

## Filling in the form

| Field | What to enter |
|---|---|
| Title | the **Title** line below |
| Description | the **Description** text below |
| Steps to reproduce | the **Steps to reproduce** text below, with your benchmark output pasted in |
| lf version | the output of `lf -version`. If you only built it from source for the benchmark, write `master 78f781b (built from source)` |
| Environment | your operating system and terminal, plus CPU and Go version for the benchmark (`go version`) |

Then press **Create**, and put the issue's link into
[../projects/lf.md](../projects/lf.md) and the table in
[README.md](README.md).

---

**Title:**

Natural sort normalizes both names on every comparison (2–3 s for 100k files; 4–8× faster if done once per sort)

**Description:**

With the default `sortby natural`, `sortignorecase true` and
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

Measured with a copy of `dir.sort()` (Go 1.24, real directory listings,
hidden files shown, medians of 5, 4-vCPU Xeon @ 2.1 GHz). "Normalized once"
computes `normalize(f.Name())` for each file before `applySort` and compares
those strings. The resulting order was identical in every case:

```
folder                       files  name sort today  normalized once  faster by
/usr/bin                     1,172  7.6–12 ms        1.5–1.7 ms       4.6–7.6×
/usr/lib/x86_64-linux-gnu    1,533  16–17 ms         2.5–3.7 ms       4.4–6.6×
node_modules, flattened     36,069  599–651 ms       110–114 ms       5.5–5.7×
/usr, flattened             97,974  1.86–2.00 s      391–404 ms       4.6–5.1×
100,000 camera-roll names  100,000  2.97–3.05 s      449–481 ms       6.3–6.6×
```

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

(An AI coding assistant helped with the analysis and wrote the first
measurements, using a standalone copy of lf's sort code. I re-ran the
benchmark below on lf itself.)

**Steps to reproduce:**

1. Add this benchmark to a clone of lf as `sort_bench_test.go`. It creates n
   empty files and runs `dir.sort()` with the default options:

```go
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func benchmarkSort(b *testing.B, n int) {
	tmp := b.TempDir()
	for i := 0; i < n; i++ {
		var name string
		switch i % 4 {
		case 0:
			name = fmt.Sprintf("IMG_%06d.JPG", i)
		case 1:
			name = fmt.Sprintf("report-%d-final.pdf", i)
		case 2:
			name = fmt.Sprintf("Été %d.txt", i)
		default:
			name = fmt.Sprintf(".cache-%d", i)
		}
		if err := os.WriteFile(filepath.Join(tmp, name), nil, 0o644); err != nil {
			b.Fatal(err)
		}
	}
	dir := newDir(tmp)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		dir.sort()
	}
}

func BenchmarkSort1k(b *testing.B)   { benchmarkSort(b, 1000) }
func BenchmarkSort10k(b *testing.B)  { benchmarkSort(b, 10000) }
func BenchmarkSort100k(b *testing.B) { benchmarkSort(b, 100000) }
```

2. Run `go test -run '^$' -bench BenchmarkSort -benchtime 5x` on master
   (78f781b):

```
<paste your output here>
```

3. Apply the change above to the `naturalSort` case and run it again:

```
<paste your output here>
```
