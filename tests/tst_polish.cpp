#include "tst_polish.h"

#include "handlers.h"
#include "thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QTest>
#include <QUrl>

void TestPolish::initTestCase()
{
    QVERIFY(m_home.isValid());
    // Never write into the developer's real thumbnail cache.
    m_realCache = qgetenv("XDG_CACHE_HOME");
    qputenv("XDG_CACHE_HOME", m_home.path().toLocal8Bit());
}

void TestPolish::cleanupTestCase()
{
    if (m_realCache.isEmpty())
        qunsetenv("XDG_CACHE_HOME");
    else
        qputenv("XDG_CACHE_HOME", m_realCache);
}

// The spec hashes the file:// URI, not the path. Hashing the path instead would make
// every other application's cache entries invisible to us and ours to them.
void TestPolish::thumbnailPathFollowsTheSpec()
{
    const QString path = QStringLiteral("/home/chase/Pictures/holiday photo.jpg");
    const QString expectedDigest = QString::fromLatin1(
        QCryptographicHash::hash(QUrl::fromLocalFile(path).toEncoded(),
                                 QCryptographicHash::Md5)
            .toHex());

    const QString normal = Thumbnails::cachePathFor(path, Thumbnails::Normal);
    QVERIFY(normal.endsWith(QStringLiteral("/thumbnails/normal/%1.png").arg(expectedDigest)));

    const QString large = Thumbnails::cachePathFor(path, Thumbnails::Large);
    QVERIFY(large.endsWith(QStringLiteral("/thumbnails/large/%1.png").arg(expectedDigest)));

    // Hashing the bare path would be a different, wrong answer.
    const QString wrong = QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex());
    QVERIFY(!normal.contains(wrong));
}

void TestPolish::thumbnailRoundTrips()
{
    const QString source = m_home.path() + QStringLiteral("/picture.png");
    QImage original(64, 48, QImage::Format_RGB32);
    original.fill(Qt::darkCyan);
    QVERIFY(original.save(source));

    QVERIFY(Thumbnails::store(source, original, Thumbnails::Normal));
    const QImage loaded = Thumbnails::load(source, Thumbnails::Normal);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), original.size());
}

// A file edited after its thumbnail was made must not keep showing the old picture.
void TestPolish::staleThumbnailIsRejected()
{
    const QString source = m_home.path() + QStringLiteral("/changing.png");
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(source));
    QVERIFY(Thumbnails::store(source, image, Thumbnails::Normal));
    QVERIFY(!Thumbnails::load(source, Thumbnails::Normal).isNull());

    // Rewrite the source with a different mtime; the cached entry is now a lie.
    QTest::qSleep(1100);
    image.fill(Qt::blue);
    QVERIFY(image.save(source));

    QVERIFY(Thumbnails::load(source, Thumbnails::Normal).isNull());
}

// Something else may have written a PNG at that name without the spec's tags. Without a
// Thumb::MTime we cannot prove it is current, and a wrong picture is worse than none.
void TestPolish::untaggedThumbnailIsRejected()
{
    const QString source = m_home.path() + QStringLiteral("/untagged.png");
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(source));

    const QString cached = Thumbnails::cachePathFor(source, Thumbnails::Normal);
    QVERIFY(QDir().mkpath(QFileInfo(cached).absolutePath()));
    QVERIFY(image.save(cached)); // no Thumb:: tags at all

    QVERIFY(Thumbnails::load(source, Thumbnails::Normal).isNull());
}

void TestPolish::missingThumbnailIsNull()
{
    QVERIFY(Thumbnails::load(m_home.path() + QStringLiteral("/never-seen.png")).isNull());
    // A source that does not exist cannot be stored either.
    QImage image(8, 8, QImage::Format_RGB32);
    QVERIFY(!Thumbnails::store(m_home.path() + QStringLiteral("/absent.png"), image));
}

void TestPolish::scalingNeverEnlarges()
{
    QImage small(40, 30, QImage::Format_RGB32);
    const QImage keptSmall = Thumbnails::scaleForCache(small, Thumbnails::Normal);
    QCOMPARE(keptSmall.size(), small.size());

    QImage big(1000, 500, QImage::Format_RGB32);
    const QImage scaled = Thumbnails::scaleForCache(big, Thumbnails::Normal);
    QCOMPARE(scaled.width(), 128);
    QCOMPARE(scaled.height(), 64); // aspect ratio preserved
    QVERIFY(scaled.width() <= 128 && scaled.height() <= 128);
}

// A thumbnail can reveal the contents of a private file, so the spec requires 0600.
void TestPolish::thumbnailIsPrivate()
{
    const QString source = m_home.path() + QStringLiteral("/private.png");
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::black);
    QVERIFY(image.save(source));
    QVERIFY(Thumbnails::store(source, image, Thumbnails::Normal));

    const QFile::Permissions permissions =
        QFile::permissions(Thumbnails::cachePathFor(source, Thumbnails::Normal));
    QVERIFY(permissions.testFlag(QFileDevice::ReadOwner));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadGroup));
    QVERIFY(!permissions.testFlag(QFileDevice::ReadOther));
}

void TestPolish::parsesMimeAssociations()
{
    const QString cache = QStringLiteral(
        "[MIME Cache]\n"
        "text/plain=nvim.desktop;gedit.desktop;\n"
        "image/png=eog.desktop;gimp.desktop;\n");

    QCOMPARE(Handlers::parseAssociations(cache, QStringLiteral("text/plain"),
                                         QStringLiteral("MIME Cache")),
             QStringList({ QStringLiteral("nvim.desktop"), QStringLiteral("gedit.desktop") }));
    QCOMPARE(Handlers::parseAssociations(cache, QStringLiteral("image/png"),
                                         QStringLiteral("MIME Cache")).size(), 2);
    // A type nobody registered for yields nothing rather than everything.
    QVERIFY(Handlers::parseAssociations(cache, QStringLiteral("video/mp4"),
                                        QStringLiteral("MIME Cache")).isEmpty());
}

void TestPolish::ignoresOtherSections()
{
    const QString list = QStringLiteral(
        "[Default Applications]\n"
        "text/plain=chosen.desktop;\n"
        "\n"
        "[Removed Associations]\n"
        "text/plain=banned.desktop;\n");

    QCOMPARE(Handlers::parseAssociations(list, QStringLiteral("text/plain"),
                                         QStringLiteral("Default Applications")),
             QStringList({ QStringLiteral("chosen.desktop") }));
    // Removed associations must not be offered as handlers.
    QCOMPARE(Handlers::parseAssociations(list, QStringLiteral("text/plain"),
                                         QStringLiteral("Added Associations")),
             QStringList());
}

void TestPolish::readsLocalisedNames()
{
    const QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Text Editor\n"
        "Name[de]=Texteditor\n"
        "Exec=gedit %U\n"
        "\n"
        "[Desktop Action new-window]\n"
        "Name=New Window\n");

    // The action group's Name must not be mistaken for the application's.
    QCOMPARE(Handlers::displayNameOf(desktop, QStringLiteral("fallback")),
             QStringLiteral("Text Editor"));
    QCOMPARE(Handlers::displayNameOf(QStringLiteral("[Desktop Entry]\nExec=x\n"),
                                     QStringLiteral("thing.desktop")),
             QStringLiteral("thing.desktop"));
}
