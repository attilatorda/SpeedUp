# Ticket: Dolphin natural sort is not transitive

- **File at:** <https://bugs.kde.org/enter_bug.cgi?product=dolphin>
- **Search first** (product dolphin): "natural", "dotted", "decimal", "version", "411707"
- **Re-checked:** 2026-10-24 on Dolphin master `c5849f9`, with a standalone copy of
  `KFileItemModel::stringCompare()` and of the model's sort from that commit
  (not in a running Dolphin, not with Dolphin's test suite)
- **Best edited into your own words** before posting (KDE's draft AI guidance
  asks for self-written text)

## Filling in the form

| Field | What to enter |
|---|---|
| Product | `dolphin` (already set) |
| Reporter | your account (already set) |
| Component | `view-engine: general` (scroll down the list, past the `panels:` entries; the sort code lives in the view engine). If it isn't listed, pick `general` |
| Version First Reported In | `git-master` |
| Severity | `normal` (leave as is) |
| Platform | `Compiled Sources` if the list has it, otherwise leave `Other`: the bug was found in the source code, not in a distribution's package |
| OS | `All` if the list has it, otherwise `Linux`. The bug is in Dolphin's own code and doesn't depend on the OS |
| Summary | the **Summary** line below |
| Description | delete the template text, paste the **Description** below, then check it with the Preview tab |
| Attachment | none needed: the evidence is in the text |

Then press **Submit Bug**, and put the bug's link into
[../projects/dolphin.md](../projects/dolphin.md) and the table in
[README.md](README.md).

---

**Summary:**

Natural sorting is not transitive for dotted numbers since 0d10eff3; the order shown depends on listing order

**Description:**

SUMMARY

Commit 0d10eff3 ("kfileitemmodel: sort dotted numeric names naturally",
BUG 411707) changed how `KFileItemModel::stringCompare()` treats dotted
numbers. It compares a two-part numeric chain such as `6.10` as a decimal
(6.10 < 6.9). Chains of different lengths are compared segment by segment,
like versions (6.9 < 6.9.1 < 6.10). Together the two rules make the
comparison non-transitive:

```
stringCompare("kernel-6.10.tar.xz",  "kernel-6.9.tar.xz")   < 0
stringCompare("kernel-6.9.tar.xz",   "kernel-6.9.1.tar.xz") < 0
stringCompare("kernel-6.9.1.tar.xz", "kernel-6.10.tar.xz")  < 0
```

Another example: `app-1.10.zip < app-1.5.zip < app-1.7.1.zip < app-1.10.zip`.

A sort with a non-transitive comparison has no single correct result. So the
order Dolphin shows for such files depends on the order they are listed in. I
sorted the three kernel names with a copy of the model's sort from c5849f9,
once for each of the 6 possible input orders. That gives 3 different results:

```
input order starts with   shown order
kernel-6.9.tar.xz         kernel-6.9.1, kernel-6.10, kernel-6.9
kernel-6.9.1.tar.xz       kernel-6.10, kernel-6.9, kernel-6.9.1
kernel-6.10.tar.xz        kernel-6.9, kernel-6.9.1, kernel-6.10
```

STEPS TO REPRODUCE

1. In Dolphin's unit test, add these rows to
   `KFileItemModelTest::testStringCompare_data()`. All three pass, which
   means a < b < c < a:

```
QTest::newRow("cycle-1") << QStringLiteral("kernel-6.10.tar.xz") << QStringLiteral("kernel-6.9.tar.xz") << true << -1;
QTest::newRow("cycle-2") << QStringLiteral("kernel-6.9.tar.xz") << QStringLiteral("kernel-6.9.1.tar.xz") << true << -1;
QTest::newRow("cycle-3") << QStringLiteral("kernel-6.9.1.tar.xz") << QStringLiteral("kernel-6.10.tar.xz") << true << -1;
```

2. In Dolphin, create `kernel-6.9.tar.xz`, `kernel-6.9.1.tar.xz` and
   `kernel-6.10.tar.xz` in a folder and sort by name with natural sorting
   (the default).

OBSERVED RESULT

The comparison is cyclic for names that mix two-part and three-part dotted
numbers. Whatever order Dolphin shows, one of the three pairs is out of order
according to the comparison. The order can differ between folders and file
systems, because the listing order differs.

EXPECTED RESULT

One consistent rule for dotted numbers, so that every listing order gives the
same result. Which rule is a design decision. For example, comparing every
chain segment by segment would make `0.1 < 0.09` and break the "decimal" test
row, so the decimal case needs a rule that still holds against three-part
chains.

SOFTWARE/OS VERSIONS

Dolphin: git master c5849f9 (2026-10-24). The behaviour comes from 0d10eff3
(2026-04-02).
Operating System: any (the bug is in Dolphin's comparison code)
KDE Frameworks / Qt: not involved in the faulty rule; checked with Qt 6.4.2

ADDITIONAL INFORMATION

Found while checking sort comparisons for consistency with a standalone copy
of the code. An AI coding assistant helped with the analysis; the results
above are from running the copied code.
