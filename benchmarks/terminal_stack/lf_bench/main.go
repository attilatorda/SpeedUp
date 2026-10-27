// lf's real sort path, natively in Go, on the Bucketization datasets.
//
// dir.sort, naturalCmp, isHidden and matchPattern below are lf's nav.go,
// misc.go and os.go (commit 664d7c8, MIT License, Copyright (c) 2016 Gökçehan
// Kara) with lf's option getters replaced by the dir's fields; diacritics.go is
// lf's file. lf's defaults: natural sort, ignore case and diacritics, folders
// first. Hidden files are shown (as in the other benchmarks); with lf's default
// of hiding them, dir.sort also runs a stable partition with glob matching,
// reported as fm_resort_default_ms.
//
// fm_resort_cached_ms is the same dir.sort with every name normalized once per
// sort instead of on every comparison (a possible lf improvement that does not
// need Bucketization); it must produce the same order.
//
// Usage: lf_bench [-repeats N] [-csv FILE] DATASET.tsv...
// Writes: project,dataset,n,role,order,fm_resort_ms,fm_resort_default_ms,fm_resort_cached_ms,ties,order_hash
package main

import (
	"bufio"
	"cmp"
	"flag"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"
)

// --- lf's file, with os.FileInfo from the dataset ------------------------------

type fakeStat struct {
	name    string
	size    int64
	mode    fs.FileMode
	modTime time.Time
}

func (s *fakeStat) Name() string       { return s.name }
func (s *fakeStat) Size() int64        { return s.size }
func (s *fakeStat) Mode() fs.FileMode  { return s.mode }
func (s *fakeStat) ModTime() time.Time { return s.modTime }
func (s *fakeStat) IsDir() bool        { return s.mode.IsDir() }
func (s *fakeStat) Sys() any           { return nil }

type linkState byte

type file struct {
	os.FileInfo
	linkState  linkState
	linkTarget string
	path       string
	dirCount   int
	dirSize    int64
	accessTime time.Time
	birthTime  time.Time
	changeTime time.Time
	customInfo string
	ext        string
	err        error
}

type sortMethod byte

const (
	naturalSort sortMethod = iota
	nameSort
	sizeSort
	timeSort
)

type dir struct {
	path           string
	files          []*file
	allFiles       []*file
	sortby         sortMethod
	dircounts      bool
	dirfirst       bool
	dironly        bool
	hidden         bool
	reverse        bool
	cacheNames     bool // normalize each name once per sort (not in lf)
	hiddenfiles    []string
	filter         []string
	sortignorecase bool
	sortignoredia  bool
}

func isDigit(b byte) bool {
	return '0' <= b && b <= '9'
}

// --- misc.go -------------------------------------------------------------------

func replaceTilde(s string) string {
	if rest, ok := strings.CutPrefix(s, "~"); ok && (rest == "" || os.IsPathSeparator(rest[0])) {
		return homeDir + rest
	}
	return s
}

var homeDir = "/home/user"

func naturalCmp(s1, s2 string) int {
	s1len := len(s1)
	s2len := len(s2)

	var lo1, lo2, hi1, hi2 int
	for {
		switch {
		case hi1 >= s1len && hi2 >= s2len:
			return 0
		case hi1 >= s1len && hi2 < s2len:
			return -1
		case hi1 < s1len && hi2 >= s2len:
			return 1
		}

		lo1 = hi1
		isDigit1 := isDigit(s1[hi1])
		for hi1 < s1len && isDigit(s1[hi1]) == isDigit1 {
			hi1++
		}
		tok1 := s1[lo1:hi1]

		lo2 = hi2
		isDigit2 := isDigit(s2[hi2])
		for hi2 < s2len && isDigit(s2[hi2]) == isDigit2 {
			hi2++
		}
		tok2 := s2[lo2:hi2]

		if isDigit1 && isDigit2 {
			num1, err1 := strconv.Atoi(tok1)
			num2, err2 := strconv.Atoi(tok2)
			if err1 == nil && err2 == nil {
				if num1 != num2 {
					return cmp.Compare(num1, num2)
				} else if len(tok1) != len(tok2) {
					return cmp.Compare(len(tok1), len(tok2))
				}
			}
		}

		if tok1 != tok2 {
			return cmp.Compare(tok1, tok2)
		}
	}
}

// --- os.go, nav.go --------------------------------------------------------------

func isHidden(f os.FileInfo, path string, hiddenfiles []string) bool {
	hidden := false
	for _, pattern := range hiddenfiles {
		if matchPattern(strings.TrimPrefix(pattern, "!"), f.Name(), path) {
			hidden = !strings.HasPrefix(pattern, "!")
		}
	}
	return hidden
}

