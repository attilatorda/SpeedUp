// Checks that IcuNaturalPrefixKey never contradicts Dolphin's natural name
// order: walking Dolphin's sorted output, the key must not decrease between
// neighbours (up to 8 apart) or between random pairs.
//
// Usage: speedup_verify_icu_key LOCALE DATASET.tsv...   (e.g. cs_CZ, ja_JP)
// Exit status 0 when no violation was found.

#include <QCoreApplication>
#include <QLocale>

#include <cstdio>
#include <random>
#include <vector>

#include "../common/dataset.h"
#include "dolphin_sort.h"
#include "icu_natural_key.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s LOCALE DATASET.tsv...\n", argv[0]);
        return 2;
    }
    const QLocale locale(QString::fromLatin1(argv[1]));
    QLocale::setDefault(locale);
    long checked = 0;
    long violations = 0;
    for (int f = 2; f < argc; ++f) {
        const auto entries = speedup_bench::load_listing(argv[f]);
        std::vector<dolphin::BenchItem> items(entries.size());
        QList<dolphin::BenchItem*> list;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            items[i].text = items[i].name = QString::fromStdString(entries[i].name);
            items[i].url = QUrl::fromLocalFile(QStringLiteral("/bench/") + QString::fromStdString(entries[i].path));
            items[i].isDir = entries[i].is_dir;
            list.append(&items[i]);
        }
        dolphin::Model model(locale);
        const IcuNaturalPrefixKey key(locale);
        model.sort(list.begin(), list.end());
        std::vector<std::uint64_t> keys(static_cast<std::size_t>(list.size()));
        for (int i = 0; i < list.size(); ++i) {
            keys[static_cast<std::size_t>(i)] = key(list[i]->text);
        }
        auto check = [&](int i, int j) {  // i comes before j in Dolphin's order
            if (list[i]->isDir != list[j]->isDir) {
                return;  // folders first is a group, not part of the key
            }
            ++checked;
            if (keys[static_cast<std::size_t>(j)] < keys[static_cast<std::size_t>(i)] &&
                model.lessThan(list[i], list[j], model.m_collator)) {
                if (violations++ < 10) {
                    std::printf("violation: '%s' sorts before '%s' but has a larger key\n", qPrintable(list[i]->text),
                                qPrintable(list[j]->text));
                }
            }
        };
        for (int i = 0; i < list.size(); ++i) {
            for (int d = 1; d <= 8 && i + d < list.size(); ++d) {
                check(i, i + d);
            }
        }
        std::mt19937 rng(7);
        for (int t = 0; t < 300000 && list.size() > 1; ++t) {
            const int i = static_cast<int>(rng() % static_cast<unsigned>(list.size()));
            const int j = static_cast<int>(rng() % static_cast<unsigned>(list.size()));
            if (i < j) {
                check(i, j);
            } else if (j < i) {
                check(j, i);
            }
        }
    }
    std::printf("%s: %ld pairs checked, %ld key violations\n", argv[1], checked, violations);
    return violations == 0 ? 0 : 1;
}
