/*
 * SPDX-FileCopyrightText: 2011 Peter Penz <peter.penz19@gmail.com>
 * SPDX-FileCopyrightText: 2012 Emmanuel Pescosta <emmanuelpescosta099@gmail.com>
 * SPDX-FileCopyrightText: 2013 Frank Reininghaus <frank78ac@googlemail.com>
 * SPDX-FileCopyrightText: 2011 Nokia Corporation and/or its subsidiary(-ies).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// Dolphin's sorting code, extracted for benchmarking so Bucketization is
// compared against what Dolphin actually runs.
//
// Source: KDE Dolphin (https://invent.kde.org/system/dolphin), commit
// c5849f9be754e3307dc778cc6d48866604f6c855 (2026-10-24):
//   src/kitemviews/kfileitemmodel.cpp            (comparison, resortAllItems, insertItems)
//   src/kitemviews/private/kfileitemmodelsortalgorithm.h  (mergeSort, parallelMergeSort, merge)
//
// Adaptations, all behaviour-preserving for flat folder views:
// - Qt::strong_ordering results became ints (this benchmark builds with Qt 6.4).
// - KFileItem / ItemData became BenchItem; Dolphin reads the same values
//   (text, name, url, isDir, isHidden, size, modification time in seconds).
// - Only flat views: no expanded folders (tree mode), no grouping.
// - Size role uses Dolphin's default "content count" folder mode with no
//   counts known yet, so folders fall back to name order.
// - resortAllItems() counts the moved items instead of emitting itemsMoved.

#pragma once

#include <QCollator>
#include <QHash>
#include <QList>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>
#include <cstdint>

namespace dolphin {

// ---- private/kfileitemmodelsortalgorithm.h ---------------------------------

template<typename RandomAccessIterator, typename LessThan>
static void merge(RandomAccessIterator begin, RandomAccessIterator pivot, RandomAccessIterator end, const LessThan &lessThan);

template<typename RandomAccessIterator, typename LessThan>
static void mergeSort(RandomAccessIterator begin, RandomAccessIterator end, const LessThan &lessThan)
{
    const int span = end - begin;
    if (span < 2) {
        return;
    }

    const RandomAccessIterator middle = begin + span / 2;
    mergeSort(begin, middle, lessThan);
    mergeSort(middle, end, lessThan);
    merge(begin, middle, end, lessThan);
}

template<typename RandomAccessIterator, typename LessThan>
static void
parallelMergeSort(RandomAccessIterator begin, RandomAccessIterator end, LessThan lessThan, int numberOfThreads, int parallelMergeSortingThreshold = 100)
{
    const int span = end - begin;

    if (numberOfThreads > 1 && span > parallelMergeSortingThreshold) {
        const int newNumberOfThreads = numberOfThreads / 2;
        const RandomAccessIterator middle = begin + span / 2;

        QFuture<void> future =
            QtConcurrent::run(parallelMergeSort<RandomAccessIterator, LessThan>, begin, middle, lessThan, newNumberOfThreads, parallelMergeSortingThreshold);
        parallelMergeSort(middle, end, lessThan, newNumberOfThreads, parallelMergeSortingThreshold);

        future.waitForFinished();

        merge(begin, middle, end, lessThan);
    } else {
        mergeSort(begin, end, lessThan);
    }
}

template<typename RandomAccessIterator, typename LessThan>
static void merge(RandomAccessIterator begin, RandomAccessIterator pivot, RandomAccessIterator end, const LessThan &lessThan)
{
    const int len1 = pivot - begin;
    const int len2 = end - pivot;

    if (len1 == 0 || len2 == 0) {
        return;
    }

    if (len1 + len2 == 2) {
        if (lessThan(*(begin + 1), *(begin))) {
            std::swap(*begin, *(begin + 1));
        }
        return;
    }

    RandomAccessIterator firstCut;
    RandomAccessIterator secondCut;
    int len2Half;
    if (len1 > len2) {
        const int len1Half = len1 / 2;
        firstCut = begin + len1Half;
        secondCut = std::lower_bound<RandomAccessIterator, decltype(*firstCut), const LessThan &>(pivot, end, *firstCut, lessThan);
        len2Half = secondCut - pivot;
    } else {
        len2Half = len2 / 2;
        secondCut = pivot + len2Half;
        firstCut = std::upper_bound<RandomAccessIterator, decltype(*secondCut), const LessThan &>(begin, pivot, *secondCut, lessThan);
    }

    std::rotate(firstCut, pivot, secondCut);

    RandomAccessIterator newPivot = firstCut + len2Half;
    merge(begin, firstCut, newPivot, lessThan);
    merge(newPivot, secondCut, end, lessThan);
}

// ---- kfileitemmodel.cpp: natural comparison helpers -------------------------

inline const QCollator &collatorForThisThread(const QCollator &prototype)
{
    thread_local QCollator collator;
    thread_local bool matchesPrototype = false;

    if (!matchesPrototype || collator.locale() != prototype.locale() || collator.caseSensitivity() != prototype.caseSensitivity()
        || collator.numericMode() != prototype.numericMode()) {
        collator = QCollator(prototype.locale());
        collator.setNumericMode(prototype.numericMode());
        collator.setCaseSensitivity(prototype.caseSensitivity());
        matchesPrototype = true;
    }

    return collator;
}

inline bool isAsciiDigit(QChar c)
{
    return c >= QLatin1Char('0') && c <= QLatin1Char('9');
}

inline int sign(int result)
{
    return (result > 0) - (result < 0);
}

inline int compareDigitStrings(QStringView a, QStringView b)
{
    int firstSignificantA = 0;
    while (firstSignificantA < a.length() && a.at(firstSignificantA) == QLatin1Char('0')) {
        ++firstSignificantA;
    }

    int firstSignificantB = 0;
    while (firstSignificantB < b.length() && b.at(firstSignificantB) == QLatin1Char('0')) {
        ++firstSignificantB;
    }

    const int significantLengthA = a.length() - firstSignificantA;
    const int significantLengthB = b.length() - firstSignificantB;
    if (significantLengthA != significantLengthB) {
        return significantLengthA < significantLengthB ? -1 : 1;
    }

    for (int i = 0; i < significantLengthA; ++i) {
        const QChar digitA = a.at(firstSignificantA + i);
        const QChar digitB = b.at(firstSignificantB + i);
        if (digitA != digitB) {
            return digitA < digitB ? -1 : 1;
        }
    }

    return 0;
}

inline int compareFractionalDigitStrings(QStringView a, QStringView b)
{
    const int length = std::max(a.length(), b.length());
    for (int i = 0; i < length; ++i) {
        const QChar digitA = i < a.length() ? a.at(i) : QLatin1Char('0');
        const QChar digitB = i < b.length() ? b.at(i) : QLatin1Char('0');
        if (digitA != digitB) {
            return digitA < digitB ? -1 : 1;
        }
    }

    return 0;
}

inline int findDigitRunEnd(QStringView text, int start)
{
    int end = start;
    while (end < text.length() && isAsciiDigit(text.at(end))) {
        ++end;
    }

    return end;
}

inline int countNumericChainSegments(QStringView text, int start, int *chainEnd)
{
    int end = findDigitRunEnd(text, start);
    int segmentCount = 1;

    while (end + 1 < text.length() && text.at(end) == QLatin1Char('.') && isAsciiDigit(text.at(end + 1))) {
        end = findDigitRunEnd(text, end + 1);
        ++segmentCount;
    }

    if (chainEnd) {
        *chainEnd = end;
    }

    return segmentCount;
}

inline int compareNumericChains(QStringView a, int startA, int endA, int segmentCountA, QStringView b, int startB, int endB, int segmentCountB)
{
    if (segmentCountA == 2 && segmentCountB == 2) {
        const int dotA = findDigitRunEnd(a, startA);
        const int dotB = findDigitRunEnd(b, startB);

        const int integerResult = compareDigitStrings(a.mid(startA, dotA - startA), b.mid(startB, dotB - startB));
        if (integerResult != 0) {
            return integerResult;
        }

        return compareFractionalDigitStrings(a.mid(dotA + 1, endA - dotA - 1), b.mid(dotB + 1, endB - dotB - 1));
    }

    int segmentStartA = startA;
    int segmentStartB = startB;

    while (true) {
        const int segmentEndA = findDigitRunEnd(a, segmentStartA);
        const int segmentEndB = findDigitRunEnd(b, segmentStartB);

        const int segmentResult = compareDigitStrings(a.mid(segmentStartA, segmentEndA - segmentStartA), b.mid(segmentStartB, segmentEndB - segmentStartB));
        if (segmentResult != 0) {
            return segmentResult;
        }

        const bool hasNextSegmentA = segmentEndA < endA;
        const bool hasNextSegmentB = segmentEndB < endB;
        if (!hasNextSegmentA || !hasNextSegmentB) {
            if (hasNextSegmentA != hasNextSegmentB) {
                return hasNextSegmentA ? 1 : -1;
            }

            return 0;
        }

        segmentStartA = segmentEndA + 1;
        segmentStartB = segmentEndB + 1;
    }
}

inline int findExtensionSeparator(QStringView text)
{
    for (int i = text.length() - 1; i > 0; --i) {
        if (text.at(i) != QLatin1Char('.')) {
            continue;
        }

        if (isAsciiDigit(text.at(i - 1)) && i + 1 < text.length() && isAsciiDigit(text.at(i + 1))) {
            continue;
        }

        return i;
    }

    return -1;
}

inline int decimalAwareNaturalCompare(QStringView a, QStringView b, const QCollator &collator)
{
    bool comparedNumericTokens = false;
    int indexA = 0;
    int indexB = 0;

    while (indexA < a.length() && indexB < b.length()) {
        if (isAsciiDigit(a.at(indexA)) && isAsciiDigit(b.at(indexB))) {
            comparedNumericTokens = true;
            int chainEndA = indexA;
            const int segmentCountA = countNumericChainSegments(a, indexA, &chainEndA);
            int chainEndB = indexB;
            const int segmentCountB = countNumericChainSegments(b, indexB, &chainEndB);

            const int numericResult = compareNumericChains(a, indexA, chainEndA, segmentCountA, b, indexB, chainEndB, segmentCountB);
            indexA = chainEndA;
            indexB = chainEndB;
            if (numericResult != 0) {
                return numericResult;
            }

            continue;
        }

        int textEndA = indexA;
        while (textEndA < a.length() && !isAsciiDigit(a.at(textEndA))) {
            ++textEndA;
        }

        int textEndB = indexB;
        while (textEndB < b.length() && !isAsciiDigit(b.at(textEndB))) {
            ++textEndB;
        }

        const int textResult = sign(collator.compare(a.mid(indexA, textEndA - indexA), b.mid(indexB, textEndB - indexB)));
        if (textResult != 0) {
            return sign(collator.compare(a.mid(indexA), b.mid(indexB)));
        }

        indexA = textEndA;
        indexB = textEndB;
    }

    const int remainderResult = sign(collator.compare(a.mid(indexA), b.mid(indexB)));
    if (remainderResult != 0) {
        return remainderResult;
    }

    if (!comparedNumericTokens) {
        return 0;
    }

    const int result = sign(a.compare(b, collator.caseSensitivity()));
    if (result != 0 || collator.caseSensitivity() == Qt::CaseSensitive) {
        return result;
    }

    return sign(a.compare(b, Qt::CaseSensitive));
}

// ---- KFileItemModel, reduced to what flat views sort on ---------------------

/// Stands in for KFileItemModel::ItemData + KFileItem.
struct BenchItem {
    QString text;  // KFileItem::text()
    QString name;  // KFileItem::name()
    QUrl url;
    bool isDir = false;
    bool isHidden = false;
    std::uint64_t size = 0;
    std::int64_t modificationTime = -1;  // seconds, like UDS_MODIFICATION_TIME
};

class Model
{
public:
    enum RoleType { NameRole, SizeRole, ModificationTimeRole };

    explicit Model(const QLocale &locale)
        : m_collator(locale)
    {
        // Dolphin's defaults: natural, case-insensitive sorting, folders first.
        m_collator.setNumericMode(true);
        m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    }

    RoleType m_sortRole = NameRole;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    bool m_naturalSorting = true;
    bool m_sortDirsFirst = true;
    bool m_sortHiddenLast = false;
    QCollator m_collator;
    QList<BenchItem *> m_itemData;
    QHash<QUrl, int> m_items;

    static bool nameLessThan(const BenchItem *a, const BenchItem *b)
    {
        constexpr QString::SectionFlags flags = QString::SectionSkipEmpty | QString::SectionIncludeLeadingSep;
        return a->text.section('.', 0, 0, flags) < b->text.section('.', 0, 0, flags);
    }

    bool lessThan(const BenchItem *a, const BenchItem *b, const QCollator &collator) const
    {
        if (m_sortHiddenLast) {
            if (a->isHidden && !b->isHidden) {
                return false;
            } else if (!a->isHidden && b->isHidden) {
                return true;
            }
        }

        // Folders first: m_sortDirsFirst, or content-count mode with the size role.
        if (m_sortDirsFirst || m_sortRole == SizeRole) {
            if (a->isDir && !b->isDir) {
                return true;
            } else if (!a->isDir && b->isDir) {
                return false;
            }
        }

        const int result = sortRoleCompare(a, b, collator);
        return (m_sortOrder == Qt::AscendingOrder) ? result < 0 : result > 0;
    }

    int sortRoleCompare(const BenchItem *a, const BenchItem *b, const QCollator &collator) const
    {
        int result = 0;

        switch (m_sortRole) {
        case NameRole:
            break;

        case SizeRole:
            if (a->isDir) {
                // Content-count mode, counts not known yet: both values are null.
                break;
            }
            if (a->size < b->size) {
                return -1;
            } else if (a->size > b->size) {
                return +1;
            }
            break;

        case ModificationTimeRole:
            if (a->modificationTime < b->modificationTime) {
                return -1;
            } else if (a->modificationTime > b->modificationTime) {
                return +1;
            }
            break;
        }

        // Fallback #1: Compare the text of the items
        result = stringCompare(a->text, b->text, collator);
        if (result != 0) {
            return result;
        }

        // Fallback #2: KFileItem::text() may not be unique in case UDS_DISPLAY_NAME is used
        result = stringCompare(a->name, b->name, collator);
        if (result != 0) {
            return result;
        }

        // Fallback #3: the sort order must always be unique
        return QString::compare(a->url.url(), b->url.url(), Qt::CaseSensitive);
    }

    int stringCompare(const QString &a, const QString &b, const QCollator &prototype) const
    {
        const QCollator &collator = collatorForThisThread(prototype);

        if (m_naturalSorting) {
            const QStringView viewA(a);
            const QStringView viewB(b);
            const int aExtensionSeparator = findExtensionSeparator(viewA);
            const int bExtensionSeparator = findExtensionSeparator(viewB);
            const int aBaseNameLength = aExtensionSeparator < 0 ? viewA.length() : aExtensionSeparator;
            const int bBaseNameLength = bExtensionSeparator < 0 ? viewB.length() : bExtensionSeparator;

            const int res = decimalAwareNaturalCompare(viewA.left(aBaseNameLength), viewB.left(bBaseNameLength), collator);
            if (res != 0 || (aExtensionSeparator < 0 && bExtensionSeparator < 0)) {
                return res;
            }

            return decimalAwareNaturalCompare(viewA.mid(aBaseNameLength), viewB.mid(bBaseNameLength), collator);
        }

        const int result = QString::compare(a, b, collator.caseSensitivity());
        if (result != 0 || collator.caseSensitivity() == Qt::CaseSensitive) {
            return result;
        }

        return QString::compare(a, b, Qt::CaseSensitive);
    }

    void sort(QList<BenchItem *>::iterator begin, QList<BenchItem *>::iterator end) const
    {
        auto lambdaLessThan = [&](const BenchItem *a, const BenchItem *b) {
            return lessThan(a, b, m_collator);
        };

        // String-based sorts use several threads, other sorts one (bug 312679).
        if (m_sortRole == NameRole) {
            static const int numberOfThreads = QThread::idealThreadCount();
            parallelMergeSort(begin, end, lambdaLessThan, numberOfThreads);
        } else {
            mergeSort(begin, end, lambdaLessThan);
        }
    }

    /// What Dolphin does when the sort role or order changes. Returns the
    /// number of items that moved (Dolphin emits them in itemsMoved).
    int resortAllItems()
    {
        const int itemCount = m_itemData.count();
        if (itemCount <= 0) {
            return 0;
        }

        QList<QUrl> oldUrls;
        oldUrls.reserve(itemCount);
        for (const BenchItem *itemData : std::as_const(m_itemData)) {
            oldUrls.append(itemData->url);
        }

        m_items.clear();
        m_items.reserve(itemCount);

        sort(m_itemData.begin(), m_itemData.end());
        for (int i = 0; i < itemCount; ++i) {
            m_items.insert(m_itemData.at(i)->url, i);
        }

        int firstMovedIndex = 0;
        while (firstMovedIndex < itemCount && firstMovedIndex == m_items.value(oldUrls.at(firstMovedIndex))) {
            ++firstMovedIndex;
        }
        if (firstMovedIndex == itemCount) {
            return 0;
        }

        int lastMovedIndex = itemCount - 1;
        while (lastMovedIndex > firstMovedIndex && lastMovedIndex == m_items.value(oldUrls.at(lastMovedIndex))) {
            --lastMovedIndex;
        }

        const int movedItemsCount = lastMovedIndex - firstMovedIndex + 1;
        QList<int> movedToIndexes;
        movedToIndexes.reserve(movedItemsCount);
        for (int i = firstMovedIndex; i <= lastMovedIndex; ++i) {
            movedToIndexes.append(m_items.value(oldUrls.at(i)));
        }
        return movedItemsCount;
    }

    /// The sorting part of insertItems() when a folder is opened (empty model).
    void insertItemsIntoEmptyModel(QList<BenchItem *> newItems)
    {
        // Natural sorting is faster on nearly sorted input, so pre-sort with QString::operator<().
        if (m_naturalSorting && m_sortRole == NameRole) {
            parallelMergeSort(newItems.begin(), newItems.end(), nameLessThan, QThread::idealThreadCount());
        }
        sort(newItems.begin(), newItems.end());
        m_itemData = newItems;
        m_items.clear();
    }
};

}  // namespace dolphin
