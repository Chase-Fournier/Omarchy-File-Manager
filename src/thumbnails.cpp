#include "thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString sizeDirectory(Thumbnails::Size size)
{
    return size == Thumbnails::Large ? QStringLiteral("large") : QStringLiteral("normal");
}

QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/thumbnails");
}

// The spec hashes the *URI*, not the path — which is why this has to go through QUrl
// rather than being MD5'd directly. Get this wrong and every other application's cache
// entries are invisible to us.
QString uriFor(const QString &path)
{
    return QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded());
}

qint64 mtimeOf(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() ? info.lastModified().toSecsSinceEpoch() : -1;
}

} // namespace

namespace Thumbnails {

QString cachePathFor(const QString &path, Size size)
{
    const QByteArray digest =
        QCryptographicHash::hash(uriFor(path).toUtf8(), QCryptographicHash::Md5);
    return cacheRoot() + QLatin1Char('/') + sizeDirectory(size) + QLatin1Char('/')
        + QString::fromLatin1(digest.toHex()) + QStringLiteral(".png");
}

QImage load(const QString &path, Size size)
{
    const QString cached = cachePathFor(path, size);
    if (!QFileInfo::exists(cached))
        return {};

    // Read through QImage, not QImageReader: QImageWriter::setText and
    // QImageReader::text do not round-trip here — the tags write but come back empty —
    // whereas QImage's own setText/text pair works. Verified both ways before choosing.
    const QImage image(cached);
    if (image.isNull())
        return {};

    const QString recorded = image.text(QStringLiteral("Thumb::MTime"));
    // No tag means we cannot prove it is current, and a wrong picture is worse than none.
    if (recorded.isEmpty())
        return {};

    const qint64 current = mtimeOf(path);
    if (current < 0 || recorded.toLongLong() != current)
        return {};

    return image;
}

bool store(const QString &path, const QImage &image, Size size)
{
    if (image.isNull())
        return false;

    const qint64 mtime = mtimeOf(path);
    if (mtime < 0)
        return false;

    const QString cached = cachePathFor(path, size);
    QDir().mkpath(QFileInfo(cached).absolutePath());

    // The spec's tags go on the image itself; see the note in load() for why.
    QImage tagged = image;
    tagged.setText(QStringLiteral("Thumb::URI"), uriFor(path));
    tagged.setText(QStringLiteral("Thumb::MTime"), QString::number(mtime));
    if (!tagged.save(cached, "png"))
        return false;

    // The spec requires 0600: a thumbnail can reveal the contents of a private file.
    QFile::setPermissions(cached, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QImage scaleForCache(const QImage &image, Size size)
{
    if (image.isNull())
        return image;
    const int limit = int(size);
    if (image.width() <= limit && image.height() <= limit)
        return image;
    return image.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace Thumbnails
