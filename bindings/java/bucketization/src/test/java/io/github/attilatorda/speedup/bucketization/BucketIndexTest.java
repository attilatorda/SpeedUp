package io.github.attilatorda.speedup.bucketization;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.text.Collator;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.ConcurrentModificationException;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;
import java.util.SplittableRandom;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Predicate;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.api.Test;

class BucketIndexTest {

    /** Mutable element, like a file whose metadata changes. */
    static final class Rec {
        final int id;
        String name;
        long size;
        double score;

        Rec(int id, String name, long size, double score) {
            this.id = id;
            this.name = name;
            this.size = size;
            this.score = score;
        }

        @Override
        public String toString() {
            return id + ":" + name + ":" + size;
        }
    }

    static Rec randomRec(SplittableRandom rng, int id) {
        long size;
        switch (rng.nextInt(4)) {
            case 0:
                size = 0;  // heavy duplicate
                break;
            case 1:
                size = rng.nextInt(10);  // few distinct values
                break;
            default:
                size = rng.nextLong(-1_000_000, 1_000_000);
        }
        return new Rec(id, KeysTest.randomString(rng), size, rng.nextGaussian() * 100);
    }

    static void randomize(Rec r, SplittableRandom rng) {
        Rec fresh = randomRec(rng, r.id);
        r.name = fresh.name;
        r.size = fresh.size;
        r.score = fresh.score;
    }

    static List<SortKey<Rec>> sortKeys() {
        List<SortKey<Rec>> keys = new ArrayList<>();
        keys.add(SortKey.ofString("name", r -> r.name));
        keys.add(SortKey.ofStringIgnoreCase("nameIgnoreCase", r -> r.name));
        keys.add(SortKey.ofCollated("collated", r -> r.name, Collator.getInstance(Locale.GERMAN)));
        keys.add(SortKey.ofLong("size", r -> r.size));
        keys.add(SortKey.ofDouble("score", r -> r.score));
        keys.add(SortKey.ofComparator("mod97", (a, b) -> {
            int c = Integer.compare(b.id % 97, a.id % 97);
            return c != 0 ? c : a.name.compareTo(b.name);
        }));
        keys.add(SortKey.ofKey("sizeThenScore", r -> Keys.fromSigned(r.size), (a, b) -> Double.compare(a.score, b.score)));
        // Groups ("folders first" in both directions), keyed and comparator-only.
        keys.add(SortKey.<Rec>ofString("nameFoldersFirst", r -> r.name).withGroups(2, r -> r.id % 3 == 0 ? 0 : 1));
        keys.add(SortKey.<Rec>ofComparator("scoreIn3Groups", (a, b) -> Double.compare(a.score, b.score))
                .withGroups(3, r -> r.id % 3));
        return keys;
    }

    /**
     * Reference order: stable sort of the elements in insertion order. Descending reverses the
     * order inside each group (the whole list when ungrouped).
     */
    static List<Integer> referenceIds(List<Rec> model, SortKey<Rec> key, Direction direction) {
        List<Rec> sorted = new ArrayList<>(model);
        sorted.sort(key);
        if (direction == Direction.DESCENDING) {
            for (int first = 0; first < sorted.size();) {
                int last = first;
                while (last < sorted.size() && key.group(sorted.get(last)) == key.group(sorted.get(first))) {
                    last++;
                }
                java.util.Collections.reverse(sorted.subList(first, last));
                first = last;
            }
        }
        return ids(sorted);
    }

    static List<Integer> ids(List<Rec> recs) {
        List<Integer> ids = new ArrayList<>();
        for (Rec r : recs) {
            ids.add(r.id);
        }
        return ids;
    }

    static List<BucketPlan> plans() {
        return List.of(
                BucketPlan.defaults(),
                BucketPlan.defaults().withPageSize(16).withSampleSize(300),
                BucketPlan.firstPage(40),
                BucketPlan.uniform(7).withAutoRebalance(false),
                BucketPlan.ofSizes(5, 10, 400).withSymmetric(false),
                BucketPlan.geometric(3, 1.5).withMaxBuckets(9).withRebalanceSkew(1.5));
    }

