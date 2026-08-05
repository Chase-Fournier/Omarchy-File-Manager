#include "tst_bulkrename.h"

#include "bulkrename.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using BulkRename::Plan;
using BulkRename::Rename;

namespace {

QStringList names(const QStringList &list)
{
    return list;
}

// Replays a plan against a real directory, which is the only way to be sure the ordering
// claim actually holds.
QStringList replay(const QStringList &originals, const QStringList &edited, QString *error)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        *error = QStringLiteral("no temp dir");
        return {};
    }

    // Each file's content is its original name, so a mix-up is visible afterwards.
    for (const QString &name : originals) {
        QFile file(dir.path() + QLatin1Char('/') + name);
        if (!file.open(QIODevice::WriteOnly)) {
            *error = QStringLiteral("could not create %1").arg(name);
            return {};
        }
        file.write(name.toUtf8());
    }

    const Plan plan = BulkRename::plan(originals, edited);
    if (!plan.ok) {
        *error = plan.error;
        return {};
    }

    for (const Rename &step : plan.steps) {
        const QString from = dir.path() + QLatin1Char('/') + step.first;
        const QString to = dir.path() + QLatin1Char('/') + step.second;
        if (QFileInfo::exists(to)) {
            *error = QStringLiteral("step %1 -> %2 would clobber an existing file")
                         .arg(step.first, step.second);
            return {};
        }
        if (!QFile::rename(from, to)) {
            *error = QStringLiteral("rename %1 -> %2 failed").arg(step.first, step.second);
            return {};
        }
    }

    // Report what each final name actually contains, so a swap that "worked" but crossed
    // its contents is caught.
    QStringList out;
    for (const QString &name : edited) {
        QFile file(dir.path() + QLatin1Char('/') + name);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("%1 is missing afterwards").arg(name);
            return {};
        }
        out.append(QString::fromUtf8(file.readAll()));
    }
    return out;
}

} // namespace

void TestBulkRename::renamesTheChangedLinesOnly()
{
    const Plan plan = BulkRename::plan({ "a.txt", "b.txt", "c.txt" },
                                       { "a.txt", "renamed.txt", "c.txt" });
    QVERIFY(plan.ok);
    QCOMPARE(plan.changed, 1);
    QCOMPARE(plan.steps.size(), 1);
    QCOMPARE(plan.steps.first(), Rename("b.txt", "renamed.txt"));
}

// §9: "Line count changed -> abort with an error, no partial renames."
void TestBulkRename::abortsWhenTheLineCountChanged()
{
    const Plan removed = BulkRename::plan({ "a", "b", "c" }, { "a", "b" });
    QVERIFY(!removed.ok);
    QVERIFY(removed.steps.isEmpty());
    QVERIFY(removed.error.contains(QStringLiteral("3")));
    QVERIFY(removed.error.contains(QStringLiteral("2")));

    const Plan added = BulkRename::plan({ "a" }, { "a", "b" });
    QVERIFY(!added.ok);
    QVERIFY(added.steps.isEmpty());
}

void TestBulkRename::rejectsEmptyNames()
{
    const Plan plan = BulkRename::plan({ "a", "b" }, { "a", "" });
    QVERIFY(!plan.ok);
    QVERIFY(plan.steps.isEmpty());
}

void TestBulkRename::rejectsSlashes()
{
    // A bulk rename renames; it does not quietly move things into subdirectories.
    const Plan plan = BulkRename::plan({ "a", "b" }, { "a", "sub/b" });
    QVERIFY(!plan.ok);
    QVERIFY(plan.error.contains(QStringLiteral("slash")));
}

void TestBulkRename::rejectsDuplicateTargets()
{
    const Plan plan = BulkRename::plan({ "a", "b" }, { "same", "same" });
    QVERIFY(!plan.ok);
    QVERIFY(plan.steps.isEmpty());
    QVERIFY(plan.error.contains(QStringLiteral("twice")));
}

// b already exists as another file's source, so a -> b must wait for b -> c.
void TestBulkRename::ordersSoNothingIsClobbered()
{
    const Plan plan = BulkRename::plan({ "a", "b" }, { "b", "c" });
    QVERIFY(plan.ok);
    QCOMPARE(plan.steps.size(), 2);
    QCOMPARE(plan.steps.at(0), Rename("b", "c"));
    QCOMPARE(plan.steps.at(1), Rename("a", "b"));
}

