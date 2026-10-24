package io.github.attilatorda.speedup.bucketization;

import java.util.AbstractList;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.BitSet;
import java.util.Collection;
import java.util.ConcurrentModificationException;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.RandomAccess;
import java.util.function.Consumer;
import java.util.function.Predicate;

/**
 * Reference implementation of {@link Bucketization}.
 *
 * <p>Holds the elements and, per sort key, their normalized keys, bucket ids and a cached grouping
 * of the elements by bucket.
 *
 * <p>Costs (n elements, k buckets, p page size):
 *
 * <ul>
 *   <li>add: O(sort keys * log k); comparator-only keys pay log k comparisons
 *   <li>sortBy: O(n + k), or O(1) when the sort key's grouping is still cached
 *   <li>first page: sorts the bucket(s) covering it, about O(p log p)
 *   <li>removeIf / updateIf: O(n) per call, independent of the number of matches
 * </ul>
 *
 * @param <T> element type
 */
public final class BucketIndex<T> implements Bucketization<T> {

    private BucketPlan plan;
    private Object[] items = new Object[16];
    private int size;
    private final List<KeyState<T>> states = new ArrayList<>();
    private KeyState<T> active;
    private Direction direction = Direction.ASCENDING;
    private int modCount;

    public BucketIndex() {
        this(BucketPlan.defaults());
    }

    public BucketIndex(BucketPlan plan) {
        this.plan = Objects.requireNonNull(plan, "plan");
    }

    // --- sort keys ---------------------------------------------------------

    @Override
    public void addSortKey(SortKey<T> key) {
        Objects.requireNonNull(key, "key");
        if (findState(key.name()) != null) {
            throw new IllegalArgumentException("duplicate sort key '" + key.name() + "'");
        }
        KeyState<T> state = new KeyState<>(key, items.length);
        if (key.hasKey()) {
            for (int slot = 0; slot < size; slot++) {
                state.keys[slot] = key.key(item(slot));
            }
        }
        relayout(state);
        states.add(state);
        modCount++;
    }

    @Override
    public boolean removeSortKey(String name) {
        KeyState<T> state = findState(name);
        if (state == null) {
            return false;
        }
        states.remove(state);
        if (active == state) {
            active = null;
        }
        modCount++;
        return true;
    }

    @Override
    public List<String> sortKeyNames() {
        List<String> names = new ArrayList<>(states.size());
        for (KeyState<T> state : states) {
            names.add(state.key.name());
        }
        return names;
    }

    // --- selecting the order -------------------------------------------------

    @Override
    public void sortBy(String name, Direction direction) {
        active = requireState(name);
        this.direction = Objects.requireNonNull(direction, "direction");
        prepare(active);
        modCount++;
    }

    @Override
    public void unsort() {
        active = null;
        modCount++;
    }

    @Override
    public SortKey<T> activeSortKey() {
        return active == null ? null : active.key;
    }

    @Override
    public Direction direction() {
        return direction;
    }

    // --- reading in order ------------------------------------------------------

    @Override
    public int size() {
        return size;
    }

    @Override
    public T get(int position) {
        Objects.checkIndex(position, size);
        if (active == null) {
            return item(position);
        }
        prepare(active);
        int ascending = toAscending(position);
        sortBucket(active, bucketAt(active, ascending));
        return item(active.order[ascending]);
    }

    @Override
    public List<T> range(int from, int to) {
        if (from < 0) {
            throw new IndexOutOfBoundsException("from < 0: " + from);
        }
        to = Math.min(to, size);
        if (from >= to) {
            return new ArrayList<>();
        }
        List<T> out = new ArrayList<>(to - from);
        if (active == null) {
            for (int slot = from; slot < to; slot++) {
                out.add(item(slot));
            }
            return out;
        }
        KeyState<T> state = active;
        prepare(state);
        int lo = direction == Direction.ASCENDING ? from : size - to;
        int hi = direction == Direction.ASCENDING ? to : size - from;
        int lastBucket = bucketAt(state, hi - 1);
        for (int b = bucketAt(state, lo); b <= lastBucket; b++) {
            sortBucket(state, b);
        }
        if (direction == Direction.ASCENDING) {
            for (int i = lo; i < hi; i++) {
                out.add(item(state.order[i]));
            }
        } else {
            for (int i = hi - 1; i >= lo; i--) {
                out.add(item(state.order[i]));
            }
        }
        return out;
    }

    @Override
    public List<T> firstPage() {
        return range(0, plan.pageSize());
    }

