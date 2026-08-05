#include "tst_directorymodel.h"

#include "directorymodel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <unistd.h>

namespace {

QStringList namesOf(const DirectoryModel &model)
{
    QStringList names;
    for (int i = 0; i < model.rowCount(); ++i)
        names << model.data(model.index(i), DirectoryModel::NameRole).toString();
    return names;
}

QString roleAt(const DirectoryModel &model, int row, int role)
{
    return model.data(model.index(row), role).toString();
}

int indexOfName(const DirectoryModel &model, const QString &name)
{
    return namesOf(model).indexOf(name);
}

} // namespace

void TestDirectoryModel::init()
{
    QVERIFY(m_dir.isValid());
}

void TestDirectoryModel::cleanup()
{
    // Each test starts from an empty tree.
    const QDir dir(m_dir.path());
    for (const QFileInfo &item : dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                  | QDir::NoDotAndDotDot)) {
        if (item.isDir() && !item.isSymLink())
            QDir(item.absoluteFilePath()).removeRecursively();
        else
            QFile::remove(item.absoluteFilePath());
    }
}

void TestDirectoryModel::write(const QString &relative, const QByteArray &contents)
{
    const QString full = m_dir.path() + QLatin1Char('/') + relative;
    QVERIFY(QDir().mkpath(QFileInfo(full).absolutePath()));
    QFile file(full);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(full));
    file.write(contents);
}

bool TestDirectoryModel::waitForIdle(DirectoryModel *model, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (model->loading() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    // Let the batches queued behind `finished` land as well.
    QCoreApplication::processEvents();
    return !model->loading();
}

void TestDirectoryModel::listsDirectoriesFirstThenNames()
{
    write(QStringLiteral("zebra.txt"));
    write(QStringLiteral("Apple.txt"));
    write(QStringLiteral("beta/keep"));
    write(QStringLiteral("Alpha/keep"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    // Directories first regardless of name, then case-insensitive name order.
    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("Alpha"), QStringLiteral("beta"),
                           QStringLiteral("Apple.txt"), QStringLiteral("zebra.txt") }));
    QCOMPARE(model.data(model.index(0), DirectoryModel::IsDirRole).toBool(), true);
    QCOMPARE(model.data(model.index(2), DirectoryModel::IsDirRole).toBool(), false);

    // Reversing the sort must not bury the directories below the files.
    model.setSortReversed(true);
    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("beta"), QStringLiteral("Alpha"),
                           QStringLiteral("zebra.txt"), QStringLiteral("Apple.txt") }));
}

void TestDirectoryModel::hiddenFilesAreOptIn()
{
    write(QStringLiteral("visible.txt"));
    write(QStringLiteral(".hidden"));
    write(QStringLiteral(".config/keep"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    QCOMPARE(namesOf(model), QStringList({ QStringLiteral("visible.txt") }));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.totalCount(), 3); // the count before hiding is still reported

    model.setShowHidden(true);
    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral(".config"), QStringLiteral(".hidden"),
                           QStringLiteral("visible.txt") }));
    QCOMPARE(model.data(model.index(0), DirectoryModel::IsHiddenRole).toBool(), true);
}