void TestBulkRename::breaksATwoWaySwap()
{
    const Plan plan = BulkRename::plan({ "a", "b" }, { "b", "a" });
    QVERIFY(plan.ok);
    // Two files change, but it takes three moves: one of them has to step aside first.
    QCOMPARE(plan.changed, 2);
    QCOMPARE(plan.steps.size(), 3);
    // One file steps aside first, and nothing is left parked under a temporary name.
    QVERIFY(plan.steps.first().second.startsWith(QStringLiteral(".omafile-rename-")));

    QStringList finalNames;
    for (const Rename &step : plan.steps) {
        if (!step.second.startsWith(QStringLiteral(".omafile-rename-")))
            finalNames.append(step.second);
    }
    finalNames.sort();
    QCOMPARE(finalNames, QStringList({ "a", "b" }));
}

void TestBulkRename::breaksAThreeWayRotation()
{
    const Plan plan = BulkRename::plan({ "a", "b", "c" }, { "b", "c", "a" });
    QVERIFY(plan.ok);
    QCOMPARE(plan.changed, 3);
    QCOMPARE(plan.steps.size(), 4); // three renames plus one detour
}

// a -> b -> c is not a cycle; it just has to run backwards.
void TestBulkRename::handlesAChainThatIsNotACycle()
{
    const Plan plan = BulkRename::plan({ "a", "b", "c" }, { "b", "c", "d" });
    QVERIFY(plan.ok);
    QCOMPARE(plan.steps.size(), 3);
    QCOMPARE(plan.steps.at(0), Rename("c", "d"));
    QCOMPARE(plan.steps.at(1), Rename("b", "c"));
    QCOMPARE(plan.steps.at(2), Rename("a", "b"));
}

void TestBulkRename::splitsEditorBuffers()
{
    // The trailing newline an editor adds is not a fourth, empty filename.
    QCOMPARE(BulkRename::linesOf(QStringLiteral("a\nb\nc\n")),
             QStringList({ "a", "b", "c" }));
    QCOMPARE(BulkRename::linesOf(QStringLiteral("a\nb\nc")), QStringList({ "a", "b", "c" }));
    QCOMPARE(BulkRename::linesOf(QString()), QStringList());
    // Leading and trailing spaces are legal in a filename and must survive.
    QCOMPARE(BulkRename::linesOf(QStringLiteral(" spaced \n")), QStringList({ " spaced " }));
}

void TestBulkRename::executesCorrectlyOnDisk_data()
{
    QTest::addColumn<QStringList>("originals");
    QTest::addColumn<QStringList>("edited");

    QTest::newRow("simple") << QStringList({ "a", "b" }) << QStringList({ "x", "y" });
    QTest::newRow("swap") << QStringList({ "a", "b" }) << QStringList({ "b", "a" });
    QTest::newRow("rotation") << QStringList({ "a", "b", "c" }) << QStringList({ "b", "c", "a" });
    QTest::newRow("chain") << QStringList({ "a", "b", "c" }) << QStringList({ "b", "c", "d" });
    QTest::newRow("reverse chain") << QStringList({ "a", "b", "c" })
                                   << QStringList({ "z", "a", "b" });
    QTest::newRow("untouched middle") << QStringList({ "a", "keep", "c" })
                                      << QStringList({ "c", "keep", "a" });
    QTest::newRow("numbering") << QStringList({ "img1.jpg", "img2.jpg", "img10.jpg" })
                               << QStringList({ "img01.jpg", "img02.jpg", "img10.jpg" });
}

// The real proof: replay the plan against actual files and check that every final name
// holds the content it should. An ordering bug shows up here as a lost or crossed file.
void TestBulkRename::executesCorrectlyOnDisk()
{
    QFETCH(QStringList, originals);
    QFETCH(QStringList, edited);

    QString error;
    const QStringList contents = replay(originals, edited, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // contents[i] is what the file now called edited[i] holds; it must be originals[i].
    QCOMPARE(contents, names(originals));
}
