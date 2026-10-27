#pragma once
// Order-preserving 64-bit key for names that a project compares with QCollator
// (or QString::localeAwareCompare), both of which use ICU in Qt 6.
//
// The key is the start of the ICU *primary-strength* sort key of the text, in
// the same locale and numeric mode as the project's collator. ICU compares
// primary weights before anything else, so when two keys differ the first
// differing primary weight decides the collator's result too, at any strength.
// Equal keys go to the tie-breaker (the project's own comparison). The
// benchmarks check every resulting order against the project's comparison.

#include <unicode/ucol.h>
#include <unicode/uiter.h>

#include <QLocale>
#include <QString>

#include <cstdint>
#include <stdexcept>
#include <string>

class IcuPrimaryKey {
public:
    IcuPrimaryKey(const QLocale& locale, bool numeric) {
        UErrorCode status = U_ZERO_ERROR;
        collator_ = ucol_open(locale.name().toUtf8().constData(), &status);
        ucol_setAttribute(collator_, UCOL_NORMALIZATION_MODE, UCOL_ON, &status);
        ucol_setAttribute(collator_, UCOL_STRENGTH, UCOL_PRIMARY, &status);
        ucol_setAttribute(collator_, UCOL_NUMERIC_COLLATION, numeric ? UCOL_ON : UCOL_OFF, &status);
        ucol_setAttribute(collator_, UCOL_ALTERNATE_HANDLING, UCOL_NON_IGNORABLE, &status);
        if (U_FAILURE(status)) {
            throw std::runtime_error(std::string("ICU collator: ") + u_errorName(status));
        }
    }
    ~IcuPrimaryKey() { ucol_close(collator_); }
    IcuPrimaryKey(const IcuPrimaryKey&) = delete;
    IcuPrimaryKey& operator=(const IcuPrimaryKey&) = delete;

    std::uint64_t operator()(QStringView text) const {
        UCharIterator it;
        uiter_setString(&it, reinterpret_cast<const UChar*>(text.utf16()), static_cast<int32_t>(text.size()));
        uint32_t state[2] = {0, 0};
        uint8_t bytes[8] = {};
        UErrorCode status = U_ZERO_ERROR;
        const int32_t got = ucol_nextSortKeyPart(collator_, &it, state, bytes, 8, &status);
        std::uint64_t key = 0;
        bool primary_ended = false;
        for (int i = 0; i < 8; ++i) {
            key <<= 8;
            // 0x01 separates the primary level from the next, 0x00 ends the key.
            if (!primary_ended && i < got && bytes[i] > 1) {
                key |= bytes[i];
            } else {
                primary_ended = true;
            }
        }
        return key;
    }

private:
    UCollator* collator_ = nullptr;
};
