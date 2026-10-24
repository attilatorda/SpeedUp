// Shows that Krusader's descending comparison is not a strict weak ordering,
// which std::stable_sort requires.
//
// KrSort::itemGreaterThan (a, b) is !itemLessThan (a, b) (krsort.cpp). For
// items that compare equal it returns true both ways, and it returns true for
// an item compared with itself. Two names that differ only in case compare
// equal under Krusader's default "ignore case" setting.
//
// Build with -D_GLIBCXX_DEBUG (the CMake target does) to have libstdc++'s
// debug mode catch it inside std::stable_sort: the program then aborts with
// "comparison doesn't meet irreflexive requirements".
//
// Usage: speedup_krusader_descending_check

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>
#include <vector>

#include "qt_family_sort.h"

using namespace qt_family;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale(QStringLiteral("en_US")));

    std::vector<File> files(3);
    files[0].name = QStringLiteral("README");
    files[1].name = QStringLiteral("readme");
    files[2].name = QStringLiteral("notes.txt");
    std::vector<krusader::FileItem> items;
    for (const File& f : files) {
        items.push_back(krusader::FileItem{&f});
    }

    krusader::KrViewProperties descending;  // Krusader's defaults, sorted by name, descending
    descending.sortOptions |= krusader::KrViewProperties::Descending;
    krusader::SortProps a(&items[0], descending.sortColumn, &descending, false, false, 0);
    krusader::SortProps b(&items[1], descending.sortColumn, &descending, false, false, 1);

    const bool aa = krusader::itemGreaterThan(&a, &a);
    const bool ab = krusader::itemGreaterThan(&a, &b);
    const bool ba = krusader::itemGreaterThan(&b, &a);
    std::printf("itemGreaterThan(README, README) = %s   (must be false: irreflexivity)\n", aa ? "true" : "false");
    std::printf("itemGreaterThan(README, readme) = %s\n", ab ? "true" : "false");
    std::printf("itemGreaterThan(readme, README) = %s   (both true breaks asymmetry)\n", ba ? "true" : "false");
    const bool broken = aa || (ab && ba);
    std::printf("%s\n", broken ? "Not a strict weak ordering: std::stable_sort's result is undefined."
                               : "The comparison is a strict weak ordering for these items.");

#ifdef _GLIBCXX_DEBUG
    std::printf("Sorting with KrSort::Sorter under libstdc++ debug mode...\n");
    std::fflush(stdout);
    krusader::Sorter sorter(static_cast<qsizetype>(items.size()), &descending);
    for (std::size_t i = 0; i < items.size(); ++i) {
        sorter.addItem(&items[i], false, static_cast<int>(i));
    }
    sorter.sort();  // aborts here when the debug checks fire
    std::printf("std::stable_sort did not detect the problem.\n");
#endif
    return broken ? 1 : 0;
}
