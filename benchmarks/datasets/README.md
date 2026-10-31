# Bucketization benchmark datasets

Directory listings (names and metadata only, no file contents) used by the
file-manager benchmarks. Format and capture tool: `capture_listing.py`.

## Real listings (`real/*.tsv.gz`)

Captured on 2026-10-24 in an Ubuntu 24.04 container.

| Dataset | Entries | What it is | How it was captured |
|---------|--------:|------------|---------------------|
| `usr-bin` | 1,172 | one real folder: `/usr/bin` | `capture_listing.py dir /usr/bin` |
| `usr-lib` | 1,533 | one real folder: `/usr/lib/x86_64-linux-gnu` (libraries, symlinks, many identical package-install timestamps) | `capture_listing.py dir /usr/lib/x86_64-linux-gnu` |
| `node-modules-top` | 395 | top level of a real `node_modules` | `capture_listing.py dir webapp/node_modules` |
| `node-modules-tree` | 36,069 | the whole `node_modules` tree flattened, like a recursive search result (many repeated names: `index.js`, `package.json`) | `capture_listing.py dir webapp/node_modules --recursive` |
| `usr-tree` | 97,974 | all of `/usr` flattened | `capture_listing.py dir /usr --recursive` |
| `linux-tree` | 97,005 | every file of the Linux kernel tree, commit `5b0764a53235d3c77e1ed674e836e1b3fe1a4b1f` (2026-10-23); real names and sizes, no timestamps (git stores none, so every time is 0: the "freshly extracted archive" case) | `git clone --depth 1 --no-checkout https://github.com/torvalds/linux` then `capture_listing.py git linux` |

The `node_modules` tree comes from `npm install` of the dependencies in
`real/node-modules-package.json` (a typical React/Next.js/webpack web app:
754 packages).

## Synthetic listings (generated, not stored)

Real camera rolls are private, so these are generated deterministically:

| Dataset | Entries | Command |
|---------|--------:|---------|
| `photos-100k` | 100,000 | `capture_listing.py photos 100000` |
| `photos-1m` | 1,000,000 | `capture_listing.py photos 1000000` |

Names follow phone and camera conventions (`IMG_20240101_123456.jpg`,
`PXL_…`, `DSC01234.JPG`, `VID_…`), timestamps increase in bursts, sizes are
JPEG-like (log-normal around 2.7 MB).

## Person names (`names/*.tsv.gz`)

For the name sort benchmark (`docs/sorting/name-sort.md`). These are US
Census Bureau data (public domain), written by
`codegen/name_tables/build_name_tables.py`:

| File | Entries | Columns |
|------|--------:|---------|
| `names/surnames.tsv.gz` | 162,253 | surname (title case), people with it (2010 Census) |
| `names/first-names.tsv.gz` | 5,163 | first name, share among first names (1990 Census, female and male averaged) |

Synthetic name lists are drawn from them and are not stored:
`capture_names.py 1000000 -o names-1m.txt`. Each line is
"Surname Firstname", both drawn by frequency, with a fixed seed.

Real-name lists, not stored either: `census_name_lists.py OUT_DIR` writes the
162,253 Census surnames in five orders (shuffled, popularity rank, sorted,
reversed, ALL CAPS) and 1M multilingual names from `capture_listing.py intl`.
Results: `benchmarks/outputs/namesort_real_*.csv`.

## Your own folders

```bash
python3 benchmarks/datasets/capture_listing.py dir ~/Pictures -o pictures.tsv
python3 benchmarks/scripts/run_bucketization_benchmarks.py --build build --extra pictures.tsv
```

The listing stays on your machine; only names, sizes and times are read.
