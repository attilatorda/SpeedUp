package io.github.attilatorda.speedup.bucketization;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.function.IntBinaryOperator;
import java.util.function.IntUnaryOperator;

/**
 * Where the bucket boundaries of one sort key are, and how many elements each bucket is expected
 * to hold. This is the index's model of the data distribution.
 *
 * <p>The layout is a strictly increasing list of split points; bucket {@code b} holds the elements
 * {@code x} with {@code split[b-1] <= x < split[b]}. Bucket ids therefore follow ascending order,
 * and elements that compare equal always share a bucket.
 *
 * <ul>
 *   <li>Comparator-only sort keys split on sampled elements ({@link #splitters()}).
 *   <li>Keyed sort keys split on normalized keys ({@link #boundaries()}). When a split falls inside
 *       a run of equal keys (names sharing their first 8 bytes, such as "IMG_2024..."), the split
 *       point also stores the sampled element and elements with that key are placed with the
 *       tie-breaker. This keeps buckets page-sized when many keys collide.
 * </ul>
 *
 * <p>Immutable, but splitters are the elements themselves, not copies.
 *
 * @param <T> element type
 */
public final class BucketLayout<T> {

    private static final long[] NO_BOUNDARIES = new long[0];
    private static final int[] NO_INTS = new int[0];

    private final long[] boundaries;
    private final int[] splitElement;  // per keyed split point: index into splitters, or -1
    private final List<T> splitters;
    private final int[] splitterPositions;
    private final int[] expected;
    private final int estimatedFrom;

    private BucketLayout(long[] boundaries, int[] splitElement, List<T> splitters, int[] splitterPositions,
                         int[] expected, int estimatedFrom) {
        this.boundaries = boundaries;
        this.splitElement = splitElement;
        this.splitters = splitters;
        this.splitterPositions = splitterPositions;
        this.expected = expected;
        this.estimatedFrom = estimatedFrom;
    }

    /** A single bucket holding everything. */
    public static <T> BucketLayout<T> single(int size) {
        return new BucketLayout<>(NO_BOUNDARIES, NO_INTS, Collections.emptyList(), NO_INTS, new int[] {size}, size);
    }

    public int bucketCount() {
        return expected.length;
    }

    /** Key of every split point, non-decreasing in unsigned order (keyed sort keys only). */
    public long[] boundaries() {
        return boundaries.clone();
    }

    /**
     * Elements stored with split points: every split point of a comparator-only key, and the split
     * points of a keyed key that fall inside a run of equal keys. They are the elements themselves:
     * changing one changes the layout.
     */
    public List<T> splitters() {
        return splitters;
    }

    /** Where each splitter was taken from: positions in the {@code items} passed to estimate(). */
    public int[] splitterPositions() {
        return splitterPositions.clone();
    }

    int[] splitterPositionsUnsafe() {
        return splitterPositions;
    }

    /** Estimated number of elements per bucket when the layout was made. */
    public int[] expectedSizes() {
        return expected.clone();
    }

    int expectedSize(int bucket) {
        return expected[bucket];
    }

    /** Number of elements the layout was estimated from. */
    public int estimatedFrom() {
        return estimatedFrom;
    }

