package io.github.attilatorda.speedup.bucketization.examples;

import io.github.attilatorda.speedup.bucketization.BucketIndex;
import io.github.attilatorda.speedup.bucketization.SortKey;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Random;

/**
 * Time until the first page is visible after the user switches the sort column, compared with a
 * full {@link List#sort}. Same workload as benchmarks/core/benchmark_bucketization.cpp.
 *
 * <pre>
 * java -cp target/classes:target/test-classes \
 *     io.github.attilatorda.speedup.bucketization.examples.BucketizationBenchmark [max_n]
 * </pre>
 */
public final class BucketizationBenchmark {

    record File(String name, long size, long modified, long accessed) {
    }

    private BucketizationBenchmark() {
    }

    /** Names like a real directory: camera files with long shared prefixes, numbered documents, words. */
    static List<File> makeFiles(int n, long seed) {
        Random rng = new Random(seed);
        List<File> files = new ArrayList<>(n);
        for (int i = 0; i < n; i++) {
            String name;
            switch (rng.nextInt(3)) {
                case 0:
                    name = "IMG_20" + rng.nextInt(100_000_000) + ".jpg";
                    break;
                case 1:
                    name = "Document " + rng.nextInt(100_000) + ".pdf";
                    break;
                default:
                    StringBuilder sb = new StringBuilder();
                    for (int c = 3 + rng.nextInt(10); c > 0; c--) {
                        sb.append((char) ('a' + rng.nextInt(26)));
                    }
                    name = sb.append(".txt").toString();
            }
            long size = (long) Math.exp(10.0 + 2.5 * rng.nextGaussian());
            long modified = 1_300_000_000L + (long) (rng.nextDouble() * 460_000_000L);
            long accessed = Math.max(modified, 1_300_000_000L + (long) (rng.nextDouble() * 460_000_000L));
            files.add(new File(name, size, modified, accessed));
        }
        return files;
    }

    static double median(double[] values) {
        double[] copy = values.clone();
        Arrays.sort(copy);
        return copy[copy.length / 2];
    }

    public static void main(String[] args) {
        int maxN = args.length > 0 ? Integer.parseInt(args[0]) : 1_000_000;
        List<SortKey<File>> keys = List.of(
                SortKey.ofString("name", File::name),
                SortKey.ofLong("size", File::size),
                SortKey.ofLong("modified", File::modified),
                SortKey.ofLong("accessed", File::accessed));
        List<Comparator<File>> plain = List.of(
                Comparator.comparing(File::name),
                Comparator.comparingLong(File::size),
                Comparator.comparingLong(File::modified),
                Comparator.comparingLong(File::accessed));
        int repeats = 7;

        System.out.printf("%10s %-9s %14s %12s %16s %18s %18s%n", "n", "key", "List.sort ms", "presort ms",
                "switch+page ms", "cached switch ms", "switch+sortAll ms");
        for (int n = 10_000; n <= maxN; n *= 10) {
            List<File> files = makeFiles(n, 42);
            for (int k = 0; k < keys.size(); k++) {
                double[] full = new double[repeats];
                double[] presort = new double[repeats];
                double[] cold = new double[repeats];
                double[] warm = new double[repeats];
                double[] sortAll = new double[repeats];
                Object sink = null;
                for (int r = 0; r < repeats; r++) {
                    List<File> copy = new ArrayList<>(files);
                    long t = System.nanoTime();
                    copy.sort(plain.get(k));
                    full[r] = (System.nanoTime() - t) / 1e6;

                    BucketIndex<File> index = new BucketIndex<>();
                    t = System.nanoTime();
                    keys.forEach(index::addSortKey);
                    index.addAll(files);
                    presort[r] = (System.nanoTime() - t) / 1e6;

                    t = System.nanoTime();
                    index.sortBy(keys.get(k).name());
                    sink = index.firstPage().get(0);
                    cold[r] = (System.nanoTime() - t) / 1e6;

                    index.sortBy(keys.get((k + 1) % keys.size()).name());
                    t = System.nanoTime();
                    index.sortBy(keys.get(k).name());
                    sink = index.firstPage().get(0);
                    warm[r] = (System.nanoTime() - t) / 1e6;

                    BucketIndex<File> fresh = new BucketIndex<>();
                    fresh.addSortKey(keys.get(k));
                    fresh.addAll(files);
                    t = System.nanoTime();
                    fresh.sortBy(keys.get(k).name());
                    fresh.sortAll();
                    sink = fresh.get(0);
                    sortAll[r] = (System.nanoTime() - t) / 1e6;
                }
                if (sink == null) {
                    throw new AssertionError();
                }
                System.out.printf("%10d %-9s %14.3f %12.3f %16.3f %18.4f %18.3f%n", n, keys.get(k).name(), median(full),
                        median(presort), median(cold), median(warm), median(sortAll));
            }
        }
        System.out.println("presort = registering 4 sort keys and assigning every element to its buckets (once per load).");
        System.out.println("medians of " + repeats + " runs; the first runs include JIT warm-up.");
    }
}