    @ParameterizedTest
    @MethodSource("plans")
    void matchesAFullSortUnderRandomChanges(BucketPlan plan) {
        SplittableRandom rng = new SplittableRandom(plan.toString().hashCode() ^ 77);
        BucketIndex<Rec> index = new BucketIndex<>(plan);
        sortKeys().forEach(index::addSortKey);
        List<String> names = index.sortKeyNames();
        List<Rec> model = new ArrayList<>();
        int nextId = 0;
        for (int round = 0; round < 60; round++) {
            switch (rng.nextInt(10)) {
                case 0:
                case 1: {
                    List<Rec> batch = new ArrayList<>();
                    for (int i = rng.nextInt(3000); i > 0; i--) {
                        batch.add(randomRec(rng, nextId++));
                    }
                    model.addAll(batch);
                    index.addAll(batch);
                    break;
                }
                case 2:
                case 3:
                    for (int i = rng.nextInt(200); i > 0; i--) {
                        Rec r = randomRec(rng, nextId++);
                        model.add(r);
                        index.add(r);
                    }
                    break;
                case 4: {
                    int mod = 2 + rng.nextInt(9);
                    int rem = rng.nextInt(mod);
                    Predicate<Rec> match = r -> r.id % mod == rem;
                    int expected = (int) model.stream().filter(match).count();
                    model.removeIf(match);
                    assertEquals(expected, index.removeIf(match));
                    break;
                }
                case 5:
                    if (!model.isEmpty()) {
                        Rec victim = model.remove(rng.nextInt(model.size()));
                        assertTrue(index.remove(victim));
                    }
                    break;
                case 6: {
                    int mod = 5 + rng.nextInt(50);
                    SplittableRandom mutation = rng.split();
                    int expected = (int) model.stream().filter(r -> r.id % mod == 0).count();
                    assertEquals(expected, index.updateIf(r -> r.id % mod == 0, r -> randomize(r, mutation)));
                    break;
                }
                case 7:
                    if (rng.nextInt(4) == 0) {
                        model.clear();
                        index.clear();
                    } else {
                        index.rebalance(names.get(rng.nextInt(names.size())));
                    }
                    break;
                default:
                    break;
            }
            String name = names.get(rng.nextInt(names.size()));
            Direction direction = rng.nextBoolean() ? Direction.ASCENDING : Direction.DESCENDING;
            index.sortBy(name, direction);
            checkMatches(index, model, rng);
            if (round % 10 == 0) {
                checkFull(index, model);
            }
            BucketStats stats = index.stats(name);
            assertEquals(model.size(), Arrays.stream(stats.actualSizes()).sum());
            assertEquals(stats.bucketCount(), stats.actualSizes().length);
        }
        for (String name : names) {
            index.sortBy(name, Direction.DESCENDING);
            checkFull(index, model);
        }
    }

    private static void checkMatches(BucketIndex<Rec> index, List<Rec> model, SplittableRandom rng) {
        List<Integer> expected = referenceIds(model, index.activeSortKey(), index.direction());
        assertEquals(expected.size(), index.size());
        if (expected.isEmpty()) {
            assertTrue(index.firstPage().isEmpty());
            return;
        }
        for (int i = 0; i < 20; i++) {
            int p = rng.nextInt(expected.size());
            assertEquals(expected.get(p), index.get(p).id, "position " + p);
        }
        int from = rng.nextInt(expected.size());
        int to = from + rng.nextInt(150);
        assertEquals(expected.subList(from, Math.min(to, expected.size())), ids(index.range(from, to)),
                () -> index + " " + index.plan());
        List<Rec> page = index.firstPage();
        assertEquals(Math.min(expected.size(), index.plan().pageSize()), page.size());
        assertEquals(expected.subList(0, page.size()), ids(page));
        Rec probe = model.get(rng.nextInt(model.size()));
        int position = index.positionOf(probe);
        assertEquals(probe.id, expected.get(position));
    }

