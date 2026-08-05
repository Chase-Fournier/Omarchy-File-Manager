#include "tst_places.h"

#include "places.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>

namespace {

// The row a given path occupies, or -1. Pins are found by target rather than by index
// because the sidebar also lists home, downloads and whatever is mounted.
int rowFor(Places &places, const QString &target)
{
    for (int i = 0; i < places.rowCount(); ++i) {
        if (places.data(places.index(i, 0), Places::TargetRole).toString() == target)
            return i;
    }
    return -1;
}

QString glyphAt(Places &places, int row)
{
    return places.data(places.index(row, 0), Places::GlyphRole).toString();
}

} // namespace

void TestPlaces::initTestCase()
{
    QVERIFY(m_root.isValid());
    m_realHome = qgetenv("HOME");
    m_realConfigHome = qgetenv("XDG_CONFIG_HOME");
    m_realRuntimeDir = qgetenv("XDG_RUNTIME_DIR");

    // All three, and for different reasons: the pins file lives under XDG_CONFIG_HOME, a
    // fake HOME keeps the developer's real ~/.ssh/config out of the sidebar so the rows
    // are deterministic, and a fake XDG_RUNTIME_DIR keeps the mount refcount sweep away
    // from any mount this machine is actually holding.
    const QString home = m_root.path() + QStringLiteral("/home");
    QVERIFY(QDir().mkpath(home + QStringLiteral("/.config")));
    QVERIFY(QDir().mkpath(m_root.path() + QStringLiteral("/run")));
    qputenv("HOME", home.toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", (home + QStringLiteral("/.config")).toLocal8Bit());
    qputenv("XDG_RUNTIME_DIR", (m_root.path() + QStringLiteral("/run")).toLocal8Bit());
}

void TestPlaces::cleanupTestCase()
{
    const auto restore = [](const char *name, const QByteArray &value) {
        if (value.isEmpty())
            qunsetenv(name);
        else
            qputenv(name, value);
    };
    restore("HOME", m_realHome);
    restore("XDG_CONFIG_HOME", m_realConfigHome);
    restore("XDG_RUNTIME_DIR", m_realRuntimeDir);
}

void TestPlaces::init()
{
    // Each test starts with nothing pinned.
    QFile::remove(pinsFile());
}

QString TestPlaces::pinsFile() const
{
    return m_root.path() + QStringLiteral("/home/.config/omafile/bookmarks");
}

// The whole point of a pin is that it is still there tomorrow, so the assertion is against
// a second Places that has only the file to go on.
void TestPlaces::pinsSurviveARestart()
{
    const QString folder = m_root.path() + QStringLiteral("/work");
    QVERIFY(QDir().mkpath(folder));

    {
        Places places;
        QVERIFY(!places.isBookmarked(folder));
        places.addBookmark(folder);
        QVERIFY(places.isBookmarked(folder));
        QVERIFY(rowFor(places, folder) >= 0);
    }

    QVERIFY2(QFileInfo::exists(pinsFile()), "pinning wrote nothing to disk");

    Places restarted;
    QVERIFY2(restarted.isBookmarked(folder), "a pin did not survive a restart");
    QVERIFY(rowFor(restarted, folder) >= 0);
}

// A pinned file is not a folder and does not behave like one — it opens rather than
// navigating — so it must not be drawn as one either.
void TestPlaces::pinnedFilesAreNotDrawnAsFolders()
{
    const QString folder = m_root.path() + QStringLiteral("/pinned-folder");
    const QString file = m_root.path() + QStringLiteral("/pinned-file.txt");
    QVERIFY(QDir().mkpath(folder));
    QFile handle(file);
    QVERIFY(handle.open(QIODevice::WriteOnly));
    handle.close();

    Places places;
    places.addBookmark(folder);
    places.addBookmark(file);

    const int folderRow = rowFor(places, folder);
    const int fileRow = rowFor(places, file);
    QVERIFY(folderRow >= 0);
    QVERIFY(fileRow >= 0);

    QVERIFY(!glyphAt(places, folderRow).isEmpty());
    QVERIFY(!glyphAt(places, fileRow).isEmpty());
    QVERIFY2(glyphAt(places, folderRow) != glyphAt(places, fileRow),
             "a pinned file is shown with the same glyph as a pinned folder");

    // Both are pins, so both are removable the same way.
    QCOMPARE(places.data(places.index(folderRow, 0), Places::KindRole).toInt(),
             int(Place::Bookmark));
    QCOMPARE(places.data(places.index(fileRow, 0), Places::KindRole).toInt(),
             int(Place::Bookmark));
}

void TestPlaces::unpinningRemovesOnlyThatOne()
{
    const QString first = m_root.path() + QStringLiteral("/one");
    const QString second = m_root.path() + QStringLiteral("/two");
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));

    Places places;
    places.addBookmark(first);
    places.addBookmark(second);

    places.removeBookmark(first);
    QVERIFY(!places.isBookmarked(first));
    QVERIFY2(places.isBookmarked(second), "unpinning one removed the other as well");
    QCOMPARE(rowFor(places, first), -1);
    QVERIFY(rowFor(places, second) >= 0);

    // And the removal reached the file, not just the list in memory.
    Places restarted;
    QVERIFY(!restarted.isBookmarked(first));
    QVERIFY(restarted.isBookmarked(second));
}

void TestPlaces::pinningTwiceIsANoOp()
{
    const QString folder = m_root.path() + QStringLiteral("/twice");
    QVERIFY(QDir().mkpath(folder));

    Places places;
    places.addBookmark(folder);
    const int before = places.rowCount();
    places.addBookmark(folder);

    QCOMPARE(places.rowCount(), before);
    QVERIFY(places.isBookmarked(folder));

    // One line in the file, not two — a duplicate would come back on the next start.
    QFile file(pinsFile());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(file.readAll());
    QCOMPARE(contents.count(folder), 1);
}
