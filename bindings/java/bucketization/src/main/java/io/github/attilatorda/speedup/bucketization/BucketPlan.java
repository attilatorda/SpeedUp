package io.github.attilatorda.speedup.bucketization;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Target bucket sizes. Buckets do not have to be equal: the default plan makes the buckets at
 * both ends one page long (what a view shows first, in ascending and in descending order) and
 * lets them grow towards the middle, so showing the first page only needs a page-sized sort.
 *
 * <p>Plans are immutable; the {@code with*} methods return modified copies.
 */
public final class BucketPlan {

    /** How the target sizes are derived. */
    public enum Kind {
        /** page, page*g, page*g^2, ... (from both ends when symmetric). */
        GEOMETRIC,
        /** [page, remainder], or [page, remainder, page] when symmetric. */
        FIRST_PAGE,
        /** N equal buckets. */
        UNIFORM,
        /** Caller-provided sizes; the last bucket absorbs the remainder. */
        SIZES
    }

    /** Bucket ids are stored as 16-bit integers. */
    public static final int MAX_BUCKET_LIMIT = 65535;
    public static final int MAX_SAMPLE_SIZE = 1 << 20;
    public static final int DEFAULT_PAGE_SIZE = 64;

    private final Kind kind;
    private final int pageSize;
    private final double growth;
    private final int maxBuckets;
    private final boolean symmetric;
    private final int sampleSize;
    private final long seed;
    private final double rebalanceSkew;
    private final boolean autoRebalance;
    private final int[] sizes;

    private BucketPlan(Kind kind, int pageSize, double growth, int maxBuckets, boolean symmetric, int sampleSize,
                       long seed, double rebalanceSkew, boolean autoRebalance, int[] sizes) {
        this.kind = kind;
        this.pageSize = pageSize;
        this.growth = growth;
        this.maxBuckets = maxBuckets;
        this.symmetric = symmetric;
        this.sampleSize = sampleSize;
        this.seed = seed;
        this.rebalanceSkew = rebalanceSkew;
        this.autoRebalance = autoRebalance;
        this.sizes = sizes;
    }

    private static BucketPlan base(Kind kind) {
        return new BucketPlan(kind, DEFAULT_PAGE_SIZE, 2.0, 64, true, 2048, 0x5EEDB0C4E7L, 4.0, true, new int[0]);
    }

    /** {@code geometric(64, 2.0)}: symmetric, at most 64 buckets. */
    public static BucketPlan defaults() {
        return geometric(DEFAULT_PAGE_SIZE, 2.0);
    }

    /** The first bucket holds what fits on screen, the rest is the remainder. */
    public static BucketPlan firstPage(int pageSize) {
        return base(Kind.FIRST_PAGE).withPageSize(pageSize);
    }

    /** Buckets start one page long and grow by {@code growth} (&gt;= 1) towards the middle. */
    public static BucketPlan geometric(int pageSize, double growth) {
        if (!(growth >= 1.0) || Double.isInfinite(growth)) {
            throw new IllegalArgumentException("BucketPlan: growth must be a finite number >= 1");
        }
        BucketPlan plan = base(Kind.GEOMETRIC).withPageSize(pageSize);
        return new BucketPlan(plan.kind, plan.pageSize, growth, plan.maxBuckets, plan.symmetric, plan.sampleSize,
                plan.seed, plan.rebalanceSkew, plan.autoRebalance, plan.sizes);
    }

    /** {@code buckets} buckets of (nearly) equal size. */
    public static BucketPlan uniform(int buckets) {
        return base(Kind.UNIFORM).withMaxBuckets(buckets);
    }

    /**
     * Explicit target sizes, e.g. {@code ofSizes(50, 200, 1000)}. The last bucket absorbs whatever
     * is left. The first size doubles as the page size.
     */
    public static BucketPlan ofSizes(int... sizes) {
        if (sizes.length == 0) {
            throw new IllegalArgumentException("BucketPlan: sizes must not be empty");
        }
        for (int size : sizes) {
            requirePositive(size, "sizes[i]");
        }
        BucketPlan plan = base(Kind.SIZES);
        return new BucketPlan(plan.kind, sizes[0], plan.growth, plan.maxBuckets, plan.symmetric, plan.sampleSize,
                plan.seed, plan.rebalanceSkew, plan.autoRebalance, sizes.clone());
    }

    public BucketPlan withPageSize(int pageSize) {
        return new BucketPlan(kind, requirePositive(pageSize, "pageSize"), growth, maxBuckets, symmetric, sampleSize,
                seed, rebalanceSkew, autoRebalance, sizes);
    }

    public BucketPlan withMaxBuckets(int maxBuckets) {
        int clamped = Math.min(requirePositive(maxBuckets, "maxBuckets"), MAX_BUCKET_LIMIT);
        return new BucketPlan(kind, pageSize, growth, clamped, symmetric, sampleSize, seed, rebalanceSkew,
                autoRebalance, sizes);
    }

    /**
     * Symmetric plans keep small buckets at both ends so descending order is as cheap as
     * ascending. Ignored by uniform and explicit-size plans.
     */
    public BucketPlan withSymmetric(boolean symmetric) {
        return new BucketPlan(kind, pageSize, growth, maxBuckets, symmetric, sampleSize, seed, rebalanceSkew,
                autoRebalance, sizes);
    }