    private static void checkFull(BucketIndex<Rec> index, List<Rec> model) {
        List<Integer> expected = referenceIds(model, index.activeSortKey(), index.direction());
        List<Integer> got = new ArrayList<>();
        for (Rec r : index) {
            got.add(r.id);
        }
        assertEquals(expected, got);
        assertEquals(expected, ids(index.asList()));
    }

    @Test
    void firstPageAfterSwitchingSortsOnlyAFewElements() {
        AtomicLong comparisons = new AtomicLong();
        BucketIndex<Rec> index = new BucketIndex<>();
        index.addSortKey(SortKey.ofComparator("nameCmp", (a, b) -> {
            comparisons.incrementAndGet();
            int c = a.name.compareTo(b.name);
            return c != 0 ? c : Integer.compare(a.id, b.id);
        }));
        index.addSortKey(SortKey.ofLong("size", r -> r.size));
        SplittableRandom rng = new SplittableRandom(3);
        int n = 100_000;
        List<Rec> batch = new ArrayList<>();
        for (int i = 0; i < n; i++) {
            Rec r = randomRec(rng, i);
            r.name += i;
            batch.add(r);
        }
        index.addAll(batch);
        assertFalse(index.needsRebalance("nameCmp"));

        comparisons.set(0);
        index.sortBy("nameCmp");
        assertEquals(0, comparisons.get(), "switching only groups by bucket id");
        assertEquals(64, index.firstPage().size());
        assertTrue(comparisons.get() < n / 10, "comparisons " + comparisons.get());

        index.sortBy("size");
        index.sortBy("nameCmp", Direction.DESCENDING);
        comparisons.set(0);
        index.firstPage();
        assertTrue(comparisons.get() < n / 10);

        comparisons.set(0);
        index.sortBy("nameCmp", Direction.ASCENDING);
        index.firstPage();
        assertEquals(0, comparisons.get(), "cached grouping and sorted bucket were kept");

        BucketStats stats = index.stats("nameCmp");
        assertTrue(stats.firstPageCostAscending() < 1000);
        assertTrue(stats.firstPageCostDescending() < 1000);
        assertTrue(stats.sortedBuckets() >= 2);
    }

    /**
     * Names sharing their first 8 bytes all get the same prefix key; the layout must still cut them
     * into page-sized buckets using the tie-breaker.
     */
    @Test
    void collidingPrefixKeysStayPageSized() {
        BucketIndex<Rec> index = new BucketIndex<>();
        index.addSortKey(SortKey.ofString("name", r -> r.name));
        SplittableRandom rng = new SplittableRandom(5);
        List<Rec> batch = new ArrayList<>();
        for (int i = 0; i < 50_000; i++) {
            batch.add(new Rec(i, (i % 2 == 1 ? "Document " : "IMG_20240101_") + rng.nextInt(1_000_000), 0, 0));
        }
        index.addAll(batch);
        BucketStats stats = index.stats("name");
        assertTrue(stats.firstPageCostAscending() < 300, "cost " + stats.firstPageCostAscending());
        assertTrue(stats.firstPageCostDescending() < 300, "cost " + stats.firstPageCostDescending());
        assertTrue(stats.maxSkew() < 2.0, "buckets follow the plan despite the collisions");
        assertFalse(index.layout("name").splitters().isEmpty());
        index.sortBy("name");
        checkFull(index, batch);
    }