func matchPattern(pattern, name, path string) bool {
	s := name

	pattern = replaceTilde(pattern)

	if filepath.IsAbs(pattern) {
		s = filepath.Join(path, name)
	}

	// pattern errors are checked when 'hiddenfiles' option is set
	matched, _ := filepath.Match(pattern, s)

	return matched
}

func (dir *dir) normalize(s string) string {
	if dir.sortignorecase {
		s = strings.ToLower(s)
	}
	if dir.sortignoredia {
		s = removeDiacritics(s)
	}
	return s
}

// compareFunc is the comparison applySort uses for the current settings.
func (dir *dir) compareFunc() func(f1, f2 *file) int {
	normalize := dir.normalize
	if dir.cacheNames && (dir.sortby == naturalSort || dir.sortby == nameSort) {
		cache := make(map[*file]string, len(dir.files))
		for _, f := range dir.files {
			cache[f] = dir.normalize(f.Name())
		}
		if dir.sortby == naturalSort {
			return func(f1, f2 *file) int { return naturalCmp(cache[f1], cache[f2]) }
		}
		return func(f1, f2 *file) int { return cmp.Compare(cache[f1], cache[f2]) }
	}
	switch dir.sortby {
	case naturalSort:
		return func(f1, f2 *file) int {
			return naturalCmp(normalize(f1.Name()), normalize(f2.Name()))
		}
	case nameSort:
		return func(f1, f2 *file) int {
			return cmp.Compare(normalize(f1.Name()), normalize(f2.Name()))
		}
	case sizeSort:
		sizeVal := func(f *file) int64 {
			if f.IsDir() && dir.dircounts {
				return int64(f.dirCount)
			}
			if f.dirSize >= 0 {
				return f.dirSize
			}
			return f.Size()
		}
		return func(f1, f2 *file) int {
			return cmp.Compare(sizeVal(f1), sizeVal(f2))
		}
	case timeSort:
		return func(f1, f2 *file) int {
			return f1.ModTime().Compare(f2.ModTime())
		}
	}
	return nil
}

func (dir *dir) sort() {
	dir.files = dir.allFiles

	applyFilter := func(fn func(f *file) bool) {
		slices.SortStableFunc(dir.files, func(i, j *file) int {
			switch {
			case !fn(i) && fn(j):
				return -1
			case fn(i) && !fn(j):
				return 1
			default:
				return 0
			}
		})

		i := slices.IndexFunc(dir.files, fn)
		if i == -1 {
			i = len(dir.files)
		}
		dir.files = dir.files[i:]
	}

	if dir.dironly {
		applyFilter(func(f *file) bool { return f.IsDir() })
	}

	if !dir.hidden {
		applyFilter(func(f *file) bool { return !isHidden(f, dir.path, dir.hiddenfiles) })
	}

	if len(dir.filter) != 0 {
		applyFilter(func(f *file) bool { return false })
	}

	applySort := func(fn func(f1, f2 *file) int) {
		if !dir.reverse {
			slices.SortStableFunc(dir.files, fn)
		} else {
			slices.SortStableFunc(dir.files, func(f1, f2 *file) int { return fn(f2, f1) })
		}
	}

	applySort(dir.compareFunc())

	if dir.dirfirst || (dir.sortby == sizeSort && dir.dircounts) {
		slices.SortStableFunc(dir.files, func(f1, f2 *file) int {
			switch {
			case f1.IsDir() && !f2.IsDir():
				return -1
			case !f1.IsDir() && f2.IsDir():
				return 1
			default:
				return 0
			}
		})
	}
}

// --- benchmark -------------------------------------------------------------------

func unescape(field string) string {
	var b strings.Builder
	for i := 0; i < len(field); i++ {
		if field[i] == '\\' && i+1 < len(field) {
			i++
			switch field[i] {
			case 't':
				b.WriteByte('\t')
			case 'n':
				b.WriteByte('\n')
			default:
				b.WriteByte(field[i])
			}
		} else {
			b.WriteByte(field[i])
		}
	}
	return b.String()
}

func load(path string) ([]*file, []string) {
	f, err := os.Open(path)
	if err != nil {
		panic(err)
	}
	defer f.Close()
	var files []*file
	var rel []string
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 1<<20), 1<<20)
	scanner.Scan() // header
	for scanner.Scan() {
		fields := strings.Split(scanner.Text(), "\t")
		if len(fields) != 7 {
			panic("bad row in " + path)
		}
		size, _ := strconv.ParseInt(fields[4], 10, 64)
		mtime, _ := strconv.ParseInt(fields[5], 10, 64)
		atime, _ := strconv.ParseInt(fields[6], 10, 64)
		mode := fs.FileMode(0o644)
		if fields[2] == "1" {
			mode = fs.ModeDir | 0o755
		}
		name := unescape(fields[1])
		p := "/bench/" + unescape(fields[0])
		files = append(files, &file{
			FileInfo:   &fakeStat{name: name, size: size, mode: mode, modTime: time.Unix(0, mtime)},
			path:       p,
			dirCount:   -1,
			dirSize:    -1,
			accessTime: time.Unix(0, atime),
			birthTime:  time.Unix(0, mtime),
			changeTime: time.Unix(0, mtime),
			ext:        filepath.Ext(name),
		})
		rel = append(rel, unescape(fields[0]))
	}
	return files, rel
}

