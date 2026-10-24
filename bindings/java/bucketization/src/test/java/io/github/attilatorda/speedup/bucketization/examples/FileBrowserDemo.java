package io.github.attilatorda.speedup.bucketization.examples;

import io.github.attilatorda.speedup.bucketization.BucketIndex;
import io.github.attilatorda.speedup.bucketization.BucketPlan;
import io.github.attilatorda.speedup.bucketization.BucketStats;
import io.github.attilatorda.speedup.bucketization.Direction;
import io.github.attilatorda.speedup.bucketization.Keys;
import io.github.attilatorda.speedup.bucketization.SortKey;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.attribute.BasicFileAttributes;
import java.text.Collator;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Lists a directory, then switches between sort columns the way a file manager does when a column
 * header is clicked.
 *
 * <pre>
 * java -cp target/classes:target/test-classes \
 *     io.github.attilatorda.speedup.bucketization.examples.FileBrowserDemo [directory] [rows]
 * </pre>
 */
public final class FileBrowserDemo {

    record Entry(String name, boolean directory, long size, long modifiedMillis) {
    }

    /** "file2" &lt; "file10", case-insensitive; equal names fall back to String order. */
    static final Comparator<String> NATURAL = (a, b) -> {
        int i = 0;
        int j = 0;
        while (i < a.length() && j < b.length()) {
            char ca = a.charAt(i);
            char cb = b.charAt(j);
            if (Character.isDigit(ca) && Character.isDigit(cb)) {
                int si = i;
                int sj = j;
                while (si < a.length() && a.charAt(si) == '0') {
                    si++;
                }
                while (sj < b.length() && b.charAt(sj) == '0') {
                    sj++;
                }
                int ei = si;
                int ej = sj;
                while (ei < a.length() && Character.isDigit(a.charAt(ei))) {
                    ei++;
                }
                while (ej < b.length() && Character.isDigit(b.charAt(ej))) {
                    ej++;
                }
                if (ei - si != ej - sj) {
                    return Integer.compare(ei - si, ej - sj);
                }
                int c = a.substring(si, ei).compareTo(b.substring(sj, ej));
                if (c != 0) {
                    return c;
                }
                i = ei;
                j = ej;
                continue;
            }
            int c = Character.compare(Character.toLowerCase(ca), Character.toLowerCase(cb));
            if (c != 0) {
                return c;
            }
            i++;
            j++;
        }
        int c = Integer.compare(a.length() - i, b.length() - j);
        return c != 0 ? c : a.compareTo(b);
    };

    private FileBrowserDemo() {
    }

    public static void main(String[] args) throws IOException {
        Path root = Paths.get(args.length > 0 ? args[0] : ".").toAbsolutePath().normalize();
        int rows = args.length > 1 ? Integer.parseInt(args[1]) : 10;

        List<Entry> entries = new ArrayList<>();
        try (Stream<Path> listing = Files.list(root)) {
            for (Path p : (Iterable<Path>) listing::iterator) {
                BasicFileAttributes a = Files.readAttributes(p, BasicFileAttributes.class);
                entries.add(new Entry(p.getFileName().toString(), a.isDirectory(), a.isDirectory() ? 0 : a.size(),
                        a.lastModifiedTime().toMillis()));
            }
        }

        // The first bucket holds one screen of rows.
        BucketIndex<Entry> index = new BucketIndex<>(BucketPlan.defaults().withPageSize(rows));
        index.addSortKey(SortKey.ofCollated("name", Entry::name, Collator.getInstance()));
        index.addSortKey(SortKey.ofComparator("natural", Comparator.comparing(Entry::name, NATURAL)));
        index.addSortKey(SortKey.ofLong("size", Entry::size));
        // Folders first, newest first. Descending order reverses everything (folders would go
        // last), so this is one composite key sorted ascending: the top bit separates folders, the
        // rest is the reversed timestamp. The shift drops one bit; the tie-breaker restores it.
        index.addSortKey(SortKey.ofKey("newest",
                e -> (e.directory() ? 0 : Long.MIN_VALUE) | (Keys.reverse(Keys.fromSigned(e.modifiedMillis())) >>> 1),
                Comparator.comparingLong(Entry::modifiedMillis).reversed()));
        index.addAll(entries);  // pre-sorting: every entry picks a bucket per sort key

        System.out.printf("%d entries in %s%n", index.size(), root);
        index.sortBy("natural");
        printPage(index, rows);
        index.sortBy("size", Direction.DESCENDING);
        printPage(index, rows);
        index.sortBy("newest");
        printPage(index, rows);
        index.sortBy("name");
        printPage(index, rows);

        BucketStats stats = index.stats("size");
        System.out.printf("%nsize key: %d buckets, first page needs %d (asc) / %d (desc) elements sorted, max skew %.2f%n",
                stats.bucketCount(), stats.firstPageCostAscending(), stats.firstPageCostDescending(), stats.maxSkew());
    }

    private static void printPage(BucketIndex<Entry> index, int rows) {
        long start = System.nanoTime();
        List<Entry> page = index.range(0, rows);
        double ms = (System.nanoTime() - start) / 1e6;
        System.out.printf("%n== sorted by %s (%s), first page in %.3f ms ==%n", index.activeSortKey().name(),
                index.direction().toString().toLowerCase(), ms);
        for (Entry e : page) {
            System.out.printf("  %-40.40s %12d %14d%s%n", e.name(), e.size(), e.modifiedMillis() / 1000,
                    e.directory() ? "  <dir>" : "");
        }
    }
}
