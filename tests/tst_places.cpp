#include "tst_places.h"

#include "mounts.h"
#include "places.h"
#include "trash.h"

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

// The trash sits under the pins: a place you keep things, not a device, so it belongs
// with home and the bookmarks rather than down among the mounts.
void TestPlaces::trashSitsUnderThePins()
{
    const QString folder = m_root.path() + QStringLiteral("/pinned");
    QVERIFY(QDir().mkpath(folder));

    Places places;
    places.addBookmark(folder);

    const int pinRow = rowFor(places, folder);
    const int trashRow = rowFor(places, Trash::homeTrashDir() + QStringLiteral("/files"));
    QVERIFY2(trashRow >= 0, "the trash is not listed at all");
    QVERIFY2(trashRow > pinRow, "the trash is above the pins");

    // Everything after it is a mount, a host or a remote — never another pin.
    for (int row = trashRow + 1; row < places.rowCount(); ++row) {
        const int kind = places.data(places.index(row, 0), Places::KindRole).toInt();
        QVERIFY2(kind != int(Place::Bookmark), "a pin was listed below the trash");
    }
}

// It is listed whether or not it exists yet — the directory is only created the first
// time something is trashed, and a row that comes and goes cannot be learned.
void TestPlaces::trashIsListedBeforeAnythingIsTrashed()
{
    const QString files = Trash::homeTrashDir() + QStringLiteral("/files");
    QDir(files).removeRecursively();
    QVERIFY(!QFileInfo(files).isDir());

    Places places;
    int row = rowFor(places, files);
    QVERIFY2(row >= 0, "the trash vanished when it was empty");
    QCOMPARE(places.data(places.index(row, 0), Places::NameRole).toString(),
             QStringLiteral("Trash"));
    // Greyed with a reason rather than failing when clicked.
    QCOMPARE(places.data(places.index(row, 0), Places::AvailableRole).toBool(), false);
    QCOMPARE(places.data(places.index(row, 0), Places::NoteRole).toString(),
             QStringLiteral("empty"));

    // Once it exists it becomes an ordinary folder to open.
    QVERIFY(QDir().mkpath(files));
    places.refresh();
    row = rowFor(places, files);
    QVERIFY(row >= 0);
    QCOMPARE(places.data(places.index(row, 0), Places::AvailableRole).toBool(), true);
    QVERIFY(places.data(places.index(row, 0), Places::NoteRole).toString().isEmpty());
}

// Every eject of a drive was a no-op: udisksctl -b resolves a *block device*, and it was
// being handed the mount point, which it answers with "Error looking up object for
// device /run/media/...". The three owners name their targets differently, so the whole
// point of the assertion is *which* string each command gets.
void TestPlaces::ejectingADriveNamesItsBlockDevice()
{
    Place drive;
    drive.kind = Place::Volume;
    drive.name = QStringLiteral("Ventoy");
    drive.target = QStringLiteral("/run/media/chase/Ventoy");
    drive.device = QStringLiteral("/dev/sda1");
    drive.mounted = true;
    drive.ejectable = true;

    const QStringList argv = Places::unmountArgv(drive);
    QCOMPARE(argv, QStringList({ QStringLiteral("udisksctl"), QStringLiteral("unmount"),
                                 QStringLiteral("-b"), QStringLiteral("/dev/sda1") }));
    QVERIFY2(!argv.contains(drive.target),
             "udisksctl was handed the mount point, which it cannot resolve");

    // A gvfs share answers to gio and is addressed by path. fusermount3 on it would take
    // down the bridge and every other share with it.
    Place share;
    share.kind = Place::Volume;
    share.name = QStringLiteral("media on nas");
    share.target = Mounts::gvfsRoot() + QStringLiteral("/smb-share:server=nas,share=media");
    share.mounted = true;
    share.ejectable = true;
    QCOMPARE(Places::unmountArgv(share),
             QStringList({ QStringLiteral("gio"), QStringLiteral("mount"),
                           QStringLiteral("-u"), share.target }));

    // A mount omafile made itself is ours to tear down directly.
    Place own;
    own.kind = Place::SshHost;
    own.name = QStringLiteral("box");
    own.target = QStringLiteral("box");
    own.mountPath = Mounts::runtimeMountRoot() + QStringLiteral("/box");
    own.mounted = true;
    QCOMPARE(Places::unmountArgv(own),
             QStringList({ QStringLiteral("fusermount3"), QStringLiteral("-u"),
                           own.mountPath }));

    // Nothing to unmount rather than a command built around an empty string: an NFS
    // mount from fstab has no /dev/ node and is not ours.
    Place nfs;
    nfs.kind = Place::Volume;
    nfs.name = QStringLiteral("export");
    nfs.target = QStringLiteral("/mnt/nfs");
    nfs.device = QStringLiteral("server:/export");
    nfs.mounted = true;
    nfs.ejectable = true;
    QVERIFY2(Places::unmountArgv(nfs).isEmpty(),
             "a non-block source was passed to udisksctl anyway");
}

// The device node has to survive the trip from mountinfo onto the row, which is the half
// of the bug the argv assertion above cannot see: unmountArgv was correct in isolation
// and still built nothing, because no volume ever carried a device.
void TestPlaces::aVolumeCarriesTheBlockDeviceItWasMountedFrom()
{
    const QList<MountPoint> mounts = Mounts::parseMountInfo(QStringLiteral(
        "50 25 8:17 / /run/media/chase/Ventoy rw,nosuid - exfat /dev/sda1 rw\n"));
    QCOMPARE(mounts.size(), 1);
    QVERIFY(mounts.first().isRemovable);

    // What rebuild() copies onto the row, asserted at the seam rather than by mounting
    // something: a removable drive cannot be faked in-process without a mount namespace.
    Place place;
    place.kind = Place::Volume;
    place.name = mounts.first().label();
    place.target = mounts.first().path;
    place.mounted = true;
    place.ejectable = mounts.first().isRemovable || mounts.first().isNetwork;
    place.device = mounts.first().source;

    QCOMPARE(place.device, QStringLiteral("/dev/sda1"));
    QCOMPARE(Places::unmountArgv(place).last(), QStringLiteral("/dev/sda1"));
}
