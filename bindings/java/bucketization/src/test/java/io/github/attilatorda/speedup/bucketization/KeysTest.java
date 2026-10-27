package io.github.attilatorda.speedup.bucketization;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.text.Collator;
import java.util.Locale;
import java.util.SplittableRandom;
import org.junit.jupiter.api.Test;

class KeysTest {

    private static final String ALPHABET = "aAbBzZ09_- .éÉßöÖ\u0000ÿĀ中😀￿";

    static String randomString(SplittableRandom rng) {
        String[] prefixes = {"", "IMG_", "IMG_2024", "doc", "Doc", "été"};
        StringBuilder sb = new StringBuilder(prefixes[rng.nextInt(prefixes.length)]);
        for (int i = rng.nextInt(7); i > 0; i--) {
            sb.append(ALPHABET.charAt(rng.nextInt(ALPHABET.length())));
        }
        return sb.toString();
    }

    @Test
    void signedKeysPreserveOrder() {
        SplittableRandom rng = new SplittableRandom(1);
        for (int i = 0; i < 10_000; i++) {
            long a = rng.nextLong() >> (i % 60);
            long b = rng.nextLong() >> (i % 61);
            assertEquals(Long.signum(Long.compare(a, b)),
                    Long.signum(Keys.compare(Keys.fromSigned(a), Keys.fromSigned(b))));
        }
        assertEquals(0x7FFFFFFFFFFFFFFFL, Keys.fromSigned(-1));
    }

    @Test
    void doubleKeysFollowDoubleCompare() {
        double[] values = {Double.NEGATIVE_INFINITY, -1e300, -1.5, -Double.MIN_VALUE, -0.0, 0.0, Double.MIN_VALUE,
                1.0, 2.0, 1e300, Double.POSITIVE_INFINITY, Double.NaN};
        for (int i = 0; i + 1 < values.length; i++) {
            assertTrue(Keys.compare(Keys.fromDouble(values[i]), Keys.fromDouble(values[i + 1])) < 0, "index " + i);
        }
        assertEquals(Keys.CANONICAL_NAN, Keys.fromDouble(Double.longBitsToDouble(0xFFF8000000000123L)));
        // Same bits as the C++ port.
        assertEquals(0xBFF0000000000000L, Keys.fromDouble(1.0));
        assertEquals(0x3FFBFFFFFFFFFFFFL, Keys.fromDouble(-2.5));
    }

    @Test
    void stringKeysNeverContradictCompareTo() {
        SplittableRandom rng = new SplittableRandom(2);
        for (int i = 0; i < 50_000; i++) {
            String a = randomString(rng);
            String b = randomString(rng);
            int keyOrder = Keys.compare(Keys.fromString(a), Keys.fromString(b));
            if (keyOrder < 0) {
                assertTrue(a.compareTo(b) < 0, a + " vs " + b);
            }
            if (a.compareTo(b) < 0) {
                assertTrue(keyOrder <= 0, a + " vs " + b);
            }
        }
        assertEquals(0L, Keys.fromString(""));
        assertEquals(0x4100000000000000L, Keys.fromString("A"));
        assertEquals(Keys.fromString("ABCDEFGH"), Keys.fromString("ABCDEFGHIJ"));
        // UTF-8 packing: equal to the C++ key of the UTF-8 bytes for BMP text.
        assertEquals(0xC3A974C3A9000000L, Keys.fromString("été"));
    }

    @Test
    void ignoreCaseKeysNeverContradictIgnoreCaseOrder() {
        SplittableRandom rng = new SplittableRandom(3);
        for (int i = 0; i < 50_000; i++) {
            String a = randomString(rng);
            String b = randomString(rng);
            int keyOrder = Keys.compare(Keys.fromStringIgnoreCase(a), Keys.fromStringIgnoreCase(b));
            int order = Keys.compareIgnoreCase(a, b);
            if (keyOrder < 0) {
                assertTrue(order < 0, a + " vs " + b);
            }
            if (order < 0) {
                assertTrue(keyOrder <= 0, a + " vs " + b);
            }
        }
        assertEquals(Keys.fromStringIgnoreCase("Readme"), Keys.fromStringIgnoreCase("README"));
        assertEquals(0, Keys.compareIgnoreCase("Readme", "rEADME"));
    }

    @Test
    void collationKeysNeverContradictTheCollator() {
        SplittableRandom rng = new SplittableRandom(4);
        for (Locale locale : new Locale[] {Locale.ROOT, Locale.ENGLISH, Locale.GERMAN, Locale.FRENCH}) {
            Collator collator = Collator.getInstance(locale);
            for (int i = 0; i < 5_000; i++) {
                String a = randomString(rng);
                String b = randomString(rng);
                long ka = Keys.fromBytes(collator.getCollationKey(a).toByteArray());
                long kb = Keys.fromBytes(collator.getCollationKey(b).toByteArray());
                if (Keys.compare(ka, kb) < 0) {
                    assertTrue(collator.getCollationKey(a).compareTo(collator.getCollationKey(b)) < 0,
                            locale + ": " + a + " vs " + b);
                }
            }
        }
    }

    @Test
    void bytesArePackedBigEndian() {
        assertEquals(0x0102030000000000L, Keys.fromBytes(new byte[] {1, 2, 3}));
        assertEquals(0xFF00000000000000L, Keys.fromBytes(new byte[] {(byte) 0xFF}));
        assertEquals(0x0203000000000000L, Keys.fromBytes(new byte[] {1, 2, 3}, 1, 2));
        assertTrue(Keys.compare(Keys.reverse(1), Keys.reverse(2)) > 0);
    }
}
