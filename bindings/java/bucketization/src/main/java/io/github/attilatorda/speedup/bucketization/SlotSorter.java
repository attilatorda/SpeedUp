package io.github.attilatorda.speedup.bucketization;

/** Merge sort on ranges of an {@code int[]} with a primitive comparator (no boxing). */
final class SlotSorter {

    /** Three-way comparison of two slots. Must be a total order. */
    @FunctionalInterface
    interface SlotComparator {
        int compare(int a, int b);
    }

    private static final int INSERTION_THRESHOLD = 24;

    private SlotSorter() {
    }

    /** Sorts {@code a[from, to)}. {@code tmp} must hold at least {@code (to - from + 1) / 2} ints. */
    static void sort(int[] a, int[] tmp, int from, int to, SlotComparator c) {
        if (to - from <= INSERTION_THRESHOLD) {
            insertionSort(a, from, to, c);
            return;
        }
        int mid = (from + to) >>> 1;
        sort(a, tmp, from, mid, c);
        sort(a, tmp, mid, to, c);
        merge(a, tmp, from, mid, to, c);
    }

    /**
     * Merges the sorted ranges {@code a[from, mid)} and {@code a[mid, to)}. {@code tmp} must hold
     * at least {@code mid - from} ints.
     */
    static void merge(int[] a, int[] tmp, int from, int mid, int to, SlotComparator c) {
        if (from == mid || mid == to || c.compare(a[mid - 1], a[mid]) <= 0) {
            return;
        }
        int left = mid - from;
        System.arraycopy(a, from, tmp, 0, left);
        int i = 0;
        int j = mid;
        int k = from;
        while (i < left && j < to) {
            a[k++] = c.compare(tmp[i], a[j]) <= 0 ? tmp[i++] : a[j++];
        }
        while (i < left) {
            a[k++] = tmp[i++];
        }
    }

    private static void insertionSort(int[] a, int from, int to, SlotComparator c) {
        for (int i = from + 1; i < to; i++) {
            int value = a[i];
            int j = i - 1;
            while (j >= from && c.compare(a[j], value) > 0) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = value;
        }
    }
}