    @Override
    public int positionIf(Predicate<? super T> filter) {
        int slot = 0;
        while (slot < size && !filter.test(item(slot))) {
            slot++;
        }
        if (slot == size) {
            return -1;
        }
        if (active == null) {
            return slot;
        }
        KeyState<T> state = active;
        prepare(state);
        int bucket = state.buckets[slot];
        sortBucket(state, bucket);
        for (int i = state.start[bucket]; i < state.start[bucket + 1]; i++) {
            if (state.order[i] == slot) {
                return toAscending(i);
            }
        }
        throw new IllegalStateException("slot missing from its bucket");
    }

    @Override
    public void sortAll() {
        if (active == null) {
            return;
        }
        prepare(active);
        for (int b = 0; b < active.layout.bucketCount(); b++) {
            sortBucket(active, b);
        }
    }

    /** Iterates in the current order, sorting buckets as they are reached. Fail-fast. */
    @Override
    public Iterator<T> iterator() {
        return new Iterator<>() {
            private final int expectedModCount = modCount;
            private int position;

            @Override
            public boolean hasNext() {
                return position < size;
            }

            @Override
            public T next() {
                if (modCount != expectedModCount) {
                    throw new ConcurrentModificationException();
                }
                if (position >= size) {
                    throw new NoSuchElementException();
                }
                return get(position++);
            }
        };
    }

    /**
     * Read-only {@link List} view of the current order. Reading an element sorts its bucket, so
     * the view stays lazy; it reflects later changes to the index.
     */
    public List<T> asList() {
        return new SortedView();
    }

    // --- buckets and distribution ------------------------------------------------

    @Override
    public int bucketOf(T element, String key) {
        KeyState<T> state = requireState(key);
        return state.layout.bucketOf(element, state.key);
    }

    @Override
    public int bucketCount(String key) {
        return requireState(key).layout.bucketCount();
    }

    @Override
    public int[] bucketSizes(String key) {
        return requireState(key).counts.clone();
    }

    @Override
    public BucketLayout<T> layout(String key) {
        return requireState(key).layout;
    }

    @Override
    public BucketStats stats(String key) {
        KeyState<T> state = requireState(key);
        int k = state.layout.bucketCount();
        int empty = 0;
        int sorted = 0;
        double maxSkew = 0.0;
        for (int b = 0; b < k; b++) {
            if (state.counts[b] == 0) {
                empty++;
            }
            if (state.hasOrder && !state.orderStale && state.sortedLen[b] == state.counts[b]) {
                sorted++;
            }
            maxSkew = Math.max(maxSkew, (double) state.counts[b] / skewFloor(state, b));
        }
        int page = Math.min(plan.pageSize(), size);
        int costAscending = 0;
        for (int b = 0; b < k && costAscending < page; b++) {
            costAscending += state.counts[b];
        }
        int costDescending = 0;
        for (int b = k - 1; b >= 0 && costDescending < page; b--) {
            costDescending += state.counts[b];
        }
        return new BucketStats(state.key.name(), size, k, state.counts.clone(), state.layout.expectedSizes(), empty,
                sorted, maxSkew, costAscending, costDescending, isStale(state));
    }

    @Override
    public int[] estimateBucketSizes(String key, BucketPlan plan) {
        KeyState<T> state = requireState(key);
        return BucketLayout.estimate(state.key, elementView(), state.keys, plan).expectedSizes();
    }

    @Override
    public boolean needsRebalance(String key) {
        return isStale(requireState(key));
    }

    @Override
    public void rebalance(String key) {
        relayout(requireState(key));
        modCount++;
    }

    @Override
    public void rebalanceAll() {
        for (KeyState<T> state : states) {
            relayout(state);
        }
        modCount++;
    }

    @Override
    public BucketPlan plan() {
        return plan;
    }

    @Override
    public void setPlan(BucketPlan plan) {
        this.plan = Objects.requireNonNull(plan, "plan");
        rebalanceAll();
    }

    // --- changing the collection ---------------------------------------------------

    @Override
    public void add(T element) {
        ensureCapacity(size + 1);
        append(element);
        modCount++;
    }

    @Override
    public void addAll(Collection<? extends T> elements) {
        ensureCapacity(size + elements.size());
        for (T element : elements) {
            append(element);
        }
        for (KeyState<T> state : states) {
            maybeAutoRebalance(state);
        }
        modCount++;
    }