    /**
     * Lowest bucket an element with normalized key {@code key} can be in. This is its bucket unless
     * a split point with the same key stores an element.
     */
    public int bucketOfKey(long key) {
        int lo = 0;
        int hi = boundaries.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            int c = Long.compareUnsigned(key, boundaries[mid]);
            if (c < 0 || (c == 0 && splitElement[mid] >= 0)) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    /** Bucket of an element under {@code key} (the sort key the layout was made for). */
    public int bucketOf(T value, SortKey<T> key) {
        if (key.hasKey()) {
            return bucketOf(value, key.key(value), key);
        }
        int lo = 0;
        int hi = splitters.size();
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            if (key.compare(value, splitters.get(mid)) < 0) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    /** Bucket of an element of a keyed sort key whose normalized key {@code valueKey} is known. */
    public int bucketOf(T value, long valueKey, SortKey<T> key) {
        int lo = 0;
        int hi = boundaries.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            int c = Long.compareUnsigned(valueKey, boundaries[mid]);
            if (c == 0 && splitElement[mid] >= 0) {
                c = key.tieBreak(value, splitters.get(splitElement[mid])) < 0 ? -1 : 0;
            }
            if (c < 0) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    }

    /**
     * Estimates a layout for {@code items} under {@code key}.
     *
     * @param keys normalized key of every item for keyed sort keys (at least {@code items.size()}
     *     entries), ignored otherwise
     */
    public static <T> BucketLayout<T> estimate(SortKey<T> key, List<? extends T> items, long[] keys, BucketPlan plan) {
        return estimate(key, items, keys, plan, null);
    }

    /**
     * Like {@link #estimate(SortKey, List, long[], BucketPlan)}, but lays out only the items at the
     * positions in {@code subset} (one group of a grouped sort key). Splitter positions still refer
     * to {@code items}.
     */
    public static <T> BucketLayout<T> estimate(SortKey<T> key, List<? extends T> items, long[] keys, BucketPlan plan,
                                               int[] subset) {
        final int n = subset != null ? subset.length : items.size();
        int[] targets = plan.targetSizes(n);
        if (targets.length <= 1) {
            return single(n);
        }

        int[] sample = sampleIndices(n, plan.sampleSize(), plan.seed());
        if (subset != null) {
            for (int i = 0; i < sample.length; i++) {
                sample[i] = subset[sample[i]];
            }
        }
        final int s = sample.length;
        int[] desired = new int[targets.length - 1];
        long cumulative = 0;
        for (int j = 0; j < desired.length; j++) {
            cumulative += targets[j];
            desired[j] = (int) (cumulative * s / n);
        }

        // Sort the sample by the exact order: key (if any), then tie-breaker.
        final boolean keyed = key.hasKey();
        IntBinaryOperator compare = (a, b) -> {
            if (keyed) {
                int c = Long.compareUnsigned(keys[a], keys[b]);
                return c != 0 ? c : key.tieBreak(items.get(a), items.get(b));
            }
            return key.compare(items.get(a), items.get(b));
        };
        final int[] sorted = sortSample(sample, compare);
        int[] positions = chooseSplitPositions(desired, s,
                q -> lowerBound(sorted, sorted[q], compare), q -> upperBound(sorted, sorted[q], compare));

        long[] boundaries = keyed ? new long[positions.length] : NO_BOUNDARIES;
        int[] splitElement = keyed ? new int[positions.length] : NO_INTS;
        List<T> chosen = new ArrayList<>();
        int[] chosenPositions = new int[positions.length];
        for (int i = 0; i < positions.length; i++) {
            int item = sorted[positions[i]];
            // A keyed split at the start of a key run needs no element.
            boolean needsElement = !keyed || keys[sorted[positions[i] - 1]] == keys[item];
            if (keyed) {
                boundaries[i] = keys[item];
                splitElement[i] = needsElement ? chosen.size() : -1;
            }
            if (needsElement) {
                chosenPositions[chosen.size()] = item;
                chosen.add(items.get(item));
            }
        }

        int[] expected = new int[positions.length + 1];
        int previous = 0;
        for (int i = 0; i <= positions.length; i++) {
            int p = i < positions.length ? positions[i] : s;
            expected[i] = (int) (((long) (p - previous) * n + s / 2) / s);
            previous = p;
        }
        return new BucketLayout<>(boundaries, splitElement, Collections.unmodifiableList(chosen),
                Arrays.copyOf(chosenPositions, chosen.size()), expected, n);
    }

    /** The sample indices sorted by {@code compare}, which may be inconsistent. */
    private static int[] sortSample(int[] sample, IntBinaryOperator compare) {
        Integer[] boxed = new Integer[sample.length];
        for (int i = 0; i < sample.length; i++) {
            boxed[i] = sample[i];
        }
        try {
            Arrays.sort(boxed, (a, b) -> compare.applyAsInt(a, b));
        } catch (IllegalArgumentException inconsistentComparator) {
            // TimSort throws when it notices a comparator with cycles, and real ones have them
            // (Dolphin's natural compare: "6.10" < "6.9" < "6.9.1" < "6.10"). Our merge sort never
            // throws. It is only the fallback: sharing it with the index's hot bucket sorts would
            // give their comparator call one more receiver type and slow them down.
            int[] sorted = sample.clone();
            SlotSorter.sort(sorted, new int[(sorted.length + 1) / 2], 0, sorted.length, compare::applyAsInt);
            return sorted;
        }
        int[] sorted = new int[sample.length];
        for (int i = 0; i < sample.length; i++) {
            sorted[i] = boxed[i];
        }
        return sorted;
    }

    /**
     * Jittered systematic sample: one random index from each of {@code s} equal strides, drawn with
     * SplitMix64 so every language port picks the same indices. Every index when {@code n <=
     * sampleSize}.
     */
    static int[] sampleIndices(int n, int sampleSize, long seed) {
        int s = Math.min(n, sampleSize);
        int[] out = new int[s];
        if (s == n) {
            for (int i = 0; i < n; i++) {
                out[i] = i;
            }
            return out;
        }
        long state = seed;
        for (int j = 0; j < s; j++) {
            long lo = (long) j * n / s;
            long hi = (long) (j + 1) * n / s;
            state += 0x9E3779B97F4A7C15L;
            long z = state;
            z = (z ^ (z >>> 30)) * 0xBF58476D1CE4E5B9L;
            z = (z ^ (z >>> 27)) * 0x94D049BB133111EBL;
            z = z ^ (z >>> 31);
            out[j] = (int) (lo + Long.remainderUnsigned(z, hi - lo));
        }
        return out;
    }

    /**
     * Turns desired split positions in a sorted sample into actual ones. A split must not fall
     * inside a run of equal elements, so each desired position moves to the start or the end of
     * its run, whichever is closer. Positions are strictly increasing and in (0, s).
     */
    private static int[] chooseSplitPositions(int[] desired, int s, IntUnaryOperator runStart, IntUnaryOperator runEnd) {
        int[] out = new int[desired.length];
        int count = 0;
        int previous = 0;
        for (int q : desired) {
            int lo = runStart.applyAsInt(q);
            int hi = runEnd.applyAsInt(q);
            int best = 0;
            if (lo > previous) {
                best = lo;
            }
            if (hi < s && hi > previous && (best == 0 || hi - q < q - lo)) {
                best = hi;
            }
            if (best != 0) {
                out[count++] = best;
                previous = best;
            }
        }
        return Arrays.copyOf(out, count);
    }

    private static int lowerBound(int[] a, int value, IntBinaryOperator compare) {
        int lo = 0;
        int hi = a.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            if (compare.applyAsInt(a[mid], value) < 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }

    private static int upperBound(int[] a, int value, IntBinaryOperator compare) {
        int lo = 0;
        int hi = a.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            if (compare.applyAsInt(a[mid], value) <= 0) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    }
}
