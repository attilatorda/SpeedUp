// Shows that Krusader's descending comparison is not a strict weak ordering,
// which std::stable_sort requires.
//
// KrSort::itemGreaterThan (a, b) is !itemLessThan (a, b) (krsort.cpp). For
// items that compare equal it returns true both ways, and it returns true for
// an item compared with itself. Two names that differ only in case compare
// equal under Krusader's default "ignore case" setting.
//
// It also checks a proposed fix, !itemLessThan (a, b) && itemLessThan (b, a),
// for all strict-weak-ordering properties on every triple of a test set, with
// and without "always sort dirs by name" (where itemLessThan (b, a) alone fails).
//
// Build with -D_GLIBCXX_DEBUG (the CMake target does) to have libstdc++'s
// debug mode catch it inside std::stable_sort: the program then aborts with
// "comparison doesn't meet irreflexive requirements".
//
// Usage: speedup_krusader_descending_check

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>
#include <functional>
#include <vector>

#include "qt_family_sort.h"

using namespace qt_family;

namespace {

using Compare = std::function<bool(krusader::SortProps*, krusader::SortProps*)>;

/// Irreflexivity, asymmetry, transitivity and transitivity of incomparability, on all triples.
int violations(std::vector<krusader::SortProps>& sp, const Compare& less) {
    int v = 0;
    const int n = static_cast<int>(sp.size());
    auto L = [&](int a, int b) { return less(&sp[a], &sp[b]); };
    auto E = [&](int a, int b) { return !L(a, b) && !L(b, a); };
    for (int a = 0; a < n; ++a) {
        v += L(a, a);
        for (int b = 0; b < n; ++b) {
            v += a != b && L(a, b) && L(b, a);
            for (int c = 0; c < n; ++c) {
                v += L(a, b) && L(b, c) && !L(a, c);
                v += E(a, b) && E(b, c) && !E(a, c);
            }
        }
    }
    return v;
}

void check_fixes() {
    struct Spec { const char* name; bool dir; quint64 size; qint64 mtime; };
    const std::vector<Spec> spec = {{"README", false, 10, 5}, {"readme", false, 10, 5}, {"zeta", false, 3, 9},
                                    {"alpha", false, 7, 1},   {"src", true, 0, 4},      {"Src", true, 0, 4},
                                    {"docs", true, 0, 8},     {"build", true, 0, 2}};
    static std::vector<File> files(spec.size());
    static std::vector<krusader::FileItem> items;
    for (std::size_t i = 0; i < spec.size(); ++i) {
        files[i].name = QString::fromLatin1(spec[i].name);
        files[i].is_dir = spec[i].dir;
        files[i].size = spec[i].size;
        files[i].mtime = spec[i].mtime;
        items.push_back(krusader::FileItem{&files[i]});
    }
    const Compare current = krusader::itemGreaterThan;
    const Compare swapped = [](krusader::SortProps* a, krusader::SortProps* b) { return krusader::itemLessThan(b, a); };
    const Compare proposed = [](krusader::SortProps* a, krusader::SortProps* b) {
        return !krusader::itemLessThan(a, b) && krusader::itemLessThan(b, a);
    };
    const std::pair<int, const char*> columns[] = {{krusader::KrViewProperties::Name, "name"},
                                                   {krusader::KrViewProperties::Size, "size"},
                                                   {krusader::KrViewProperties::Modified, "modified"}};
    for (const auto& [column, label] : columns) {
        for (bool byName : {false, true}) {
            krusader::KrViewProperties props;
            props.sortColumn = column;
            props.sortOptions |= krusader::KrViewProperties::Descending;
            if (byName) {
                props.sortOptions |= krusader::KrViewProperties::AlwaysSortDirsByName;
            }
            std::vector<krusader::SortProps> sp;
            for (std::size_t i = 0; i < items.size(); ++i) {
                sp.emplace_back(&items[i], column, &props, false, false, static_cast<int>(i));
            }
            std::printf("descending by %-8s always-dirs-by-name %d: violations: current %2d, itemLessThan(b, a) %2d, proposed %d\n",
                        label, byName, violations(sp, current), violations(sp, swapped), violations(sp, proposed));
        }
    }
}

}  // namespace

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
    check_fixes();

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