    @Test
    void skewIsDetectedAndRebalanced() {
        BucketIndex<Long> index =
                new BucketIndex<>(BucketPlan.defaults().withAutoRebalance(false).withSampleSize(1 << 16));
        index.addSortKey(SortKey.ofLong("value", Long::longValue));
        List<Long> values = new ArrayList<>();
        for (long i = 0; i < 10_000; i++) {
            values.add(i);
        }
        index.addAll(values);
        assertTrue(index.needsRebalance("value"), "estimated from an empty collection");
        index.rebalance("value");
        assertFalse(index.needsRebalance("value"));
        assertTrue(index.bucketCount("value") > 10);
        // New, larger values all land in the last bucket (think: new files sorted by date).
        for (long i = 0; i < 2000; i++) {
            index.add(1_000_000 + i);
        }
        int[] sizes = index.bucketSizes("value");
        assertEquals(64 + 2000, sizes[sizes.length - 1]);
        assertTrue(index.needsRebalance("value"));
        assertTrue(index.stats("value").maxSkew() > 4.0);
        int[] estimate = index.estimateBucketSizes("value", index.plan());
        assertEquals(64, estimate[estimate.length - 1]);
        index.rebalance("value");
        sizes = index.bucketSizes("value");
        assertEquals(64, sizes[sizes.length - 1]);

        BucketIndex<Long> automatic = new BucketIndex<>();
        automatic.addSortKey(SortKey.ofLong("value", Long::longValue));
        automatic.addAll(values);
        assertFalse(automatic.needsRebalance("value"), "addAll re-estimated once at the end");
        for (long i = 0; i < 2000; i++) {
            automatic.add(1_000_000 + i);
        }
        automatic.sortBy("value", Direction.DESCENDING);
        assertEquals(1_000_000L + 1999, automatic.firstPage().get(0));
        assertFalse(automatic.needsRebalance("value"));
    }

    /** Folders first in both directions, with one pre-sort per key. */
    @Test
    void groupsStayFirstInBothDirections() {
        record Entry(String name, boolean dir) {
        }
        BucketIndex<Entry> index = new BucketIndex<>(BucketPlan.defaults().withPageSize(4));
        index.addSortKey(SortKey.<Entry>ofString("name", Entry::name).withGroups(2, e -> e.dir() ? 0 : 1));
        List<Entry> entries = new ArrayList<>();
        for (int i = 0; i < 300; i++) {
            entries.add(new Entry("f" + (1000 + i), false));
            if (i % 10 == 0) {
                entries.add(new Entry("d" + (1000 + i), true));
            }
        }
        index.addAll(entries);
        assertEquals(30, index.layout("name", 0).estimatedFrom());
        assertEquals(300, index.layout("name", 1).estimatedFrom());
        assertThrows(IndexOutOfBoundsException.class, () -> index.layout("name", 2));
        index.sortBy("name", Direction.DESCENDING);
        assertEquals("d1290", index.get(0).name());
        assertEquals("d1000", index.get(29).name());
        assertEquals("f1299", index.get(30).name());
        assertEquals("f1000", index.get(329).name());
        assertEquals(List.of("d1010", "d1000", "f1299", "f1298"), index.range(28, 32).stream().map(Entry::name).toList());
        assertTrue(index.stats("name").firstPageCostDescending() < 40);
        index.sortBy("name", Direction.ASCENDING);
        assertEquals("d1000", index.get(0).name());
        assertEquals("f1000", index.get(30).name());
        assertEquals(31, index.positionIf(e -> e.name().equals("f1001")));
        index.sortBy("name", Direction.DESCENDING);
        assertEquals(328, index.positionIf(e -> e.name().equals("f1001")));

        BucketIndex<Entry> bad = new BucketIndex<>();
        bad.addSortKey(SortKey.<Entry>ofString("g", Entry::name).withGroups(2, e -> 7));
        assertThrows(IndexOutOfBoundsException.class, () -> bad.add(new Entry("x", false)));
        assertThrows(IllegalArgumentException.class, () -> SortKey.<Entry>ofString("g", Entry::name).withGroups(0, e -> 0));
    }

