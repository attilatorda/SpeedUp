//! yazi's real sort path, natively in Rust, on the Bucketization datasets.
//!
//! `FilesSorter::sort` below is yazi's `yazi-fs/src/sorter.rs` (commit 40a9142,
//! MIT, Copyright (c) 2023 - sxyazi) with yazi's `File` reduced to the fields it
//! reads; the comparisons are unchanged. yazi's defaults: alphabetical, case
//! insensitive, folders first, alphabetical fallback.
//!
//! Usage: yazi_bench [--repeats N] [--csv FILE] DATASET.tsv...
//! Prints and writes: project,dataset,n,role,order,fm_resort_ms,ties,order_hash

use std::cmp::Ordering;
use std::fs;
use std::io::Write;
use std::time::{Duration, Instant, SystemTime};

// --- yazi's File, reduced -----------------------------------------------------

/// Like yazi's `File` (url + stat + extra): moved as a whole by the sort.
#[derive(Clone)]
#[allow(dead_code)]
struct File {
	url:       Vec<u8>, // the path; yazi's UrlBuf keeps the file name's offset, so urn() is O(1)
	urn_start: usize,
	kind:      u8,
	mode:      u16,
	len:       u64,
	atime:     Option<SystemTime>,
	btime:     Option<SystemTime>,
	ctime:     Option<SystemTime>,
	mtime:     Option<SystemTime>,
	dev:       u64,
	uid:       u32,
	gid:       u32,
	nlink:     u64,
	extra:     Option<Box<[u8]>>,
}

impl File {
	#[inline(always)]
	fn is_dir(&self) -> bool { self.kind == 1 }
	#[inline(always)]
	fn urn(&self) -> &[u8] { &self.url[self.urn_start..] }
}

#[derive(Clone, Copy, PartialEq)]
#[allow(dead_code)]
enum SortBy {
	None,
	Mtime,
	Alphabetical,
	Size,
}

#[derive(Clone, Copy, PartialEq)]
#[allow(dead_code)]
enum SortFallback {
	Alphabetical,
}

// --- yazi-fs/src/sorter.rs (the arms benchmarked here) ------------------------

#[derive(Clone, Copy)]
struct FilesSorter {
	by:        SortBy,
	sensitive: bool,
	reverse:   bool,
	dir_first: bool,
	fallback:  SortFallback,
}

impl FilesSorter {
	fn sort(&self, items: &mut [File]) {
		if items.is_empty() {
			return;
		}

		macro_rules! promote {
			($a:ident, $b:ident) => {
				if self.dir_first {
					match $b.is_dir().cmp(&$a.is_dir()) {
						Ordering::Equal => {}
						not_eq => return not_eq,
					}
				}
			};
		}

		match self.by {
			SortBy::None => {}
			SortBy::Mtime => items.sort_unstable_by(|a, b| {
				promote!(a, b);
				self.fallback(a, b, self.cmp(a.mtime, b.mtime))
			}),
			SortBy::Alphabetical => items.sort_unstable_by(|a, b| {
				promote!(a, b);
				self.fallback(a, b, self.sort_alphabetically(a, b))
			}),
			SortBy::Size => items.sort_unstable_by(|a, b| {
				promote!(a, b);
				// Folder sizes are only known after yazi computes them; none here.
				let aa: Option<u64> = None;
				let bb: Option<u64> = None;
				self.fallback(a, b, self.cmp(aa.unwrap_or(a.len), bb.unwrap_or(b.len)))
			}),
		}
	}

	/// The same comparison as `sort` uses, for counting ties.
	fn compare(&self, a: &File, b: &File) -> Ordering {
		if self.dir_first {
			match b.is_dir().cmp(&a.is_dir()) {
				Ordering::Equal => {}
				not_eq => return not_eq,
			}
		}
		match self.by {
			SortBy::None => Ordering::Equal,
			SortBy::Mtime => self.fallback(a, b, self.cmp(a.mtime, b.mtime)),
			SortBy::Alphabetical => self.fallback(a, b, self.sort_alphabetically(a, b)),
			SortBy::Size => self.fallback(a, b, self.cmp(a.len, b.len)),
		}
	}

	#[inline(always)]
	fn sort_alphabetically(&self, a: &File, b: &File) -> Ordering {
		if self.sensitive {
			self.cmp(a.urn(), b.urn())
		} else {
			self.cmp_insensitive(a.urn(), b.urn())
		}
	}

	#[inline(always)]
	fn fallback(&self, a: &File, b: &File, ord: Ordering) -> Ordering {
		if ord != Ordering::Equal {
			return ord;
		}
		match self.fallback {
			SortFallback::Alphabetical => self.cmp(a.urn(), b.urn()),
		}
	}

	#[inline(always)]
	fn cmp<T: Ord>(&self, a: T, b: T) -> Ordering { if self.reverse { b.cmp(&a) } else { a.cmp(&b) } }

	#[inline(always)]
	fn cmp_insensitive(&self, a: &[u8], b: &[u8]) -> Ordering {
		let l = a.len().min(b.len());
		let (lhs, rhs) = if self.reverse { (&b[..l], &a[..l]) } else { (&a[..l], &b[..l]) };

		for i in 0..l {
			match lhs[i].to_ascii_lowercase().cmp(&rhs[i].to_ascii_lowercase()) {
				Ordering::Equal => {}
				not_eq => return not_eq,
			}
		}

		if self.reverse { b.len().cmp(&a.len()) } else { a.len().cmp(&b.len()) }
	}
}