func newDir(files []*file) *dir {
	return &dir{
		path:           "/bench",
		allFiles:       files,
		files:          files,
		dirfirst:       true,
		hidden:         true,
		hiddenfiles:    []string{".*"},
		sortignorecase: true,
		sortignoredia:  true,
	}
}

// fullCompare is the order dir.sort produces: folders first, then the column.
func (dir *dir) fullCompare() func(f1, f2 *file) int {
	fn := dir.compareFunc()
	return func(f1, f2 *file) int {
		if f1.IsDir() != f2.IsDir() {
			if f1.IsDir() {
				return -1
			}
			return 1
		}
		if dir.reverse {
			return fn(f2, f1)
		}
		return fn(f1, f2)
	}
}

func median(v []float64) float64 {
	sort.Float64s(v)
	return v[len(v)/2]
}

func main() {
	repeats := flag.Int("repeats", 5, "repeats per measurement")
	csvPath := flag.String("csv", "", "CSV output")
	flag.Parse()
	if flag.NArg() == 0 {
		fmt.Fprintln(os.Stderr, "usage: lf_bench [-repeats N] [-csv FILE] DATASET.tsv...")
		os.Exit(2)
	}
	out := "project,dataset,n,role,order,fm_resort_ms,fm_resort_default_ms,fm_resort_cached_ms,ties,order_hash\n"
	type role struct {
		by   sortMethod
		name string
	}
	roles := []role{{naturalSort, "name"}, {sizeSort, "size"}, {timeSort, "modified"}}
	for _, path := range flag.Args() {
		files, rel := load(path)
		relOf := make(map[*file]string, len(files))
		for i, f := range files {
			relOf[f] = rel[i]
		}
		base := filepath.Base(path)
		dataset := strings.SplitN(base, ".", 2)[0]
		n := len(files)
		reps := *repeats
		if n > 300000 {
			reps = max(1, reps/3)
		}
		fmt.Printf("\n%s: %d entries\n", dataset, n)
		for r := range roles {
			for _, reverse := range []bool{false, true} {
				measure := func(hidden, cached bool) (float64, []*file, *dir) {
					var times []float64
					var d *dir
					for i := 0; i < reps; i++ {
						d = newDir(slices.Clone(files))
						d.hidden = hidden
						d.cacheNames = cached
						d.sortby = roles[(r+1)%len(roles)].by // the previous column
						d.reverse = false
						d.sort()
						d.allFiles = slices.Clone(d.allFiles) // keep the previous order for the next sort
						d.sortby = roles[r].by
						d.reverse = reverse
						t := time.Now()
						d.sort()
						times = append(times, float64(time.Since(t).Nanoseconds())/1e6)
					}
					return median(times), d.files, d
				}
				ms, result, d := measure(true, false)
				defaultMs, _, _ := measure(false, false)
				cachedMs, cachedResult, _ := measure(true, true)
				if !slices.Equal(result, cachedResult) {
					panic("normalizing names once changed the order")
				}
				full := d.fullCompare()
				ties := 0
				for i := 1; i < len(result); i++ {
					c := full(result[i-1], result[i])
					if c > 0 {
						panic("lf's sort did not sort under its own comparison")
					}
					if c == 0 {
						ties++
					}
				}
				var h uint64 = 0xcbf29ce484222325
				for _, f := range result {
					for _, c := range []byte(relOf[f]) {
						h = (h ^ uint64(c)) * 0x100000001b3
					}
					h = (h ^ '\n') * 0x100000001b3
				}
				order := "asc"
				if reverse {
					order = "desc"
				}
				fmt.Printf("  lf       %-8s %-4s dir.sort %9.3f ms (hidden files hidden: %9.3f ms; names normalized once: %9.3f ms; ties %d)\n",
					roles[r].name, order, ms, defaultMs, cachedMs, ties)
				out += fmt.Sprintf("lf,%s,%d,%s,%s,%.4f,%.4f,%.4f,%d,%016x\n", dataset, n, roles[r].name, order, ms, defaultMs, cachedMs, ties, h)
			}
		}
	}
	if *csvPath != "" {
		if err := os.WriteFile(*csvPath, []byte(out), 0o644); err != nil {
			panic(err)
		}
	}
}
