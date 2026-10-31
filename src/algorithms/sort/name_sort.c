/* Name sort: a bucket sort for person names (docs/sorting/name-sort.md).
 *
 * 1. Sample n/128 names (256 to 16384) and sort the sample.
 * 2. Plan: a trie over the first three symbols of the names (symbol 0 =
 *    end of name, 1 + b = byte b). A prefix p is expected to hold n * P(p)
 *    names. P comes from the sample, blended with priors from US Census
 *    name data (name_freq_tables.h), one table per position for positions
 *    1, 2 and 3. A prefix expected to hold more than a bucket's worth, and
 *    seen in the sample, splits on its next position. The leaves, in order,
 *    are packed into buckets of about `target` names.
 * 3. Count the names per bucket, scatter them, and finish every bucket:
 *    comparison sort up to NS_SMALL names, MSD radix sort above. Both skip
 *    the bytes the bucket's prefix already fixes.
 *
 * Measured choices (docs/sorting/name-sort.md, "Tuning"): a deeper trie
 * gives buckets closer to the target size but is slower, because the trie
 * lookups cost more than the bucket sorts save; below NS_PLAN_MIN names
 * MSD radix sort alone is faster than planning. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "speedup/algorithms/sort.h"
#include "name_freq_tables.h"

#define NS_END 0u
#define NS_DEFAULT_TARGET 32
#define NS_SAMPLE_MIN 256         /* sample at least 256 names ... */
#define NS_SAMPLE_MAX 16384       /* ... and at most 16384 */
#define NS_ALL_WEIGHT 0.01        /* share of the any-position table in the prior */
#define NS_SMALL 32               /* up to this, sort by comparison */
#define NS_CHILD 0x80000000u      /* trie entry: child block (else: bucket or leaf id) */

/* The measured choices. Each can be overridden at compile time (-DNS_...)
 * for the ablation study: benchmarks/scripts/run_sort_benchmarks.py ablation. */
#ifndef NS_MAX_DEPTH
#define NS_MAX_DEPTH 3            /* trie levels; deeper levels use the any-position prior */
#endif
#ifndef NS_PLAN_MIN
#define NS_PLAN_MIN 131072        /* below this, MSD radix sort without a plan */
#endif
#ifndef NS_SAMPLE_SHARE
#define NS_SAMPLE_SHARE 128       /* sample n / 128 names */
#endif
#ifndef NS_MAX_BLOCKS
#define NS_MAX_BLOCKS 1024        /* 1 MB of lookup tables at most */
#endif
#ifndef NS_PRIOR_WEIGHT
#define NS_PRIOR_WEIGHT 16.0      /* the prior counts as this many sampled names */
#endif
#ifndef NS_RADIX_ABOVE
#define NS_RADIX_ABOVE NS_SMALL   /* buckets above this size are finished by MSD radix sort */
#endif

/* ---- order ------------------------------------------------------------- */

static inline unsigned ns_fold(unsigned char c, int caseless) {
    return (caseless && c >= 'A' && c <= 'Z') ? (unsigned)c + 32u : (unsigned)c;
}

static inline unsigned ns_sym(const speedup_str_t* s, size_t pos, int caseless) {
    return pos < s->len ? 1u + ns_fold((unsigned char)s->data[pos], caseless) : NS_END;
}

/* Compares from byte `from` on; both names must agree before it (in the
 * folded order when caseless). */
static int ns_compare_from(const speedup_str_t* a, const speedup_str_t* b, size_t from, int caseless) {
    const size_t n = a->len < b->len ? a->len : b->len;
    if (!caseless) {
        if (n > from) {
            int r = memcmp(a->data + from, b->data + from, n - from);
            if (r) return r < 0 ? -1 : 1;
        }
        return (a->len > b->len) - (a->len < b->len);
    }
    for (size_t i = from; i < n; i++) {
        unsigned x = ns_fold((unsigned char)a->data[i], 1), y = ns_fold((unsigned char)b->data[i], 1);
        if (x != y) return x < y ? -1 : 1;
    }
    if (a->len != b->len) return a->len < b->len ? -1 : 1;
    /* equal up to case: byte order decides */
    if (n) {
        int r = memcmp(a->data, b->data, n);
        if (r) return r < 0 ? -1 : 1;
    }
    return 0;
}

int speedup_name_compare(speedup_str_t a, speedup_str_t b, uint32_t flags) {
    return ns_compare_from(&a, &b, 0, (flags & SPEEDUP_NAME_SORT_ASCII_CASELESS) != 0);
}

/* ---- comparison sort (introsort) ---------------------------------------- */

