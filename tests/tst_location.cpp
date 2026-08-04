#include "tst_location.h"

#include "location.h"

#include <QDir>
#include <QTest>

void TestLocation::parsesLocalPaths()
{
    const Location local = Location::parse(QStringLiteral("/home/chase/Projects"));
    QVERIFY(local.isValid());
    QVERIFY(local.isLocal());
    QVERIFY(!local.isRemote());
    QCOMPARE(local.scheme(), QStringLiteral("file"));
    QCOMPARE(local.localPath(), QStringLiteral("/home/chase/Projects"));
    QCOMPARE(local.displayName(), QStringLiteral("Projects"));

    // file:// URIs resolve to the same thing a bare path would.
    const Location asUri = Location::parse(QStringLiteral("file:///home/chase/Projects"));
    QCOMPARE(asUri, local);
}

void TestLocation::expandsTilde()
{
    QCOMPARE(Location::parse(QStringLiteral("~")).localPath(), QDir::homePath());
    QCOMPARE(Location::parse(QStringLiteral("~/Projects")).localPath(),
             QDir::homePath() + QStringLiteral("/Projects"));
    // A leading tilde inside a name is not an expansion.
    QCOMPARE(Location::parse(QStringLiteral("/tmp/~backup")).localPath(),
             QStringLiteral("/tmp/~backup"));
}

void TestLocation::resolvesRelativeAgainstBase()
{
    const Location base = Location::parse(QStringLiteral("/home/chase"));
    QCOMPARE(Location::parse(QStringLiteral("Projects"), base).localPath(),
             QStringLiteral("/home/chase/Projects"));
    QCOMPARE(Location::parse(QStringLiteral("../etc"), base).localPath(),
             QStringLiteral("/home/etc"));

    // A relative path against a remote base stays remote.
    const Location remote = Location::parse(QStringLiteral("ssh://box/srv"));
    const Location child = Location::parse(QStringLiteral("www"), remote);
    QVERIFY(child.isRemote());
    QCOMPARE(child.toString(), QStringLiteral("ssh://box/srv/www"));
}

void TestLocation::normalizesDotSegments()
{
    QCOMPARE(Location::parse(QStringLiteral("/a//b/./c")).localPath(), QStringLiteral("/a/b/c"));
    QCOMPARE(Location::parse(QStringLiteral("/a/b/../c")).localPath(), QStringLiteral("/a/c"));
    QCOMPARE(Location::parse(QStringLiteral("/a/b/")).localPath(), QStringLiteral("/a/b"));
    // ".." must never climb above the root.
    QCOMPARE(Location::parse(QStringLiteral("/../../a")).localPath(), QStringLiteral("/a"));
    QCOMPARE(Location::parse(QStringLiteral("/")).localPath(), QStringLiteral("/"));
}

void TestLocation::parsesRemoteUris()
{
    const Location ssh = Location::parse(QStringLiteral("ssh://chase@box:2222/srv/www"));
    QVERIFY(ssh.isRemote());
    QVERIFY(!ssh.isLocal());
    QCOMPARE(ssh.scheme(), QStringLiteral("ssh"));
    QCOMPARE(ssh.host(), QStringLiteral("chase@box:2222"));
    QCOMPARE(ssh.path(), QStringLiteral("/srv/www"));
    QVERIFY(ssh.localPath().isEmpty());
    QCOMPARE(ssh.displayPath(), QStringLiteral("chase@box:2222:/srv/www"));

    const Location smb = Location::parse(QStringLiteral("smb://server/share"));
    QCOMPARE(smb.scheme(), QStringLiteral("smb"));
    QCOMPARE(smb.host(), QStringLiteral("server"));

    // An unknown scheme is not a URI; it is a filename that happens to contain a colon.
    const Location odd = Location::parse(QStringLiteral("/tmp/weird://name"));
    QVERIFY(odd.isLocal());
}

void TestLocation::parsesRcloneRemotes()
{
    const Location drive = Location::parse(QStringLiteral("rclone:gdrive:Photos/2026"));
    QVERIFY(drive.isRemote());
    QCOMPARE(drive.scheme(), QStringLiteral("rclone"));
    QCOMPARE(drive.host(), QStringLiteral("gdrive"));
    QCOMPARE(drive.path(), QStringLiteral("/Photos/2026"));
    QCOMPARE(drive.toString(), QStringLiteral("rclone:gdrive:Photos/2026"));
}