void TestDirectoryModel::filterNarrowsAndReportsMatchPositions()
{
    write(QStringLiteral("omafile.pro"));
    write(QStringLiteral("README.md"));
    write(QStringLiteral("theme.cpp"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    model.setFilter(QStringLiteral("me"));
    // Fuzzy, so all three match — o[m]afil[e].pro included. What matters is the ranking:
    // the two with an adjacent "me" beat the one where the letters are far apart.
    QCOMPARE(model.count(), 3);
    QCOMPARE(namesOf(model).last(), QStringLiteral("omafile.pro"));

    // The matched character positions are what let the delegate bold them.
    const int themeRow = indexOfName(model, QStringLiteral("theme.cpp"));
    const QVariantList themeHits =
        model.data(model.index(themeRow), DirectoryModel::MatchPositionsRole).toList();
    QCOMPARE(themeHits, QVariantList({ 3, 4 })); // the[me].cpp

    const int readmeRow = indexOfName(model, QStringLiteral("README.md"));
    const QVariantList readmeHits =
        model.data(model.index(readmeRow), DirectoryModel::MatchPositionsRole).toList();
    QCOMPARE(readmeHits, QVariantList({ 4, 5 })); // READ[ME].md

    // Filtering selects the first surviving row so Enter is immediately useful.
    QCOMPARE(model.currentIndex(), 0);

    // Fuzzy, not substring: non-adjacent characters match too.
    model.setFilter(QStringLiteral("tcp"));
    QCOMPARE(namesOf(model), QStringList({ QStringLiteral("theme.cpp") }));

    model.setFilter(QString());
    QCOMPARE(model.count(), 3);
    QVERIFY(model.data(model.index(0), DirectoryModel::MatchPositionsRole).toList().isEmpty());
}

// The whole point of the two-pass design: a listing costs no stat calls, and size/time
// appear only for rows the view actually asked about.
void TestDirectoryModel::statsArriveOnlyForRequestedRows()
{
    write(QStringLiteral("a.txt"), QByteArray(2048, 'x'));
    write(QStringLiteral("b.txt"), QByteArray(512, 'y'));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    // Nothing has been stat'd yet, so there is no size to show.
    QCOMPARE(roleAt(model, 0, DirectoryModel::SizeTextRole), QString());
    QCOMPARE(roleAt(model, 0, DirectoryModel::TimeTextRole), QString());

    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    model.ensureStats(0, 1);
    QVERIFY(changes.wait(5000));
    QCoreApplication::processEvents();

    QCOMPARE(roleAt(model, 0, DirectoryModel::SizeTextRole), QStringLiteral("2.0 KB"));
    QCOMPARE(roleAt(model, 1, DirectoryModel::SizeTextRole), QStringLiteral("512 B"));
    QVERIFY(!roleAt(model, 0, DirectoryModel::TimeTextRole).isEmpty());
}

// Scrolling asks for stats on the newly visible rows. If their arrival re-announced the
// current index, the view would scroll itself back to the selection every time a stat
// landed — which is exactly what broke scrolling in practice. The cursor moving and the
// cursor's row changing are different events and must stay different signals.
void TestDirectoryModel::arrivingStatsDoNotMoveTheCursor()
{
    for (int i = 0; i < 40; ++i)
        write(QStringLiteral("file-%1.txt").arg(i, 3, 10, QLatin1Char('0')));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));
    model.setCurrentIndex(5);

    QSignalSpy cursorMoved(&model, &DirectoryModel::currentIndexChanged);
    QSignalSpy detailsChanged(&model, &DirectoryModel::currentDetailsChanged);

    // Stat a window that does not contain the cursor, as scrolling away from it would.
    model.ensureStats(20, 39);
    QVERIFY(detailsChanged.wait(5000));
    QCoreApplication::processEvents();

    QCOMPARE(model.currentIndex(), 5);   // the cursor has not moved
    QCOMPARE(cursorMoved.count(), 0);    // and nothing said that it did
    QVERIFY(detailsChanged.count() > 0); // the status bar still gets its update
}

// §14: anything that shells out breaks on these. omafile does not shell out to list,
// which is exactly why they should keep working.
void TestDirectoryModel::handlesAwkwardFilenames()
{
    write(QStringLiteral("with space.txt"));
    write(QStringLiteral("with'quote.txt"));
    write(QStringLiteral("with\"double.txt"));
    write(QStringLiteral("with\nnewline.txt"));
    write(QStringLiteral("with;semi&amp.txt"));
    write(QStringLiteral("--dashes.txt"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    QCOMPARE(model.count(), 6);
    QVERIFY(namesOf(model).contains(QStringLiteral("with\nnewline.txt")));
    QVERIFY(namesOf(model).contains(QStringLiteral("with'quote.txt")));

    // A name with a newline must still round-trip into a usable path.
    const int row = indexOfName(model, QStringLiteral("with\nnewline.txt"));
    QVERIFY(row >= 0);
    QCOMPARE(model.rowPath(row), m_dir.path() + QStringLiteral("/with\nnewline.txt"));
}

void TestDirectoryModel::reportsBrokenSymlinks()
{
    write(QStringLiteral("real.txt"));
    QVERIFY(QFile::link(m_dir.path() + QStringLiteral("/real.txt"),
                        m_dir.path() + QStringLiteral("/good.link")));
    QVERIFY(::symlink("/nonexistent/target",
                      QFile::encodeName(m_dir.path() + QStringLiteral("/bad.link")).constData())
            == 0);

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    model.ensureStats(0, model.count() - 1);
    QVERIFY(changes.wait(5000));
    QCoreApplication::processEvents();

    const int bad = indexOfName(model, QStringLiteral("bad.link"));
    const int good = indexOfName(model, QStringLiteral("good.link"));
    QVERIFY(bad >= 0 && good >= 0);
    QCOMPARE(model.data(model.index(bad), DirectoryModel::IsBrokenRole).toBool(), true);
    QCOMPARE(model.data(model.index(good), DirectoryModel::IsBrokenRole).toBool(), false);
}

void TestDirectoryModel::reportsPermissionDenied()
{
    // Root is exempt from the permission bits, so there is nothing here to be denied and
    // the listing succeeds. Skipping says that plainly rather than failing on a machine
    // where the behaviour under test cannot occur — CI hit this by running its container
    // as root, and the fix there is to run the suite as somebody else.
    if (::geteuid() == 0)
        QSKIP("running as root: permission bits do not apply");

    const QString locked = m_dir.path() + QStringLiteral("/locked");
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QFile::setPermissions(locked, QFileDevice::WriteOwner));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(locked));
    QVERIFY(waitForIdle(&model));

    QCOMPARE(model.count(), 0);
    QVERIFY(!model.error().isEmpty());

    // Restore so the temp dir can be cleaned up.
    QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
}