    @Override
    public int removeIf(Predicate<? super T> filter) {
        int[] remap = new int[size];
        int kept = 0;
        for (int slot = 0; slot < size; slot++) {
            remap[slot] = filter.test(item(slot)) ? -1 : kept++;
        }
        int removed = size - kept;
        if (removed == 0) {
            return 0;
        }
        for (KeyState<T> state : states) {
            if (state.hasOrder && state.orderStale) {
                regroup(state);
            }
        }
        for (int slot = 0; slot < size; slot++) {
            if (remap[slot] >= 0) {
                items[remap[slot]] = items[slot];
            }
        }
        Arrays.fill(items, kept, size, null);
        int oldSize = size;
        size = kept;
        for (KeyState<T> state : states) {
            compact(state, remap, oldSize, kept);
        }
        modCount++;
        return removed;
    }

    @Override
    public int updateIf(Predicate<? super T> filter, Consumer<? super T> mutator) {
        int updated = 0;
        for (int slot = 0; slot < size; slot++) {
            T element = item(slot);
            if (!filter.test(element)) {
                continue;
            }
            if (mutator != null) {
                mutator.accept(element);
            }
            for (KeyState<T> state : states) {
                rekey(state, slot);
            }
            updated++;
        }
        for (KeyState<T> state : states) {
            if (state.splitterChanged) {
                relayout(state);
            }
        }
        if (updated > 0) {
            modCount++;
        }
        return updated;
    }

    @Override
    public void clear() {
        Arrays.fill(items, 0, size, null);
        size = 0;
        for (KeyState<T> state : states) {
            relayout(state);
        }
        modCount++;
    }

    @Override
    public String toString() {
        return "BucketIndex[size=" + size + ", keys=" + sortKeyNames() + ", active="
                + (active == null ? "none" : active.key.name() + " " + direction) + "]";
    }

    // --- internals -------------------------------------------------------------------

    /** Everything the index keeps per sort key. Slots are positions in {@code items}. */
    private static final class KeyState<T> {
        final SortKey<T> key;
        long[] keys;          // normalized key per slot (keyed sort keys only)
        char[] buckets;       // bucket id per slot, 16-bit unsigned
        BucketLayout<T> layout = BucketLayout.single(0);
        int[] counts = {0};   // elements per bucket
        int churn;            // adds, removals and updates since the layout was made
        int staleCheckedAt = -1;

        // Layouts may split on the elements themselves (see BucketLayout), so
        // changing or removing a splitter element invalidates the layout.
        int[] splitterSlots = new int[0];  // sorted slots the splitters were taken from
        boolean splitterChanged;

        // Cached grouping by bucket; built on first use, kept across sortBy calls.
        boolean hasOrder;
        boolean orderStale;   // adds or updates are not placed yet
        int[] order;          // slots grouped by bucket, ascending bucket order
        int[] start;          // bucket b occupies order[start[b], start[b+1])
        int[] sortedLen;      // length of each bucket's sorted prefix
        int orderedSlots;     // slots below this are present in order
        final BitSet moved = new BitSet();  // slot was updated and must be re-placed

        KeyState(SortKey<T> key, int capacity) {
            this.key = key;
            this.keys = key.hasKey() ? new long[capacity] : null;
            this.buckets = new char[capacity];
        }
    }

    /** Read-only list over the current order. */
    private final class SortedView extends AbstractList<T> implements RandomAccess {
        @Override
        public T get(int index) {
            return BucketIndex.this.get(index);
        }

        @Override
        public int size() {
            return size;
        }
    }

    @SuppressWarnings("unchecked")
    private T item(int slot) {
        return (T) items[slot];
    }

    /** The elements in insertion order, without copying. */
    private List<T> elementView() {
        return new AbstractList<>() {
            @Override
            public T get(int index) {
                return item(index);
            }

            @Override
            public int size() {
                return size;
            }
        };
    }

    private KeyState<T> findState(String name) {
        for (KeyState<T> state : states) {
            if (state.key.name().equals(name)) {
                return state;
            }
        }
        return null;
    }

    private KeyState<T> requireState(String name) {
        KeyState<T> state = findState(name);
        if (state == null) {
            throw new IllegalArgumentException("unknown sort key '" + name + "'");
        }
        return state;
    }

    private int toAscending(int position) {
        return direction == Direction.ASCENDING ? position : size - 1 - position;
    }

