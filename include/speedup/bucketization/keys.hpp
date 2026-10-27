#pragma once
/// @file keys.hpp
/// Order-preserving 64-bit key encodings ("normalized keys").
///
/// A normalized key is an unsigned 64-bit integer whose numeric order agrees
/// with the order of the value it was made from. Keys are what the buckets are
/// cut on: comparing two `uint64_t` values is much cheaper than comparing two
/// strings, so every element is encoded once and the encoding is reused for
/// every bucket assignment and most in-bucket comparisons.
///
/// The encodings are part of the cross-language specification
/// (docs/bucketization/README.md): every port produces the same bits for the
/// same numeric input.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace speedup::bucketization::keys {

inline constexpr std::uint64_t sign_bit = 0x8000000000000000ULL;

/// Key every NaN maps to. It sorts after +infinity.
inline constexpr std::uint64_t canonical_nan = 0xFFF8000000000000ULL;

/// Unsigned integers are already in key order.
constexpr std::uint64_t from_unsigned(std::uint64_t value) noexcept { return value; }

/// Signed integers: flipping the sign bit maps INT64_MIN..INT64_MAX onto 0..UINT64_MAX.
constexpr std::uint64_t from_signed(std::int64_t value) noexcept {
    return static_cast<std::uint64_t>(value) ^ sign_bit;
}

/// IEEE-754 doubles: -inf < negative < -0.0 < +0.0 < positive < +inf < NaN.
inline std::uint64_t from_double(double value) noexcept {
    if (std::isnan(value)) {
        return canonical_nan;
    }
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return (bits & sign_bit) ? ~bits : (bits | sign_bit);
}

/// Packs the first 8 bytes big-endian and zero-pads shorter input.
///
/// The result preserves byte-lexicographic order (memcmp, std::string,
/// strxfrm/std::collate::transform output, ICU sort keys) as a prefix: if
/// `from_bytes(a) < from_bytes(b)` then `a < b`. Inputs sharing their first
/// 8 bytes get the same key and are ordered by the sort key's tie-breaker.
inline std::uint64_t from_bytes(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t key = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        key <<= 8;
        if (i < size) {
            key |= bytes[i];
        }
    }
    return key;
}

/// Prefix key of a byte string. Consistent with `std::string_view::compare`,
/// which compares characters as unsigned char (UTF-8 code point order).
inline std::uint64_t from_string(std::string_view text) noexcept {
    return from_bytes(text.data(), text.size());
}

/// Reverses key order. Only needed for hand-built composite keys; the index
/// handles descending order itself (see Direction).
constexpr std::uint64_t reverse(std::uint64_t key) noexcept { return ~key; }

namespace detail {
template <class V>
inline constexpr bool always_false = false;
}  // namespace detail

/// True for types `normalize` encodes as a string prefix.
template <class V>
inline constexpr bool is_string_like_v = std::is_convertible_v<const V&, std::string_view>;

/// True when `normalize(V)` is the whole value, i.e. no tie-breaker is needed.
template <class V>
inline constexpr bool is_exact_v = std::is_arithmetic_v<V> || std::is_enum_v<V>;

/// Picks the encoding that matches the natural `<` of the value's type.
template <class V>
std::uint64_t normalize(const V& value) noexcept {
    if constexpr (std::is_same_v<V, bool>) {
        return value ? 1U : 0U;
    } else if constexpr (std::is_enum_v<V>) {
        return normalize(static_cast<std::underlying_type_t<V>>(value));
    } else if constexpr (std::is_integral_v<V> && std::is_signed_v<V>) {
        return from_signed(static_cast<std::int64_t>(value));
    } else if constexpr (std::is_integral_v<V>) {
        return from_unsigned(static_cast<std::uint64_t>(value));
    } else if constexpr (std::is_floating_point_v<V>) {
        return from_double(static_cast<double>(value));
    } else if constexpr (is_string_like_v<V>) {
        return from_string(std::string_view(value));
    } else {
        static_assert(detail::always_false<V>, "no normalized key encoding for this type; use SortKey::of_key");
    }
}

}  // namespace speedup::bucketization::keys
