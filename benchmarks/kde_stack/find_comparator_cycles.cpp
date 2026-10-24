// Finds cycles (a < b < c < a) in Dolphin's natural name comparison.
//
// A sort comparison must be transitive; when it is not, the order a file
// manager shows depends on the order the folder was listed in. Dolphin's
// decimal-aware natural compare reads a two-part number ("6.10") as a decimal
// but compares two-part with three-part numbers ("6.9.1") part by part, which
// makes version-numbered names cycle.
//
// Usage: speedup_find_comparator_cycles [NAME...]   (defaults to version-numbered downloads)

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>
#include <vector>

#include "dolphin_sort.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QStringList names;
    for (int i = 1; i < argc; ++i) {
        names << QString::fromLocal8Bit(argv[i]);
    }
    if (names.isEmpty()) {
        names = {QStringLiteral("kernel-6.10.tar.xz"), QStringLiteral("kernel-6.9.tar.xz"), QStringLiteral("kernel-6.9.1.tar.xz"),
                 QStringLiteral("app-1.10.zip"), QStringLiteral("app-1.5.zip"), QStringLiteral("app-1.7.1.zip")};
    }
    const QLocale locale(QStringLiteral("en_US"));
    const dolphin::Model model(locale);
    std::vector<dolphin::BenchItem> items(static_cast<std::size_t>(names.size()));
    for (int i = 0; i < names.size(); ++i) {
        items[static_cast<std::size_t>(i)].text = items[static_cast<std::size_t>(i)].name = names[i];
        items[static_cast<std::size_t>(i)].url = QUrl::fromLocalFile(QStringLiteral("/bench/%1").arg(i));
    }
    auto less = [&](std::size_t a, std::size_t b) { return model.lessThan(&items[a], &items[b], model.m_collator); };
    int cycles = 0;
    for (std::size_t a = 0; a < items.size(); ++a) {
        for (std::size_t b = 0; b < items.size(); ++b) {
            for (std::size_t c = a + 1; c < items.size(); ++c) {  // report each cycle once
                if (a != b && b != c && less(a, b) && less(b, c) && less(c, a) && a < b) {
                    std::printf("cycle: %s < %s < %s < %s\n", qPrintable(names[static_cast<int>(a)]),
                                qPrintable(names[static_cast<int>(b)]), qPrintable(names[static_cast<int>(c)]),
                                qPrintable(names[static_cast<int>(a)]));
                    ++cycles;
                }
            }
        }
    }
    std::printf("%d cycle(s) found\n", cycles);
    return 0;
}