static void ns_insertion(speedup_str_t* a, size_t n, size_t from, int caseless) {
    for (size_t i = 1; i < n; i++) {
        speedup_str_t v = a[i];
        size_t j = i;
        while (j > 0 && ns_compare_from(&a[j - 1], &v, from, caseless) > 0) {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = v;
    }
}

static void ns_sift(speedup_str_t* a, size_t root, size_t n, size_t from, int caseless) {
    speedup_str_t v = a[root];
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= n) break;
        if (child + 1 < n && ns_compare_from(&a[child + 1], &a[child], from, caseless) > 0) child++;
        if (ns_compare_from(&a[child], &v, from, caseless) <= 0) break;
        a[root] = a[child];
        root = child;
    }
    a[root] = v;
}

static void ns_heapsort(speedup_str_t* a, size_t n, size_t from, int caseless) {
    for (size_t i = n / 2; i-- > 0;) ns_sift(a, i, n, from, caseless);
    for (size_t end = n; end-- > 1;) {
        speedup_str_t t = a[0]; a[0] = a[end]; a[end] = t;
        ns_sift(a, 0, end, from, caseless);
    }
}

static void ns_introsort_loop(speedup_str_t* a, size_t n, size_t from, int caseless, unsigned depth) {
    while (n > 16) {
        if (depth == 0) {
            ns_heapsort(a, n, from, caseless);
            return;
        }
        depth--;
        const speedup_str_t* x = &a[0];
        const speedup_str_t* y = &a[n / 2];
        const speedup_str_t* z = &a[n - 1];
        speedup_str_t p;
        if (ns_compare_from(x, y, from, caseless) < 0) {
            p = ns_compare_from(y, z, from, caseless) < 0 ? *y : (ns_compare_from(x, z, from, caseless) < 0 ? *z : *x);
        } else {
            p = ns_compare_from(x, z, from, caseless) < 0 ? *x : (ns_compare_from(y, z, from, caseless) < 0 ? *z : *y);
        }
        size_t i = 0, j = n - 1;
        for (;;) {
            while (ns_compare_from(&a[i], &p, from, caseless) < 0) i++;
            while (ns_compare_from(&p, &a[j], from, caseless) < 0) j--;
            if (i >= j) break;
            speedup_str_t t = a[i]; a[i] = a[j]; a[j] = t;
            i++;
            j--;
        }
        size_t left = j + 1;
        if (left < n - left) {
            ns_introsort_loop(a, left, from, caseless, depth);
            a += left;
            n -= left;
        } else {
            ns_introsort_loop(a + left, n - left, from, caseless, depth);
            n = left;
        }
    }
    ns_insertion(a, n, from, caseless);
}

static void ns_introsort(speedup_str_t* a, size_t n, size_t from, int caseless) {
    unsigned depth = 0;
    for (size_t m = n; m > 1; m >>= 1) depth += 2;
    if (n > 1) ns_introsort_loop(a, n, from, caseless, depth);
}

/* ---- MSD radix sort: small inputs, and buckets above NS_SMALL ----------- */

typedef struct ns_task {
    size_t lo, n, depth;
} ns_task;

/* Sorts a[0..n), whose names agree on their first `depth` symbols.
 * scratch and syms hold n entries each. Out of memory, the rest is sorted
 * by comparison instead. */
static void ns_msd(speedup_str_t* a, size_t n, size_t depth, int caseless, speedup_str_t* scratch, uint32_t* syms) {
    size_t cap = 64, top = 0;
    ns_task* stack = (ns_task*)malloc(cap * sizeof(ns_task));
    if (!stack) {
        ns_introsort(a, n, depth, caseless);
        return;
    }
    stack[top++] = (ns_task){0, n, depth};
    while (top) {
        ns_task t = stack[--top];
        speedup_str_t* r = a + t.lo;
        if (t.n <= NS_SMALL) {
            ns_introsort(r, t.n, t.depth, caseless);
            continue;
        }
        size_t count[SPEEDUP_NAME_SYMBOLS] = {0};
        for (size_t i = 0; i < t.n; i++) {
            unsigned s = ns_sym(&r[i], t.depth, caseless);
            syms[t.lo + i] = s;
            count[s]++;
        }
        unsigned only = syms[t.lo];
        if (count[only] == t.n) {
            /* one symbol for all: nothing moves */
            if (only == NS_END) {
                if (caseless) ns_introsort(r, t.n, t.depth, caseless); /* equal up to case */
                continue;
            }
            t.depth++;
            stack[top++] = t;
            continue;
        }
        size_t offset[SPEEDUP_NAME_SYMBOLS];
        size_t sum = 0;
        for (unsigned s = 0; s < SPEEDUP_NAME_SYMBOLS; s++) {
            offset[s] = sum;
            sum += count[s];
        }
        for (size_t i = 0; i < t.n; i++) scratch[t.lo + offset[syms[t.lo + i]]++] = r[i];
        memcpy(r, scratch + t.lo, t.n * sizeof(speedup_str_t));
        size_t begin = 0;
        for (unsigned s = 0; s < SPEEDUP_NAME_SYMBOLS; s++) {
            size_t c = count[s];
            if (c > 1) {
                if (s == NS_END) {
                    if (caseless) ns_introsort(r + begin, c, t.depth, caseless);
                } else {
                    if (top == cap) {
                        ns_task* grown = (ns_task*)realloc(stack, 2 * cap * sizeof(ns_task));
                        if (!grown) {
                            ns_introsort(r + begin, c, t.depth + 1, caseless);
                            begin += c;
                            continue;
                        }
                        stack = grown;
                        cap *= 2;
                    }
                    stack[top++] = (ns_task){t.lo + begin, c, t.depth + 1};
                }
            }
            begin += c;
        }
    }
    free(stack);
}

