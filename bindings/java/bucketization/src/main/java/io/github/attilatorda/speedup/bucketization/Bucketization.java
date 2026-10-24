package io.github.attilatorda.speedup.bucketization;

import java.util.Collection;
import java.util.List;
import java.util.Objects;
import java.util.function.Consumer;
import java.util.function.Predicate;

/**
 * A collection whose elements can be sorted by any of several sort keys, where switching between
 * them is cheap.
 *
 * <p>Every element is assigned to a bucket per sort key once, when it is added ("pre-sorting").
 * Selecting a sort key then only groups the elements by bucket id (a counting pass, no
 * comparisons), and a bucket is fully sorted the first time a position inside it is read. A view
 * showing the first rows therefore pays for O(n + page log page) work instead of O(n log n).
 *
 * <p>Positions are indexes into the current order (sort key and direction). Bucket ids always
 * follow ascending key order. Descending order is the exact reverse of ascending order. Equal
 * elements keep insertion order in ascending order.
 *
 * <p>Reads may sort lazily, so implementations are not thread-safe, not even for concurrent
 * reads. Elements must not be changed in a way that affects their order without calling {@link
 * #update} or {@link #updateIf} afterwards. Iterators are fail-fast.
 *
 * @param <T> element type
 */
public interface Bucketization<T> extends Iterable<T> {

    // --- sort keys ---------------------------------------------------------

    /**
     * Registers a sort key and assigns every element to one of its buckets.
     *
     * @throws IllegalArgumentException if the name is already taken
     */
    void addSortKey(SortKey<T> key);

    /** Removes a sort key. If it was active the order reverts to insertion order. */
    boolean removeSortKey(String name);

    List<String> sortKeyNames();

    // --- selecting the order -------------------------------------------------

    /**
     * Switches the order: O(n) grouping, no comparisons. Switching back to a sort key used before
     * reuses its cached grouping and sorted buckets.
     *
     * @throws IllegalArgumentException for an unknown name
     */
    void sortBy(String name, Direction direction);

    default void sortBy(String name) {
        sortBy(name, Direction.ASCENDING);
    }

    /** Back to insertion order. */
    void unsort();

    /** The active sort key, or {@code null} in insertion order. */
    SortKey<T> activeSortKey();

    Direction direction();

    // --- reading in order ------------------------------------------------------

    int size();

    default boolean isEmpty() {
        return size() == 0;
    }

    /**
     * Element at {@code position} in the current order (sorts its bucket if needed).
     *
     * @throws IndexOutOfBoundsException if the position is out of range
     */
    T get(int position);

    /** Elements at positions [from, to), clamped to size(). Typically the visible rows of a view. */
    List<T> range(int from, int to);

    /** The first {@code plan().pageSize()} elements. */
    List<T> firstPage();

    /** Position of the first element (in insertion order) matching {@code filter}, or -1. */
    int positionIf(Predicate<? super T> filter);

    /** Position of an element equal to {@code element}, or -1. */
    default int positionOf(Object element) {
        return positionIf(e -> Objects.equals(e, element));
    }

    /** Fully sorts every bucket of the active sort key now (e.g. when idle). */
    void sortAll();

    // --- buckets and distribution ------------------------------------------------

    /**
     * Bucket {@code element} belongs to under the named sort key's current layout. The element
     * does not have to be in the collection.
     */
    int bucketOf(T element, String key);

    int bucketCount(String key);

    /** Current number of elements per bucket. */
    int[] bucketSizes(String key);

    /** Current boundaries and expected sizes. */
    BucketLayout<T> layout(String key);

    BucketStats stats(String key);

    /** Bucket sizes {@code plan} would produce on the current data (sample based, nothing is changed). */
    int[] estimateBucketSizes(String key, BucketPlan plan);

    /**
     * True when the collection grew past twice the size the layout was estimated from, or a bucket
     * exceeds the plan's skew limit.
     */
    boolean needsRebalance(String key);

    /** Re-estimates the layout from the current data and reassigns buckets. */
    void rebalance(String key);

    void rebalanceAll();

    BucketPlan plan();

    /** Replaces the plan and rebalances every sort key. */
    void setPlan(BucketPlan plan);

    // --- changing the collection ---------------------------------------------------

    void add(T element);

    /** Bulk insert; stale layouts are re-estimated once at the end. */
    void addAll(Collection<? extends T> elements);

    /** Removes every matching element; returns how many were removed. */
    int removeIf(Predicate<? super T> filter);

    default boolean remove(Object element) {
        return removeIf(e -> Objects.equals(e, element)) > 0;
    }

    /**
     * Applies {@code mutator} (nullable) to every matching element and re-reads its keys. Call it
     * after anything that changes an element's order.
     */
    int updateIf(Predicate<? super T> filter, Consumer<? super T> mutator);

    default int updateIf(Predicate<? super T> filter) {
        return updateIf(filter, null);
    }

    /** Re-reads the keys of elements equal to {@code element}, e.g. after a file was modified. */
    default boolean update(T element) {
        return updateIf(e -> Objects.equals(e, element), null) > 0;
    }

    void clear();
}
