package io.github.attilatorda.speedup.bucketization;

/**
 * Snapshot of how one sort key's elements are spread over its buckets.
 *
 * @param keyName sort key name
 * @param size number of elements
 * @param bucketCount number of buckets
 * @param actualSizes elements per bucket now
 * @param expectedSizes elements per bucket when the layout was estimated
 * @param emptyBuckets buckets holding no element
 * @param sortedBuckets buckets already fully sorted in the cached order
 * @param maxSkew max of actual / max(expected, page size)
 * @param firstPageCostAscending elements to sort before the first ascending page shows
 * @param firstPageCostDescending same for descending order
 * @param needsRebalance whether the layout is stale
 */
public record BucketStats(
        String keyName,
        int size,
        int bucketCount,
        int[] actualSizes,
        int[] expectedSizes,
        int emptyBuckets,
        int sortedBuckets,
        double maxSkew,
        int firstPageCostAscending,
        int firstPageCostDescending,
        boolean needsRebalance) {
}