    /** How many elements are sampled to estimate the key distribution. */
    public BucketPlan withSampleSize(int sampleSize) {
        int clamped = Math.min(requirePositive(sampleSize, "sampleSize"), MAX_SAMPLE_SIZE);
        return new BucketPlan(kind, pageSize, growth, maxBuckets, symmetric, clamped, seed, rebalanceSkew,
                autoRebalance, sizes);
    }

    /** Seed of the deterministic sampler. */
    public BucketPlan withSeed(long seed) {
        return new BucketPlan(kind, pageSize, growth, maxBuckets, symmetric, sampleSize, seed, rebalanceSkew,
                autoRebalance, sizes);
    }

    /**
     * A bucket holding more than {@code skew} times its expected size (expected sizes below one
     * page count as one page) asks for a rebalance.
     */
    public BucketPlan withRebalanceSkew(double skew) {
        if (!(skew > 1.0)) {
            throw new IllegalArgumentException("BucketPlan: rebalance skew must be > 1");
        }
        return new BucketPlan(kind, pageSize, growth, maxBuckets, symmetric, sampleSize, seed, skew, autoRebalance,
                sizes);
    }

    /**
     * When enabled (default), layouts that went stale are re-estimated automatically after bulk
     * inserts and before sorted reads.
     */
    public BucketPlan withAutoRebalance(boolean enabled) {
        return new BucketPlan(kind, pageSize, growth, maxBuckets, symmetric, sampleSize, seed, rebalanceSkew, enabled,
                sizes);
    }

    public Kind kind() {
        return kind;
    }

    public int pageSize() {
        return pageSize;
    }

    public double growth() {
        return growth;
    }

    public int maxBuckets() {
        return maxBuckets;
    }

    public boolean symmetric() {
        return symmetric;
    }

    public int sampleSize() {
        return sampleSize;
    }

    public long seed() {
        return seed;
    }

    public double rebalanceSkew() {
        return rebalanceSkew;
    }

    public boolean autoRebalance() {
        return autoRebalance;
    }

    public int[] sizes() {
        return sizes.clone();
    }

    /**
     * Target size of every bucket for {@code n} elements, in ascending key order. The sizes add up
     * to {@code n}; there is always at least one bucket.
     */
    public int[] targetSizes(int n) {
        if (n < 0) {
            throw new IllegalArgumentException("n must be >= 0");
        }
        if (n == 0) {
            return new int[] {0};
        }
        switch (kind) {
            case UNIFORM:
                return uniformSizes(n);
            case SIZES:
                return explicitSizes(n);
            case FIRST_PAGE:
                return firstPageSizes(n);
            default:
                return geometricSizes(n);
        }
    }

    private int[] uniformSizes(int n) {
        int count = Math.min(maxBuckets, n);
        int[] out = new int[count];
        Arrays.fill(out, n / count);
        for (int i = 0; i < n % count; i++) {
            out[i]++;
        }
        return out;
    }

    private int[] explicitSizes(int n) {
        List<Integer> out = new ArrayList<>();
        int remaining = n;
        for (int size : sizes) {
            if (remaining == 0 || out.size() + 1 == maxBuckets) {
                break;
            }
            int take = Math.min(size, remaining);
            out.add(take);
            remaining -= take;
        }
        if (remaining > 0) {
            out.add(remaining);
        }
        return out.stream().mapToInt(Integer::intValue).toArray();
    }

    private int[] firstPageSizes(int n) {
        int page = Math.min(pageSize, n);
        if (maxBuckets == 1 || page == n) {
            return new int[] {n};
        }
        if (symmetric && maxBuckets >= 3 && n > 2L * page) {
            return new int[] {page, n - 2 * page, page};
        }
        return new int[] {page, n - page};
    }

    private int[] geometricSizes(int n) {
        List<Integer> front = new ArrayList<>();
        List<Integer> back = new ArrayList<>();
        int remaining = n;
        int budget = maxBuckets;
        double size = pageSize;
        while (remaining > 0 && budget > 1) {
            int step = (int) Math.ceil(size);
            int take = Math.min(step, remaining);
            front.add(take);
            remaining -= take;
            budget--;
            if (symmetric) {
                if (remaining == 0 || budget <= 1) {
                    break;
                }
                take = Math.min(step, remaining);
                back.add(take);
                remaining -= take;
                budget--;
            }
            size = Math.min(size * growth, n);
        }
        if (remaining > 0) {
            front.add(remaining);
        }
        for (int i = back.size() - 1; i >= 0; i--) {
            front.add(back.get(i));
        }
        return front.stream().mapToInt(Integer::intValue).toArray();
    }

    private static int requirePositive(int value, String what) {
        if (value <= 0) {
            throw new IllegalArgumentException("BucketPlan: " + what + " must be > 0");
        }
        return value;
    }

    @Override
    public String toString() {
        return "BucketPlan[" + kind + ", page=" + pageSize + ", growth=" + growth + ", maxBuckets=" + maxBuckets
                + ", symmetric=" + symmetric + "]";
    }
}