/* ---- plan ----------------------------------------------------------------- */

typedef struct ns_leaf {
    double mass;
    unsigned char depth;          /* symbols in the path */
    uint16_t lo, hi;              /* the leaf covers symbols lo..hi after the path */
    uint16_t path[NS_MAX_DEPTH];
} ns_leaf;

typedef struct ns_plan {
    uint32_t (*blocks)[SPEEDUP_NAME_SYMBOLS];
    size_t nblocks, cap_blocks;
    ns_leaf* leaves;
    size_t nleaves, cap_leaves;
    /* priors per position: positions 1..3, then one row for every later one */
    double prior[SPEEDUP_NAME_PRIOR_POSITIONS + 1][SPEEDUP_NAME_SYMBOLS];
    const speedup_str_t* sample;
    double n;
    double target;
    int caseless;
    int failed;
} ns_plan;

static void ns_priors(ns_plan* p) {
    for (int d = 0; d <= SPEEDUP_NAME_PRIOR_POSITIONS; d++) {
        double* row = p->prior[d];
        for (unsigned s = 0; s < SPEEDUP_NAME_SYMBOLS; s++) {
            const double all = speedup_name_prior_all[s];
            row[s] = d < SPEEDUP_NAME_PRIOR_POSITIONS
                         ? (1.0 - NS_ALL_WEIGHT) * speedup_name_prior_pos[d][s] + NS_ALL_WEIGHT * all
                         : all;
        }
        if (p->caseless) {
            for (unsigned c = 'A'; c <= 'Z'; c++) {
                row[1 + c + 32] += row[1 + c];
                row[1 + c] = 0.0;
            }
        }
        for (unsigned s = 0; s < SPEEDUP_NAME_SYMBOLS; s++) row[s] /= SPEEDUP_NAME_PRIOR_TOTAL;
    }
}

static size_t ns_new_block(ns_plan* p) {
    if (p->nblocks == p->cap_blocks) {
        size_t cap = p->cap_blocks ? 2 * p->cap_blocks : 16;
        void* grown = realloc(p->blocks, cap * sizeof *p->blocks);
        if (!grown) {
            p->failed = 1;
            return 0;
        }
        p->blocks = (uint32_t (*)[SPEEDUP_NAME_SYMBOLS])grown;
        p->cap_blocks = cap;
    }
    return p->nblocks++;
}

static uint32_t ns_new_leaf(ns_plan* p, const uint16_t* path, unsigned depth, unsigned sym, double mass) {
    if (p->nleaves == p->cap_leaves) {
        size_t cap = p->cap_leaves ? 2 * p->cap_leaves : 1024;
        ns_leaf* grown = (ns_leaf*)realloc(p->leaves, cap * sizeof(ns_leaf));
        if (!grown) {
            p->failed = 1;
            return 0;
        }
        p->leaves = grown;
        p->cap_leaves = cap;
    }
    ns_leaf* leaf = &p->leaves[p->nleaves];
    leaf->mass = mass;
    leaf->depth = (unsigned char)depth;
    leaf->lo = leaf->hi = (uint16_t)sym;
    for (unsigned i = 0; i < depth; i++) leaf->path[i] = path[i];
    return (uint32_t)p->nleaves++;
}

/* Fills block `block`, the node of `path` (depth symbols long), which is
 * expected to hold a share `mass` of the names; sample[lo, hi) are the
 * sampled names with that path. Symbols that are not split share a leaf
 * with their left neighbour while the two together stay within a bucket,
 * so the 257 entries of a block need only a few leaves. */