void TestDirectoryModel::navigationSelectsTheDirectoryJustLeft()
{
    write(QStringLiteral("alpha/keep"));
    write(QStringLiteral("beta/keep"));
    write(QStringLiteral("gamma/keep"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    const int beta = indexOfName(model, QStringLiteral("beta"));
    model.setCurrentIndex(beta);
    model.activate(beta);
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.location().displayName(), QStringLiteral("beta"));

    // Going back up must land on `beta`, not on the first row: Backspace then Enter
    // should return you where you were.
    model.goParent();
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.currentName(), QStringLiteral("beta"));
}

// Walking up a path and back down should retrace it: the sequence
// downloads -> ~ -> home -> / must come back / -> home -> ~ -> downloads without having
// to hunt for the right row at each level.
void TestDirectoryModel::remembersWhereTheCursorWasInEachDirectory()
{
    write(QStringLiteral("home/chase/downloads/keep"));
    write(QStringLiteral("home/chase/documents/keep"));
    write(QStringLiteral("home/other/keep"));
    write(QStringLiteral("var/keep"));

    DirectoryModel model;
    const auto enter = [&](const QString &name) {
        const int row = indexOfName(model, name);
        QVERIFY(row >= 0);
        model.setCurrentIndex(row);
        model.activate(row);
        QVERIFY(waitForIdle(&model));
    };

    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    // Down: root -> home -> chase -> downloads.
    enter(QStringLiteral("home"));
    enter(QStringLiteral("chase"));
    enter(QStringLiteral("downloads"));
    QCOMPARE(model.location().displayName(), QStringLiteral("downloads"));

    // Up to the top again.
    for (int i = 0; i < 3; ++i) {
        model.goParent();
        QVERIFY(waitForIdle(&model));
    }
    QCOMPARE(model.currentName(), QStringLiteral("home"));

    // Back down: each level lands on the entry it was left on, not on the first row.
    model.activate(model.currentIndex());
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.currentName(), QStringLiteral("chase"));

    model.activate(model.currentIndex());
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.currentName(), QStringLiteral("downloads"));
}

// Anything asked for by name wins over what the cursor happened to be on last time.
void TestDirectoryModel::cursorMemoryYieldsToAnExplicitSelection()
{
    write(QStringLiteral("sub/alpha.txt"));
    write(QStringLiteral("sub/beta.txt"));
    write(QStringLiteral("sub/gamma.txt"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path() + QStringLiteral("/sub")));
    QVERIFY(waitForIdle(&model));
    model.setCurrentIndex(indexOfName(model, QStringLiteral("gamma.txt")));

    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    // Returning would normally restore gamma.txt...
    model.setLocation(Location::fromLocalPath(m_dir.path() + QStringLiteral("/sub")));
    model.selectByName(QStringLiteral("alpha.txt"));
    QVERIFY(waitForIdle(&model));
    // ...but an explicit request outranks it.
    QCOMPARE(model.currentName(), QStringLiteral("alpha.txt"));
}

