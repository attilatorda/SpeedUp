package io.github.attilatorda.speedup.bucketization;

import java.util.Comparator;

/**
 * Order-preserving 64-bit key encodings ("normalized keys").
 *
 * <p>A normalized key is a 64-bit value whose <em>unsigned</em> order agrees with the order of the
 * value it was made from. Buckets are cut on these keys, and comparing two {@code long}s is far
 * cheaper than comparing two strings, so every element is encoded once and the key is reused for
 * bucket assignment and most in-bucket comparisons. Compare keys with {@link #compare(long, long)}
 * ({@link Long#compareUnsigned}), never with {@code <}.
 *
 * <p>The numeric encodings are part of the cross-language specification
 * (docs/bucketization/README.md) and produce the same bits in every port.
 */
public final class Keys {

    /** Key every NaN maps to. It sorts after positive infinity. */
    public static final long CANONICAL_NAN = 0xFFF8000000000000L;

    /** Case-insensitive order matching {@link #fromStringIgnoreCase(CharSequence)}. */
    public static final Comparator<CharSequence> IGNORE_CASE_ORDER = Keys::compareIgnoreCase;

    private Keys() {
    }

    /** Unsigned order of two normalized keys. */
    public static int compare(long a, long b) {
        return Long.compareUnsigned(a, b);
    }

    /** A value that already is an unsigned 64-bit key. */
    public static long fromUnsigned(long value) {
        return value;
    }

    /** Signed integers: flipping the sign bit maps MIN_VALUE..MAX_VALUE onto 0..2^64-1. */
    public static long fromSigned(long value) {
        return value ^ Long.MIN_VALUE;
    }

    /**
     * IEEE-754 doubles, consistent with {@link Double#compare}: -inf &lt; negatives &lt; -0.0 &lt;
     * +0.0 &lt; positives &lt; +inf &lt; NaN.
     */
    public static long fromDouble(double value) {
        if (Double.isNaN(value)) {
            return CANONICAL_NAN;
        }
        long bits = Double.doubleToRawLongBits(value);
        return bits < 0 ? ~bits : bits | Long.MIN_VALUE;
    }

    /**
     * Packs the first 8 bytes big-endian, zero-padding shorter input. Preserves byte-lexicographic
     * order (unsigned bytes) as a prefix, e.g. of {@code CollationKey.toByteArray()}: if {@code
     * fromBytes(a) < fromBytes(b)} (unsigned) then {@code a < b}.
     */
    public static long fromBytes(byte[] bytes) {
        return fromBytes(bytes, 0, bytes.length);
    }

    /** {@link #fromBytes(byte[])} on {@code bytes[offset, offset + length)}. */
    public static long fromBytes(byte[] bytes, int offset, int length) {
        long key = 0;
        for (int i = 0; i < 8; i++) {
            key <<= 8;
            if (i < length) {
                key |= bytes[offset + i] & 0xFFL;
            }
        }
        return key;
    }

    /**
     * Prefix key consistent with {@link String#compareTo} and {@link CharSequence#compare} (UTF-16
     * code unit order). Characters are packed UTF-8 style, one to three bytes each, so the key
     * covers the first eight ASCII characters.
     */
    public static long fromString(CharSequence text) {
        return packChars(text, false);
    }

    /** Prefix key consistent with {@link #IGNORE_CASE_ORDER}. */
    public static long fromStringIgnoreCase(CharSequence text) {
        return packChars(text, true);
    }

    /**
     * Compares character by character after folding case the way {@link
     * String#CASE_INSENSITIVE_ORDER} folds a single {@code char}; shorter strings come first.
     */
    public static int compareIgnoreCase(CharSequence a, CharSequence b) {
        int length = Math.min(a.length(), b.length());
        for (int i = 0; i < length; i++) {
            char x = fold(a.charAt(i));
            char y = fold(b.charAt(i));
            if (x != y) {
                return x - y;
            }
        }
        return a.length() - b.length();
    }

    /** Reverses key order. Only needed for hand-built composite keys. */
    public static long reverse(long key) {
        return ~key;
    }

    private static char fold(char c) {
        return Character.toLowerCase(Character.toUpperCase(c));
    }

    private static long packChars(CharSequence text, boolean ignoreCase) {
        long key = 0;
        int used = 0;
        for (int i = 0; i < text.length() && used < 8; i++) {
            char c = ignoreCase ? fold(text.charAt(i)) : text.charAt(i);
            // UTF-8 byte order equals code unit order; surrogates are encoded
            // as plain 3-byte values so every char keeps its place.
            int encoded;
            int length;
            if (c < 0x80) {
                encoded = c;
                length = 1;
            } else if (c < 0x800) {
                encoded = (0xC0 | (c >> 6)) << 8 | (0x80 | (c & 0x3F));
                length = 2;
            } else {
                encoded = (0xE0 | (c >> 12)) << 16 | (0x80 | ((c >> 6) & 0x3F)) << 8 | (0x80 | (c & 0x3F));
                length = 3;
            }
            for (int b = length - 1; b >= 0 && used < 8; b--) {
                key = (key << 8) | ((encoded >>> (8 * b)) & 0xFF);
                used++;
            }
        }
        return used == 0 ? 0 : key << (8 * (8 - used));
    }
}