static void ns_split(ns_plan* p, size_t block, uint16_t* path, unsigned depth, double mass, size_t lo, size_t hi) {
    const double total = (double)(hi - lo);
    const double* prior = p->prior[depth < SPEEDUP_NAME_PRIOR_POSITIONS ? depth : SPEEDUP_NAME_PRIOR_POSITIONS];
    const uint32_t none = UINT32_MAX;
    uint32_t open = none;
    size_t i = lo;
    for (unsigned c = 0; c < SPEEDUP_NAME_SYMBOLS && !p->failed; c++) {
        size_t j = i;
        while (j < hi && ns_sym(&p->sample[j], depth, p->caseless) == c) j++;
        const double q = ((double)(j - i) + NS_PRIOR_WEIGHT * prior[c]) / (total + NS_PRIOR_WEIGHT);
        const double child_mass = mass * q;
        /* A prefix the sample never saw is not split: the prior alone says
         * too little about it, and if it is large after all, MSD radix
         * finishes its bucket. */
        if (c != NS_END && depth + 1 < NS_MAX_DEPTH && j > i && p->n * child_mass > p->target &&
            p->nblocks < NS_MAX_BLOCKS) {
            open = none;
            size_t child = ns_new_block(p);
            if (p->failed) return;
            p->blocks[block][c] = NS_CHILD | (uint32_t)child;
            path[depth] = (uint16_t)c;
            ns_split(p, child, path, depth + 1, child_mass, i, j);
        } else if (open != none && p->n * (p->leaves[open].mass + child_mass) <= p->target) {
            p->leaves[open].mass += child_mass;
            p->leaves[open].hi = (uint16_t)c;
            p->blocks[block][c] = open;
        } else {
            open = ns_new_leaf(p, path, depth, c, child_mass);
            p->blocks[block][c] = open;
        }
        i = j;
    }
}

static inline uint32_t ns_bucket_of(const ns_plan* p, const speedup_str_t* s) {
    uint32_t e = p->blocks[0][ns_sym(s, 0, p->caseless)];
    for (size_t d = 1; e & NS_CHILD; d++) e = p->blocks[e & ~NS_CHILD][ns_sym(s, d, p->caseless)];
    return e;
}

/* Bytes every name of a bucket shares: the common prefix of the bucket's
 * first key (first leaf, its lowest symbol) and last key (last leaf, its
 * highest symbol), up to the first end symbol. */
static unsigned char ns_shared(const ns_leaf* first, const ns_leaf* last) {
    const unsigned la = first->depth + 1u, lb = last->depth + 1u;
    unsigned k = 0;
    while (k < la && k < lb) {
        unsigned a = k < first->depth ? first->path[k] : first->lo;
        unsigned b = k < last->depth ? last->path[k] : last->hi;
        if (a != b || a == NS_END) break;
        k++;
    }
    return (unsigned char)k;
}

/* Small inputs: MSD radix sort alone beats building a plan. */
static void ns_sort_unplanned(speedup_str_t* names, size_t n, int caseless) {
    speedup_str_t* scratch = (speedup_str_t*)malloc(n * sizeof(speedup_str_t));
    uint32_t* syms = (uint32_t*)malloc(n * sizeof(uint32_t));
    if (scratch && syms) {
        ns_msd(names, n, 0, caseless, scratch, syms);
    } else {
        ns_introsort(names, n, 0, caseless);
    }
    free(scratch);
    free(syms);
}

/* ---- entry points ----------------------------------------------------------- */