void TestDirectoryModel::watcherAppliesDiffWithoutResetting()
{
    write(QStringLiteral("a.txt"));
    write(QStringLiteral("c.txt"));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.count(), 2);

    model.setCurrentIndex(1); // c.txt

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);

    write(QStringLiteral("b.txt"));

    QVERIFY(inserts.wait(10000));
    QVERIFY(waitForIdle(&model));

    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                           QStringLiteral("c.txt") }));
    // A reset would throw away scroll position and selection; a diff must not.
    QCOMPARE(resets.count(), 0);
    // The selection follows the file it was on, not the row number it occupied.
    QCOMPARE(model.currentName(), QStringLiteral("c.txt"));

    QSignalSpy removes(&model, &QAbstractItemModel::rowsRemoved);
    QVERIFY(QFile::remove(m_dir.path() + QStringLiteral("/a.txt")));
    QVERIFY(removes.wait(10000));
    QVERIFY(waitForIdle(&model));

    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("b.txt"), QStringLiteral("c.txt") }));
    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.currentName(), QStringLiteral("c.txt"));
}

void TestDirectoryModel::sortingBySizeAndTime()
{
    write(QStringLiteral("small.bin"), QByteArray(100, 'x'));
    write(QStringLiteral("big.bin"), QByteArray(50000, 'x'));
    write(QStringLiteral("medium.bin"), QByteArray(5000, 'x'));

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(m_dir.path()));
    QVERIFY(waitForIdle(&model));

    // Size sorting needs stats the listing pass deliberately skipped, so the model has
    // to go get them before the order means anything.
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    model.setSortMode(DirectoryModel::SortSize);
    QVERIFY(changes.wait(5000));
    QCoreApplication::processEvents();

    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("small.bin"), QStringLiteral("medium.bin"),
                           QStringLiteral("big.bin") }));

    model.setSortReversed(true);
    QCOMPARE(namesOf(model),
             QStringList({ QStringLiteral("big.bin"), QStringLiteral("medium.bin"),
                           QStringLiteral("small.bin") }));
}

// §12: a 10k-entry directory completes in under 150 ms.
void TestDirectoryModel::listing10kIsWithinBudget()
{
    const QString big = m_dir.path() + QStringLiteral("/big");
    QVERIFY(QDir().mkpath(big));
    for (int i = 0; i < 10000; ++i) {
        QFile file(big + QStringLiteral("/entry-%1.txt").arg(i, 5, 10, QLatin1Char('0')));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    DirectoryModel model;
    QElapsedTimer timer;
    timer.start();
    model.setLocation(Location::fromLocalPath(big));
    QVERIFY(waitForIdle(&model));
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(model.count(), 10000);
    qInfo("listed 10k entries in %lldms (budget 150ms)", static_cast<long long>(elapsed));
    QVERIFY2(elapsed < 150, qPrintable(QStringLiteral("took %1ms").arg(elapsed)));
}

// §12: a keystroke reaches the filtered list in under 5 ms.
void TestDirectoryModel::filteringAKeystrokeIsWithinBudget()
{
    const QString big = m_dir.path() + QStringLiteral("/big");
    QVERIFY(QDir().mkpath(big));
    for (int i = 0; i < 10000; ++i) {
        QFile file(big + QStringLiteral("/entry-%1.txt").arg(i, 5, 10, QLatin1Char('0')));
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    DirectoryModel model;
    model.setLocation(Location::fromLocalPath(big));
    QVERIFY(waitForIdle(&model));
    QCOMPARE(model.count(), 10000);

    QElapsedTimer timer;
    timer.start();
    model.setFilter(QStringLiteral("entry-042"));
    const qint64 elapsed = timer.elapsed();

    // Fuzzy matching is broader than a prefix scan, so the exact count is a property of
    // the scorer, not of this test; what this test guards is the §12 latency budget.
    QVERIFY(model.count() > 0);
    QVERIFY(model.count() < 10000);
    // The best match must still be an exact prefix hit.
    QVERIFY(namesOf(model).first().startsWith(QStringLiteral("entry-042")));
    qInfo("filtered 10k entries in %lldms (budget 5ms)", static_cast<long long>(elapsed));
    QVERIFY2(elapsed < 5, qPrintable(QStringLiteral("took %1ms").arg(elapsed)));
}
