#pragma once
// Order-preserving 64-bit prefix key for Dolphin's natural name order.
//
// Dolphin compares names with decimalAwareNaturalCompare(): digit runs by
// value, everything else with QCollator, which uses ICU. The key below is the
// start of an ICU *primary-strength*, numeric-collation sort key of the base
// name up to the end of its first number: the leading non-digit run, then the
// first ASCII digit run (ICU encodes it by value, so "9" < "10" and leading
// zeros do not count, as in Dolphin). Nothing after the first number goes in.
//
// Why it never contradicts Dolphin: Dolphin first compares the leading
// non-digit runs; if they differ it compares the whole base names with the
// collator, and ICU compares primary weights first, numbers by value. If the
// runs are equal Dolphin compares the first numbers by value next. Either way,
// whenever two keys differ, the first differing weight inside them decides
// Dolphin's result too. Equal keys go to the tie-breaker (Dolphin's own
// comparison). The benchmark checks the order against Dolphin's on every
// dataset.

#include <unicode/ucol.h>
#include <unicode/uiter.h>

#include <QLocale>
#include <QString>
#include <QVarLengthArray>

#include <cstdint>
#include <stdexcept>

#include "dolphin_sort.h"

class IcuNaturalPrefixKey {
public:
    explicit IcuNaturalPrefixKey(const QLocale& locale) {
        UErrorCode status = U_ZERO_ERROR;
        collator_ = ucol_open(locale.name().toUtf8().constData(), &status);
        // Same settings QCollator uses (natural, case-insensitive), at primary strength.
        ucol_setAttribute(collator_, UCOL_NORMALIZATION_MODE, UCOL_ON, &status);
        ucol_setAttribute(collator_, UCOL_STRENGTH, UCOL_PRIMARY, &status);
        ucol_setAttribute(collator_, UCOL_NUMERIC_COLLATION, UCOL_ON, &status);
        ucol_setAttribute(collator_, UCOL_ALTERNATE_HANDLING, UCOL_NON_IGNORABLE, &status);
        if (U_FAILURE(status)) {
            throw std::runtime_error(std::string("ICU collator: ") + u_errorName(status));
        }
    }
    ~IcuNaturalPrefixKey() { ucol_close(collator_); }
    IcuNaturalPrefixKey(const IcuNaturalPrefixKey&) = delete;
    IcuNaturalPrefixKey& operator=(const IcuNaturalPrefixKey&) = delete;

    std::uint64_t operator()(const QString& text) const {
        const QStringView view(text);
        const int separator = dolphin::findExtensionSeparator(view);
        const QStringView base = separator < 0 ? view : view.left(separator);
        int end = 0;
        while (end < base.size() && !dolphin::isAsciiDigit(base.at(end))) {
            ++end;
        }
        while (end < base.size() && dolphin::isAsciiDigit(base.at(end))) {
            ++end;
        }
        QVarLengthArray<UChar, 64> buffer(end);
        for (int i = 0; i < end; ++i) {
            buffer[i] = base.at(i).unicode();
        }

        UCharIterator it;
        uiter_setString(&it, buffer.constData(), static_cast<int32_t>(buffer.size()));
        uint32_t state[2] = {0, 0};
        uint8_t bytes[8] = {};
        UErrorCode status = U_ZERO_ERROR;
        const int32_t got = ucol_nextSortKeyPart(collator_, &it, state, bytes, 8, &status);
        std::uint64_t key = 0;
        bool primaryEnded = false;
        for (int i = 0; i < 8; ++i) {
            key <<= 8;
            // 0x01 separates the primary level from the next, 0x00 ends the key.
            if (!primaryEnded && i < got && bytes[i] > 1) {
                key |= bytes[i];
            } else {
                primaryEnded = true;
            }
        }
        return key;
    }

private:
    UCollator* collator_ = nullptr;
};