void speedup_name_sort_ex(speedup_str_t* names, size_t n, uint32_t flags,
                          const speedup_name_sort_options_t* options, speedup_name_sort_stats_t* stats) {
    const int caseless = (flags & SPEEDUP_NAME_SORT_ASCII_CASELESS) != 0;
    const size_t target = (options && options->target_bucket) ? options->target_bucket : NS_DEFAULT_TARGET;
    if (stats) memset(stats, 0, sizeof *stats);
    if (n < 2) return;
    if (n < NS_PLAN_MIN || n < 4 * target) {
        ns_sort_unplanned(names, n, caseless);
        return;
    }
    size_t s = n / NS_SAMPLE_SHARE;
    s = s < NS_SAMPLE_MIN ? NS_SAMPLE_MIN : (s > NS_SAMPLE_MAX ? NS_SAMPLE_MAX : s);
    if (options && options->sample_size) s = options->sample_size;
    if (s > n) s = n;

    ns_plan plan;
    memset(&plan, 0, sizeof plan);
    plan.caseless = caseless;
    plan.n = (double)n;
    plan.target = (double)target;
    ns_priors(&plan);

    speedup_str_t* sample = (speedup_str_t*)malloc(s * sizeof(speedup_str_t));
    uint32_t* ids = (uint32_t*)malloc(n * sizeof(uint32_t));
    speedup_str_t* tmp = (speedup_str_t*)malloc(n * sizeof(speedup_str_t));
    size_t* start = NULL;
    size_t* next = NULL;
    unsigned char* shared = NULL;
    uint32_t* leaf_bucket = NULL;
    size_t buckets = 0;
    if (!sample || !ids || !tmp) goto fallback;

    for (size_t i = 0; i < s; i++) sample[i] = names[i * (n / s) + (i * (n % s)) / s];
    ns_sort_unplanned(sample, s, caseless);
    plan.sample = sample;
    {
        uint16_t path[NS_MAX_DEPTH];
        size_t root = ns_new_block(&plan);
        if (!plan.failed) ns_split(&plan, root, path, 0, 1.0, 0, s);
    }
    if (plan.failed) goto fallback;

    /* Pack the leaves, in order, into buckets of about `target` names. */
    leaf_bucket = (uint32_t*)malloc(plan.nleaves * sizeof(uint32_t));
    shared = (unsigned char*)malloc(plan.nleaves);
    if (!leaf_bucket || !shared) goto fallback;
    {
        /* Leaves can have zero mass (bytes no name has), so the bucket's
         * first leaf is tracked explicitly, not by the mass so far. */
        double acc = 0.0;
        size_t first = 0;
        for (size_t l = 0; l < plan.nleaves; l++) {
            leaf_bucket[l] = (uint32_t)buckets;
            acc += plan.leaves[l].mass;
            if (plan.n * acc >= plan.target || l + 1 == plan.nleaves) {
                shared[buckets] = ns_shared(&plan.leaves[first], &plan.leaves[l]);
                buckets++;
                acc = 0.0;
                first = l + 1;
            }
        }
    }
    for (size_t b = 0; b < plan.nblocks; b++) {
        for (unsigned c = 0; c < SPEEDUP_NAME_SYMBOLS; c++) {
            uint32_t e = plan.blocks[b][c];
            if (!(e & NS_CHILD)) plan.blocks[b][c] = leaf_bucket[e];
        }
    }

    start = (size_t*)calloc(buckets + 1, sizeof(size_t));
    next = (size_t*)malloc(buckets * sizeof(size_t));
    if (!start || !next) goto fallback;

    for (size_t i = 0; i < n; i++) {
        uint32_t b = ns_bucket_of(&plan, &names[i]);
        ids[i] = b;
        start[b + 1]++;
    }
    for (size_t b = 0; b < buckets; b++) start[b + 1] += start[b];
    memcpy(next, start, buckets * sizeof(size_t));
    for (size_t i = 0; i < n; i++) tmp[next[ids[i]]++] = names[i];

    /* names and ids are free now: MSD radix uses them as scratch. */
    size_t largest = 0, oversized = 0;
    double sum_sq = 0.0;
    for (size_t b = 0; b < buckets; b++) {
        const size_t size = start[b + 1] - start[b];
        largest = size > largest ? size : largest;
        sum_sq += (double)size * (double)size;
        if (size < 2) continue;
        if (size > NS_RADIX_ABOVE) {
            ns_msd(tmp + start[b], size, shared[b], caseless, names + start[b], ids + start[b]);
            oversized++;
        } else {
            ns_introsort(tmp + start[b], size, shared[b], caseless);
        }
    }
    memcpy(names, tmp, n * sizeof(speedup_str_t));
    if (stats) {
        const double mean = (double)n / (double)buckets;
        double var = sum_sq / (double)buckets - mean * mean;
        stats->buckets = buckets;
        stats->trie_blocks = plan.nblocks;
        stats->max_bucket = largest;
        stats->mean_bucket = mean;
        stats->stddev_bucket = var > 0.0 ? sqrt(var) : 0.0;
        stats->oversized = oversized;
    }
    goto done;

fallback:
    ns_introsort(names, n, 0, caseless);
done:
    free(plan.blocks);
    free(plan.leaves);
    free(sample);
    free(ids);
    free(tmp);
    free(start);
    free(next);
    free(shared);
    free(leaf_bucket);
}

void speedup_name_sort(speedup_str_t* names, size_t n, uint32_t flags) {
    speedup_name_sort_ex(names, n, flags, NULL, NULL);
}