    /**
     * A comparator with cycles (rock-paper-scissors) must not throw or lose elements. Real
     * comparators have such bugs: Dolphin's natural compare orders kernel-6.10 &lt; kernel-6.9 &lt;
     * kernel-6.9.1 &lt; kernel-6.10.
     */
    @Test
    void inconsistentComparatorDoesNotBreakTheIndex() {
        java.util.Comparator<Rec> rps = (a, b) -> {
            int x = a.id % 3;
            int y = b.id % 3;
            if (x == y) {
                return Integer.compare(a.id, b.id);
            }
            return (x + 1) % 3 == y ? -1 : 1;
        };
        // Antisymmetric but otherwise random per pair: makes TimSort throw
        // "Comparison method violates its general contract", which the layout must survive.
        java.util.Comparator<Rec> chaos = (a, b) -> {
            if (a.id == b.id) {
                return 0;
            }
            int lo = Math.min(a.id, b.id);
            int hi = Math.max(a.id, b.id);
            long h = (lo * 0x9E3779B97F4A7C15L) ^ (hi * 0xC2B2AE3D27D4EB4FL);
            h ^= h >>> 29;
            int sign = (h & 1) == 0 ? -1 : 1;
            return a.id == lo ? sign : -sign;
        };
        BucketIndex<Rec> index = new BucketIndex<>(BucketPlan.defaults().withPageSize(8));
        index.addSortKey(SortKey.ofComparator("rps", rps));
        index.addSortKey(SortKey.ofKey("rpsTie", r -> 7L, rps));
        index.addSortKey(SortKey.ofComparator("chaos", chaos));
        index.addSortKey(SortKey.ofKey("chaosTie", r -> 7L, chaos));
        List<Rec> batch = new ArrayList<>();
        for (int i = 0; i < 5000; i++) {
            batch.add(new Rec(i, "", 0, 0));
        }
        index.addAll(batch);
        for (String key : List.of("rps", "rpsTie", "chaos", "chaosTie")) {
            for (Direction direction : Direction.values()) {
                index.sortBy(key, direction);
                List<Integer> got = ids(index.asList());
                got.sort(null);
                for (int i = 0; i < batch.size(); i++) {
                    assertEquals(i, got.get(i));
                }
            }
        }
    }

    @Test
    void edgeCases() {
        BucketIndex<Rec> index = new BucketIndex<>();
        assertTrue(index.isEmpty());
        assertTrue(index.firstPage().isEmpty());
        assertThrows(IndexOutOfBoundsException.class, () -> index.get(0));
        assertThrows(IllegalArgumentException.class, () -> index.sortBy("missing"));
        index.addSortKey(SortKey.ofString("name", r -> r.name));
        assertThrows(IllegalArgumentException.class, () -> index.addSortKey(SortKey.ofString("name", r -> r.name)));
        assertThrows(IllegalArgumentException.class, () -> SortKey.ofLong("", (Rec r) -> r.size));
        assertThrows(UnsupportedOperationException.class,
                () -> SortKey.<Rec>ofComparator("c", (a, b) -> 0).key(new Rec(0, "", 0, 0)));
        index.sortBy("name");
        assertTrue(index.firstPage().isEmpty());
        assertTrue(index.range(5, 2).isEmpty());

        Rec b = new Rec(1, "b", 0, 0);
        Rec a = new Rec(2, "a", 0, 0);
        index.add(b);
        index.add(a);
        assertEquals("a", index.get(0).name);
        assertEquals(2, index.range(0, 100).size());
        assertEquals(0, index.bucketOf(a, "name"));

        // Changing an element without telling the index is not allowed; update() fixes the order.
        a.name = "c";
        assertTrue(index.update(a));
        assertEquals("b", index.get(0).name);

        Iterator<Rec> it = index.iterator();
        it.next();
        index.add(new Rec(3, "d", 0, 0));
        assertThrows(ConcurrentModificationException.class, it::next);

        assertTrue(index.removeSortKey("name"));
        assertNull(index.activeSortKey());
        assertEquals("b", index.get(0).name);  // insertion order
        assertFalse(index.removeSortKey("name"));
        assertEquals(-1, index.positionOf(new Rec(99, "", 0, 0)));
        assertEquals(1, index.positionOf(a));
    }
}
