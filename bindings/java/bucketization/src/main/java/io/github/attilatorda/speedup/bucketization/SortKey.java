package io.github.attilatorda.speedup.bucketization;

import java.text.Collator;
import java.util.Comparator;
import java.util.Objects;
import java.util.function.Function;
import java.util.function.ToDoubleFunction;
import java.util.function.ToLongFunction;

/**
 * One way of ordering the elements of a collection (name, size, modified, ...).
 *
 * <p>There are two kinds:
 *
 * <ul>
 *   <li><b>keyed</b> ({@link #ofKey}, {@link #ofLong}, {@link #ofString}, ...): every element maps
 *       to a normalized 64-bit key (see {@link Keys}). The order is "key first, then the optional
 *       tie-breaker". Buckets are cut on the key, so assigning an element to its bucket costs a
 *       binary search over a few longs.
 *   <li><b>comparator-only</b> ({@link #ofComparator}): any comparator, e.g. natural ("file2" &lt;
 *       "file10") ordering. Buckets are cut on sampled splitter elements, so assigning an element
 *       costs O(log buckets) comparator calls, paid once when the element is added.
 * </ul>
 *
 * <p>A sort key is itself a {@link Comparator} for the exact order it defines.
 *
 * @param <T> element type
 */
public final class SortKey<T> implements Comparator<T> {

    private final String name;
    private final ToLongFunction<? super T> keyFunction;
    private final Comparator<? super T> comparator;

    private SortKey(String name, ToLongFunction<? super T> keyFunction, Comparator<? super T> comparator) {
        this.name = Objects.requireNonNull(name, "name");
        if (name.isEmpty()) {
            throw new IllegalArgumentException("SortKey: name must not be empty");
        }
        this.keyFunction = keyFunction;
        this.comparator = comparator;
    }

    /**
     * Keyed sort key. {@code key} must return a normalized key (see {@link Keys});
     * {@code tieBreaker} (nullable) orders elements whose keys are equal, which is where a prefix
     * key such as {@link Keys#fromString} needs the full comparison.
     */
    public static <T> SortKey<T> ofKey(String name, ToLongFunction<? super T> key, Comparator<? super T> tieBreaker) {
        return new SortKey<>(name, Objects.requireNonNull(key, "key"), tieBreaker);
    }

    /** Keyed sort key whose normalized key is the whole order. */
    public static <T> SortKey<T> ofKey(String name, ToLongFunction<? super T> key) {
        return ofKey(name, key, null);
    }

    /** Comparator-only sort key. */
    public static <T> SortKey<T> ofComparator(String name, Comparator<? super T> comparator) {
        return new SortKey<>(name, null, Objects.requireNonNull(comparator, "comparator"));
    }

    /** Signed {@code long} (or {@code int}) values: sizes, timestamps, counters. Exact. */
    public static <T> SortKey<T> ofLong(String name, ToLongFunction<? super T> value) {
        Objects.requireNonNull(value, "value");
        return ofKey(name, e -> Keys.fromSigned(value.applyAsLong(e)));
    }

    /** {@code double} values in {@link Double#compare} order. Exact. */
    public static <T> SortKey<T> ofDouble(String name, ToDoubleFunction<? super T> value) {
        Objects.requireNonNull(value, "value");
        return ofKey(name, e -> Keys.fromDouble(value.applyAsDouble(e)));
    }

    /** Strings in {@link String#compareTo} order: prefix key plus full-string tie-breaker. */
    public static <T> SortKey<T> ofString(String name, Function<? super T, ? extends CharSequence> value) {
        Objects.requireNonNull(value, "value");
        return ofKey(name, e -> Keys.fromString(value.apply(e)),
                (a, b) -> CharSequence.compare(value.apply(a), value.apply(b)));
    }

    /** Strings in {@link Keys#IGNORE_CASE_ORDER}. */
    public static <T> SortKey<T> ofStringIgnoreCase(String name, Function<? super T, ? extends CharSequence> value) {
        Objects.requireNonNull(value, "value");
        return ofKey(name, e -> Keys.fromStringIgnoreCase(value.apply(e)),
                (a, b) -> Keys.compareIgnoreCase(value.apply(a), value.apply(b)));
    }

    /**
     * Locale-aware order: the order of the collator's {@link java.text.CollationKey}s. The key is
     * the first 8 bytes of the collation key and the tie-breaker compares whole collation keys, so
     * the two always agree. (The JDK's {@link Collator#compare} matches its collation keys for
     * ordinary text but can disagree on noncharacters such as U+FFFF.) The collator is cloned.
     */
    public static <T> SortKey<T> ofCollated(String name, Function<? super T, String> value, Collator collator) {
        Objects.requireNonNull(value, "value");
        Collator own = (Collator) Objects.requireNonNull(collator, "collator").clone();
        return ofKey(name, e -> Keys.fromBytes(own.getCollationKey(value.apply(e)).toByteArray()),
                (a, b) -> own.getCollationKey(value.apply(a)).compareTo(own.getCollationKey(value.apply(b))));
    }

    public String name() {
        return name;
    }

    /** True for keyed sort keys. */
    public boolean hasKey() {
        return keyFunction != null;
    }

    /**
     * Normalized key of {@code element}.
     *
     * @throws UnsupportedOperationException for comparator-only sort keys
     */
    public long key(T element) {
        if (keyFunction == null) {
            throw new UnsupportedOperationException("comparator-only sort key '" + name + "' has no key");
        }
        return keyFunction.applyAsLong(element);
    }

    /**
     * Orders elements with equal keys (keyed), or is the whole order (comparator-only). Returns 0
     * when there is no tie-breaker.
     */
    public int tieBreak(T a, T b) {
        return comparator == null ? 0 : comparator.compare(a, b);
    }

    /** The exact order this sort key defines. */
    @Override
    public int compare(T a, T b) {
        if (keyFunction != null) {
            int c = Long.compareUnsigned(keyFunction.applyAsLong(a), keyFunction.applyAsLong(b));
            if (c != 0) {
                return c;
            }
        }
        return tieBreak(a, b);
    }

    @Override
    public String toString() {
        return "SortKey[" + name + (hasKey() ? ", keyed" : ", comparator") + "]";
    }
}
