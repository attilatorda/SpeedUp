package io.github.attilatorda.speedup.bucketization;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.SplittableRandom;
import org.junit.jupiter.api.Test;

class BucketLayoutTest {

    private static long[] keysOf(List<Long> values, SortKey<Long> key) {
        long[] keys = new long[values.size()];
        for (int i = 0; i < keys.length; i++) {
            keys[i] = key.key(values.get(i));
        }
        return keys;
    }

    @Test
    void exactLayoutWhenEverythingIsSampled() {
        List<Long> values = new ArrayList<>();
        for (int i = 0; i < 1000; i++) {
            values.add((i * 7919L) % 1000);
        }
        SortKey<Long> key = SortKey.ofLong("v", Long::longValue);
        BucketPlan plan = BucketPlan.defaults();
        BucketLayout<Long> layout = BucketLayout.estimate(key, values, keysOf(values, key), plan);
        assertArrayEquals(plan.targetSizes(1000), layout.expectedSizes());
        assertEquals(layout.bucketCount() - 1, layout.boundaries().length);
        int[] counts = new int[layout.bucketCount()];
        for (Long v : values) {
            counts[layout.bucketOf(v, key)]++;
        }
        assertArrayEquals(layout.expectedSizes(), counts);
    }

    @Test
    void equalKeysNeverStraddleABoundary() {
        List<Long> values = new ArrayList<>();
        for (int i = 0; i < 5000; i++) {
            values.add(i < 3000 ? 0L : i);
        }
        SortKey<Long> key = SortKey.ofLong("v", Long::longValue);
        BucketLayout<Long> layout = BucketLayout.estimate(key, values, keysOf(values, key), BucketPlan.defaults());
        assertEquals(layout.bucketOf(0L, key), layout.bucketOf(values.get(2999), key));
        long[] bounds = layout.boundaries();
        for (int i = 1; i < bounds.length; i++) {
            assertTrue(Keys.compare(bounds[i - 1], bounds[i]) < 0);
        }
        assertEquals(5000, Arrays.stream(layout.expectedSizes()).sum());
    }

    /** Golden values shared with the C++ tests: every port samples and splits identically. */
    @Test
    void matchesTheCppPort() {
        assertArrayEquals(new int[] {413, 17291, 26358, 43264, 50750, 64062, 87425, 88408},
                BucketLayout.sampleIndices(100_000, 8, 42));
        List<Long> values = new ArrayList<>();
        for (int i = 0; i < 10_000; i++) {
            values.add((i * 7919L) % 10_000);
        }
        SortKey<Long> key = SortKey.ofLong("v", Long::longValue);
        BucketLayout<Long> layout = BucketLayout.estimate(key, values, keysOf(values, key), BucketPlan.defaults());
        long[] bounds = Arrays.stream(layout.boundaries()).map(Keys::fromSigned).toArray();
        assertArrayEquals(new long[] {75, 183, 412, 918, 1963, 3964, 5712, 7785, 8936, 9548, 9769, 9926}, bounds);
        assertArrayEquals(new int[] {63, 127, 254, 513, 1025, 2046, 1938, 2046, 1025, 513, 254, 127, 68},
                layout.expectedSizes());
    }

    @Test
    void comparatorOnlyLayoutsSplitOnElements() {
        SplittableRandom rng = new SplittableRandom(5);
        List<String> names = new ArrayList<>();
        for (int i = 0; i < 20_000; i++) {
            names.add(KeysTest.randomString(rng) + i);
        }
        SortKey<String> key = SortKey.ofComparator("natural", String::compareTo);
        BucketLayout<String> layout = BucketLayout.estimate(key, names, null, BucketPlan.defaults());
        assertEquals(layout.bucketCount() - 1, layout.splitters().size());
        assertEquals(0, layout.boundaries().length);
        for (int i = 1; i < layout.splitters().size(); i++) {
            assertTrue(layout.splitters().get(i - 1).compareTo(layout.splitters().get(i)) < 0);
        }
        int[] counts = new int[layout.bucketCount()];
        for (String name : names) {
            counts[layout.bucketOf(name, key)]++;
        }
        // Sampled estimate: the end buckets stay close to one page.
        assertTrue(counts[0] < 200, "first bucket " + counts[0]);
        assertTrue(counts[counts.length - 1] < 200, "last bucket " + counts[counts.length - 1]);
    }
}