void TestLocation::roundTripsEveryScheme_data()
{
    QTest::addColumn<QString>("input");

    QTest::newRow("local") << QStringLiteral("/home/chase/Projects");
    QTest::newRow("local root") << QStringLiteral("/");
    QTest::newRow("ssh") << QStringLiteral("ssh://box/srv/www");
    QTest::newRow("ssh with user") << QStringLiteral("ssh://chase@box/srv");
    QTest::newRow("sftp") << QStringLiteral("sftp://box/home");
    QTest::newRow("smb") << QStringLiteral("smb://server/share");
    QTest::newRow("davs") << QStringLiteral("davs://host/remote.php/webdav");
    QTest::newRow("mtp") << QStringLiteral("mtp://phone/Internal");
    QTest::newRow("ftp") << QStringLiteral("ftp://host/pub");
    QTest::newRow("rclone") << QStringLiteral("rclone:s3:bucket/key");
}

void TestLocation::roundTripsEveryScheme()
{
    QFETCH(QString, input);

    const Location once = Location::parse(input);
    QCOMPARE(once.toString(), input);
    // Parsing our own output must be a fixed point, or navigation drifts.
    QCOMPARE(Location::parse(once.toString()), once);
}

void TestLocation::handlesSpacesAndEscapes()
{
    const Location spaced = Location::parse(QStringLiteral("/tmp/My Documents/a b"));
    QCOMPARE(spaced.localPath(), QStringLiteral("/tmp/My Documents/a b"));
    QCOMPARE(spaced.displayName(), QStringLiteral("a b"));

    // Percent escapes in a URI are decoded once, not twice.
    const Location escaped = Location::parse(QStringLiteral("ssh://box/srv/my%20files"));
    QCOMPARE(escaped.path(), QStringLiteral("/srv/my files"));

    // A literal percent in a local path is not an escape at all.
    const Location percent = Location::parse(QStringLiteral("/tmp/100%25"));
    QCOMPARE(percent.localPath(), QStringLiteral("/tmp/100%25"));
}

void TestLocation::segmentsForBreadcrumb()
{
    const QString home = QDir::homePath();

    const Location inHome = Location::fromLocalPath(home + QStringLiteral("/Projects/omafile"));
    QCOMPARE(inHome.segments(),
             QStringList({ QStringLiteral("~"), QStringLiteral("Projects"),
                           QStringLiteral("omafile") }));
    QCOMPARE(inHome.displayPath(), QStringLiteral("~/Projects/omafile"));

    const Location outsideHome = Location::fromLocalPath(QStringLiteral("/usr/share"));
    QCOMPARE(outsideHome.segments(),
             QStringList({ QStringLiteral("/"), QStringLiteral("usr"),
                           QStringLiteral("share") }));

    const Location remote = Location::parse(QStringLiteral("ssh://box/srv/www"));
    QCOMPARE(remote.segments(),
             QStringList({ QStringLiteral("box"), QStringLiteral("srv"),
                           QStringLiteral("www") }));
}

void TestLocation::parentAndChild()
{
    const Location start = Location::parse(QStringLiteral("/home/chase/Projects"));
    QCOMPARE(start.parent().localPath(), QStringLiteral("/home/chase"));
    QCOMPARE(start.child(QStringLiteral("omafile")).localPath(),
             QStringLiteral("/home/chase/Projects/omafile"));

    // Walking up a remote location keeps its scheme and host.
    const Location remote = Location::parse(QStringLiteral("ssh://box/srv/www"));
    QCOMPARE(remote.parent().toString(), QStringLiteral("ssh://box/srv"));
    QCOMPARE(remote.parent().parent().toString(), QStringLiteral("ssh://box/"));

    // A child whose name contains a slash must not silently become two levels.
    QCOMPARE(start.child(QStringLiteral("a/b")).localPath(),
             QStringLiteral("/home/chase/Projects/a/b"));
}

void TestLocation::rootHasNoParent()
{
    const Location root = Location::parse(QStringLiteral("/"));
    QVERIFY(root.isRoot());
    QCOMPARE(root.parent(), root);
    QCOMPARE(root.child(QStringLiteral("etc")).localPath(), QStringLiteral("/etc"));
}
