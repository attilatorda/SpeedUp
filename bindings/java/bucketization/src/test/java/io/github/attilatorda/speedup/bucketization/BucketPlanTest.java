package io.github.attilatorda.speedup.bucketization;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Arrays;
import org.junit.jupiter.api.Test;

class BucketPlanTest {

    @Test
    void geometricSizesAddUpAndAreSymmetric() {
        BucketPlan plan = BucketPlan.defaults();
        for (int n : new int[] {0, 1, 63, 64, 65, 128, 129, 1000, 100_000, 1_000_000}) {
            int[] sizes = plan.targetSizes(n);
            assertEquals(n, Arrays.stream(sizes).sum(), "n=" + n);
            assertTrue(sizes.length >= 1 && sizes.length <= plan.maxBuckets());
            if (n >= 3 * 64) {
                assertEquals(64, sizes[0]);
                assertEquals(64, sizes[sizes.length - 1]);
            }
        }
        int[] big = plan.targetSizes(1_000_000);
        assertEquals(128, big[1]);
        assertEquals(256, big[2]);
        assertEquals(512, big[3]);
        assertEquals(128, big[big.length - 2]);
        assertArrayEquals(new int[] {64, 128, 256, 512, 40}, plan.withSymmetric(false).targetSizes(1000));
        assertArrayEquals(new int[] {64, 128, 256, 512, 1024, 2048, 1936, 2048, 1024, 512, 256, 128, 64},
                plan.targetSizes(10_000));
    }

    @Test
    void otherKinds() {
        assertArrayEquals(new int[] {50, 900, 50}, BucketPlan.firstPage(50).targetSizes(1000));
        assertArrayEquals(new int[] {50, 950}, BucketPlan.firstPage(50).withSymmetric(false).targetSizes(1000));
        assertArrayEquals(new int[] {30}, BucketPlan.firstPage(50).targetSizes(30));
        assertArrayEquals(new int[] {50, 30}, BucketPlan.firstPage(50).targetSizes(80));
        assertArrayEquals(new int[] {3, 3, 2, 2}, BucketPlan.uniform(4).targetSizes(10));
        assertArrayEquals(new int[] {1, 1}, BucketPlan.uniform(4).targetSizes(2));
        assertArrayEquals(new int[] {10, 20, 70}, BucketPlan.ofSizes(10, 20).targetSizes(100));
        assertArrayEquals(new int[] {10, 15}, BucketPlan.ofSizes(10, 20, 30).targetSizes(25));
        assertArrayEquals(new int[] {10, 90}, BucketPlan.ofSizes(10, 20, 30).withMaxBuckets(2).targetSizes(100));
        assertArrayEquals(new int[] {500}, BucketPlan.defaults().withMaxBuckets(1).targetSizes(500));
        assertEquals(5, BucketPlan.defaults().withMaxBuckets(5).targetSizes(100_000).length);
        assertEquals(BucketPlan.MAX_BUCKET_LIMIT, BucketPlan.uniform(1_000_000).maxBuckets());
    }

    @Test
    void validation() {
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.geometric(0, 2.0));
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.geometric(10, 0.5));
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.geometric(10, Double.NaN));
        assertThrows(IllegalArgumentException.class, BucketPlan::ofSizes);
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.ofSizes(5, 0));
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.defaults().withRebalanceSkew(1.0));
        assertThrows(IllegalArgumentException.class, () -> BucketPlan.defaults().targetSizes(-1));
    }
}