// --- datasets ----------------------------------------------------------------

fn unescape(field: &str) -> Vec<u8> {
	let bytes = field.as_bytes();
	let mut out = Vec::with_capacity(bytes.len());
	let mut i = 0;
	while i < bytes.len() {
		if bytes[i] == b'\\' && i + 1 < bytes.len() {
			i += 1;
			out.push(match bytes[i] {
				b't' => b'\t',
				b'n' => b'\n',
				c => c,
			});
		} else {
			out.push(bytes[i]);
		}
		i += 1;
	}
	out
}

fn load(path: &str) -> Vec<File> {
	let text = fs::read_to_string(path).unwrap_or_else(|e| panic!("cannot read {path}: {e}"));
	let mut files = Vec::new();
	for line in text.lines().skip(1) {
		let f: Vec<&str> = line.split('\t').collect();
		assert_eq!(f.len(), 7, "bad row in {path}: {line}");
		let rel = unescape(f[0]);
		let name = unescape(f[1]);
		let mut url = b"/bench/".to_vec();
		url.extend_from_slice(&rel);
		let urn_start = url.len() - name.len();
		let mtime_ns: i64 = f[5].parse().unwrap();
		let atime_ns: i64 = f[6].parse().unwrap();
		let time = |ns: i64| {
			Some(if ns >= 0 {
				SystemTime::UNIX_EPOCH + Duration::from_nanos(ns as u64)
			} else {
				SystemTime::UNIX_EPOCH - Duration::from_nanos(ns.unsigned_abs())
			})
		};
		files.push(File {
			url,
			urn_start,
			kind: if f[2] == "1" { 1 } else { 0 },
			mode: 0o644,
			len: f[4].parse().unwrap(),
			atime: time(atime_ns),
			btime: time(mtime_ns),
			ctime: time(mtime_ns),
			mtime: time(mtime_ns),
			dev: 2049,
			uid: 1000,
			gid: 1000,
			nlink: 1,
			extra: None,
		});
	}
	files
}

fn dataset_name(path: &str) -> String {
	let base = path.rsplit(['/', '\\']).next().unwrap_or(path);
	base.split('.').next().unwrap_or(base).to_string()
}

/// FNV-1a over the paths in order, '\n' separated (same as fm_harness.h).
fn order_hash(items: &[File]) -> u64 {
	let mut h: u64 = 0xcbf29ce484222325;
	for f in items {
		for &c in &f.url[b"/bench/".len()..] {
			h = (h ^ c as u64).wrapping_mul(0x100000001b3);
		}
		h = (h ^ b'\n' as u64).wrapping_mul(0x100000001b3);
	}
	h
}

fn median(mut v: Vec<f64>) -> f64 {
	v.sort_by(|a, b| a.partial_cmp(b).unwrap());
	v[v.len() / 2]
}

fn sorter(by: SortBy, reverse: bool) -> FilesSorter {
	FilesSorter { by, sensitive: false, reverse, dir_first: true, fallback: SortFallback::Alphabetical }
}

fn main() {
	let mut repeats = 5;
	let mut csv: Option<String> = None;
	let mut datasets = Vec::new();
	let mut args = std::env::args().skip(1);
	while let Some(a) = args.next() {
		match a.as_str() {
			"--repeats" => repeats = args.next().unwrap().parse().unwrap(),
			"--csv" => csv = args.next(),
			_ => datasets.push(a),
		}
	}
	if datasets.is_empty() {
		eprintln!("usage: yazi_bench [--repeats N] [--csv FILE] DATASET.tsv...");
		std::process::exit(2);
	}
	println!("yazi (Rust, native): File is {} bytes", std::mem::size_of::<File>());
	let mut out = String::from("project,dataset,n,role,order,fm_resort_ms,ties,order_hash\n");
	let roles = [(SortBy::Alphabetical, "name"), (SortBy::Size, "size"), (SortBy::Mtime, "modified")];
	for path in &datasets {
		let listing = load(path);
		let name = dataset_name(path);
		let n = listing.len();
		let reps = if n > 300_000 { (repeats / 3).max(1) } else { repeats };
		println!("\n{name}: {n} entries");
		for r in 0..3 {
			for reverse in [false, true] {
				let (by, role) = roles[r];
				let mut previous = listing.clone();
				sorter(roles[(r + 1) % 3].0, false).sort(&mut previous);
				let s = sorter(by, reverse);
				let mut times = Vec::new();
				let mut result = Vec::new();
				for _ in 0..reps {
					let mut items = previous.clone();
					let t = Instant::now();
					s.sort(&mut items);
					times.push(t.elapsed().as_secs_f64() * 1000.0);
					result = items;
				}
				let ties = result.windows(2).filter(|w| s.compare(&w[0], &w[1]) == Ordering::Equal).count();
				let sorted = result.windows(2).all(|w| s.compare(&w[0], &w[1]) != Ordering::Greater);
				assert!(sorted, "yazi's sort did not sort under its own comparison");
				let ms = median(times);
				let order = if reverse { "desc" } else { "asc" };
				println!("  yazi     {role:<8} {order:<4} sort_unstable_by {ms:9.3} ms (ties {ties})");
				out.push_str(&format!("yazi,{name},{n},{role},{order},{ms:.4},{ties},{:016x}\n", order_hash(&result)));
			}
		}
	}
	if let Some(path) = csv {
		fs::File::create(&path).and_then(|mut f| f.write_all(out.as_bytes())).expect("cannot write csv");
	}
}