    private static int bucketAt(KeyState<?> state, int ascendingPosition) {
        // Last bucket whose start is <= position.
        int lo = 0;
        int hi = state.start.length;
        while (lo < hi) {
            int mid = (lo + hi) >>> 1;
            if (state.start[mid] <= ascendingPosition) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo - 1;
    }

    private int skewFloor(KeyState<T> state, int bucket) {
        return Math.max(Math.max(state.layout.expectedSize(bucket), plan.pageSize()), 1);
    }

    private boolean isStale(KeyState<T> state) {
        if (size > 2L * Math.max(state.layout.estimatedFrom(), plan.pageSize())) {
            return true;
        }
        for (int b = 0; b < state.counts.length; b++) {
            if (state.counts[b] > plan.rebalanceSkew() * skewFloor(state, b)) {
                return true;
            }
        }
        return false;
    }

    /** Automatic rebalancing waits until enough has changed to pay for itself. */
    private void maybeAutoRebalance(KeyState<T> state) {
        if (!plan.autoRebalance() || state.churn == state.staleCheckedAt) {
            return;
        }
        state.staleCheckedAt = state.churn;
        if (state.churn >= Math.max(plan.pageSize(), state.layout.estimatedFrom() / 8) && isStale(state)) {
            relayout(state);
        }
    }

    /** Makes the grouping of {@code state} current. Called before every sorted read. */
    private void prepare(KeyState<T> state) {
        maybeAutoRebalance(state);
        if (!state.hasOrder) {
            buildOrder(state);
        } else if (state.orderStale) {
            regroup(state);
        }
    }

    private static boolean isSplitter(KeyState<?> state, int slot) {
        return Arrays.binarySearch(state.splitterSlots, slot) >= 0;
    }

    private void relayout(KeyState<T> state) {
        state.layout = BucketLayout.estimate(state.key, elementView(), state.keys, plan);
        state.splitterSlots = state.layout.splitterPositionsUnsafe().clone();
        Arrays.sort(state.splitterSlots);
        state.splitterChanged = false;
        state.counts = new int[state.layout.bucketCount()];
        for (int slot = 0; slot < size; slot++) {
            int b = state.keys != null ? state.layout.bucketOf(item(slot), state.keys[slot], state.key)
                    : state.layout.bucketOf(item(slot), state.key);
            state.buckets[slot] = (char) b;
            state.counts[b]++;
        }
        state.churn = 0;
        state.staleCheckedAt = -1;
        state.hasOrder = false;
        state.orderStale = false;
        state.order = null;
        state.start = null;
        state.sortedLen = null;
        state.orderedSlots = 0;
        state.moved.clear();
    }

    private void ensureCapacity(int minimum) {
        if (minimum <= items.length) {
            return;
        }
        int capacity = Math.max(minimum, items.length + (items.length >> 1));
        items = Arrays.copyOf(items, capacity);
        for (KeyState<T> state : states) {
            if (state.keys != null) {
                state.keys = Arrays.copyOf(state.keys, capacity);
            }
            state.buckets = Arrays.copyOf(state.buckets, capacity);
        }
    }

    private void append(T element) {
        int slot = size;
        items[slot] = element;
        size++;
        for (KeyState<T> state : states) {
            int b;
            if (state.keys != null) {
                long k = state.key.key(element);
                state.keys[slot] = k;
                b = state.layout.bucketOf(element, k, state.key);
            } else {
                b = state.layout.bucketOf(element, state.key);
            }
            state.buckets[slot] = (char) b;
            state.counts[b]++;
            state.churn++;
            if (state.hasOrder) {
                state.orderStale = true;
            }
        }
    }

    private void rekey(KeyState<T> state, int slot) {
        int b;
        if (state.keys != null) {
            state.keys[slot] = state.key.key(item(slot));
            b = state.layout.bucketOf(item(slot), state.keys[slot], state.key);
        } else {
            b = state.layout.bucketOf(item(slot), state.key);
        }
        state.counts[state.buckets[slot]]--;
        state.buckets[slot] = (char) b;
        state.counts[b]++;
        state.churn++;
        if (isSplitter(state, slot)) {
            state.splitterChanged = true;
        }
        if (state.hasOrder && slot < state.orderedSlots) {
            state.moved.set(slot);
            state.orderStale = true;
        }
    }

    private static int[] bucketStarts(int[] counts) {
        int[] start = new int[counts.length + 1];
        for (int b = 0; b < counts.length; b++) {
            start[b + 1] = start[b] + counts[b];
        }
        return start;
    }

    /** Counting sort of all slots by bucket id. Nothing is sorted yet. */
    private void buildOrder(KeyState<T> state) {
        int k = state.layout.bucketCount();
        state.start = bucketStarts(state.counts);
        int[] fill = Arrays.copyOf(state.start, k);
        state.order = new int[size];
        for (int slot = 0; slot < size; slot++) {
            state.order[fill[state.buckets[slot]]++] = slot;
        }
        state.sortedLen = new int[k];
        state.moved.clear();
        state.orderedSlots = size;
        state.hasOrder = true;
        state.orderStale = false;
    }

    /**
     * Places added and updated slots without losing the sorted prefixes. Slots that stayed keep
     * their relative order at the front of their bucket, so the part of a sorted prefix that
     * survived is still sorted. Added and updated slots go to the (unsorted) back of their bucket.
     */
    private void regroup(KeyState<T> state) {
        int k = state.layout.bucketCount();
        int[] start = bucketStarts(state.counts);
        int[] fill = Arrays.copyOf(start, k);
        int[] sortedLen = new int[k];
        int[] order = new int[size];
        int[] deferred = new int[state.moved.cardinality()];
        int deferredCount = 0;
        for (int b = 0; b < k; b++) {
            int prefixEnd = state.start[b] + state.sortedLen[b];
            for (int i = state.start[b]; i < state.start[b + 1]; i++) {
                int slot = state.order[i];
                if (state.moved.get(slot)) {
                    deferred[deferredCount++] = slot;
                    continue;
                }
                order[fill[b]++] = slot;
                if (i < prefixEnd) {
                    sortedLen[b]++;
                }
            }
        }
        for (int i = 0; i < deferredCount; i++) {
            int slot = deferred[i];
            order[fill[state.buckets[slot]]++] = slot;
        }
        for (int slot = state.orderedSlots; slot < size; slot++) {
            order[fill[state.buckets[slot]]++] = slot;
        }
        state.order = order;
        state.start = start;
        state.sortedLen = sortedLen;
        state.moved.clear();
        state.orderedSlots = size;
        state.orderStale = false;
    }

    /**
     * Applies a removal to one sort key. The grouping is filtered in place, which keeps every
     * bucket's surviving sorted prefix sorted.
     */
    private void compact(KeyState<T> state, int[] remap, int oldSize, int kept) {
        int write = 0;
        for (int slot = 0; slot < oldSize; slot++) {
            if (remap[slot] < 0) {
                state.counts[state.buckets[slot]]--;
                continue;
            }
            if (state.keys != null) {
                state.keys[write] = state.keys[slot];
            }
            state.buckets[write] = state.buckets[slot];
            write++;
        }
        state.churn += oldSize - kept;
        for (int i = 0; i < state.splitterSlots.length; i++) {
            int mapped = remap[state.splitterSlots[i]];
            if (mapped < 0) {
                relayout(state);
                return;
            }
            state.splitterSlots[i] = mapped;
        }
        if (!state.hasOrder) {
            return;
        }
        int k = state.layout.bucketCount();
        int out = 0;
        int[] start = new int[k + 1];
        for (int b = 0; b < k; b++) {
            int prefixEnd = state.start[b] + state.sortedLen[b];
            int sorted = 0;
            for (int i = state.start[b]; i < state.start[b + 1]; i++) {
                int mapped = remap[state.order[i]];
                if (mapped < 0) {
                    continue;
                }
                state.order[out++] = mapped;
                if (i < prefixEnd) {
                    sorted++;
                }
            }
            state.sortedLen[b] = sorted;
            start[b + 1] = out;
        }
        state.order = Arrays.copyOf(state.order, kept);
        state.start = start;
        state.moved.clear();
        state.orderedSlots = kept;
    }

    /**
     * Sorts the unsorted tail of a bucket and merges it into the sorted prefix. Order: key,
     * tie-breaker, insertion order (slot numbers keep insertion order).
     */
    private void sortBucket(KeyState<T> state, int bucket) {
        int from = state.start[bucket];
        int to = state.start[bucket + 1];
        int sorted = state.sortedLen[bucket];
        if (sorted == to - from) {
            return;
        }
        SlotSorter.SlotComparator comparator = slotComparator(state);
        int mid = from + sorted;
        int[] tmp = new int[Math.max(to - mid, sorted)];
        SlotSorter.sort(state.order, tmp, mid, to, comparator);
        SlotSorter.merge(state.order, tmp, from, mid, to, comparator);
        state.sortedLen[bucket] = to - from;
    }

    private SlotSorter.SlotComparator slotComparator(KeyState<T> state) {
        SortKey<T> key = state.key;
        long[] keys = state.keys;
        if (keys != null) {
            return (a, b) -> {
                int c = Long.compareUnsigned(keys[a], keys[b]);
                if (c == 0) {
                    c = key.tieBreak(item(a), item(b));
                }
                return c != 0 ? c : Integer.compare(a, b);
            };
        }
        return (a, b) -> {
            int c = key.compare(item(a), item(b));
            return c != 0 ? c : Integer.compare(a, b);
        };
    }
}
