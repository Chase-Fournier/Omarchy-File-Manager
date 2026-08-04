#include "trash.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

QString errorString()
{
    return QString::fromLocal8Bit(::strerror(errno));
}

dev_t deviceOf(const QString &path)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0)
        return 0;
    return info.st_dev;
}

// "report.txt" -> "report 2.txt", "report 3.txt"; "archive.tar.gz" keeps its full suffix
// only if the split point is a real extension, which is why lastIndexOf is right here.
QString numberedName(const QString &name, int counter)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0)
        return QStringLiteral("%1 %2").arg(name).arg(counter);
    return QStringLiteral("%1 %2%3").arg(name.left(dot)).arg(counter).arg(name.mid(dot));
}

bool ensureTrashLayout(const QString &trashDir, QString *error)
{
    QDir dir;
    if (!dir.mkpath(trashDir + QStringLiteral("/files"))
        || !dir.mkpath(trashDir + QStringLiteral("/info"))) {
        if (error)
            *error = QStringLiteral("could not create %1").arg(trashDir);
        return false;
    }
    return true;
}

} // namespace

QString Trash::homeTrashDir()
{
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return dataHome + QStringLiteral("/Trash");
}

QString Trash::topDirFor(const QString &path)
{
    const QFileInfo info(path);
    QString current = info.absolutePath();
    const dev_t device = deviceOf(current);
    if (device == 0)
        return QStringLiteral("/");

    // Walk up while we stay on the same device; the last such directory is the mount point.
    QString candidate = current;
    while (current != QLatin1String("/")) {
        const QString parent = QFileInfo(current).absolutePath();
        if (parent == current)
            break;
        if (deviceOf(parent) != device)
            break;
        candidate = parent;
        current = parent;
    }
    return candidate;
}

QString Trash::trashDirFor(const QString &path, QString *error)
{
    const QString home = homeTrashDir();

    // The common case: the file lives on the same filesystem as the home trash, so the
    // move into it is a rename and the spec's volume rules never come up.
    if (deviceOf(QFileInfo(path).absolutePath()) == deviceOf(QFileInfo(home).absolutePath())
        || deviceOf(QFileInfo(path).absolutePath()) == deviceOf(QDir::homePath())) {
        return ensureTrashLayout(home, error) ? home : QString();
    }

    const QString topDir = topDirFor(path);
    const uint uid = ::getuid();

    // $topdir/.Trash/$uid, but only if the admin-created .Trash is a sticky, real
    // directory — the spec requires refusing it otherwise, since a symlink there would
    // be a trivial way to redirect other users' deletions.
    const QString shared = topDir + QStringLiteral("/.Trash");
    struct stat sharedInfo;
    if (::lstat(QFile::encodeName(shared).constData(), &sharedInfo) == 0
        && S_ISDIR(sharedInfo.st_mode) && !S_ISLNK(sharedInfo.st_mode)
        && (sharedInfo.st_mode & S_ISVTX)) {
        const QString mine = shared + QLatin1Char('/') + QString::number(uid);
        if (ensureTrashLayout(mine, error))
            return mine;
    }

    const QString fallback = topDir + QStringLiteral("/.Trash-") + QString::number(uid);
    return ensureTrashLayout(fallback, error) ? fallback : QString();
}

bool Trash::reserveName(const QString &trashDir, const QString &baseName,
                        const QString &recordedPath, QString *chosenName, QString *infoPath,
                        QString *error)
{
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));

    // Path is percent-encoded per the spec, with '/' left readable.
    const QString encoded =
        QString::fromLatin1(QUrl::toPercentEncoding(recordedPath, QByteArrayLiteral("/")));
    const QByteArray contents =
        QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n").arg(encoded, stamp).toUtf8();

    for (int counter = 1; counter < 10000; ++counter) {
        const QString name = counter == 1 ? baseName : numberedName(baseName, counter);
        const QString info = trashDir + QStringLiteral("/info/") + name
            + QStringLiteral(".trashinfo");

        // O_EXCL is the whole mechanism: whoever creates the info file owns that name.
        const int fd = ::open(QFile::encodeName(info).constData(), O_WRONLY | O_CREAT | O_EXCL,
                              0600);
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            if (error)
                *error = errorString();
            return false;
        }

        const qint64 written = ::write(fd, contents.constData(), contents.size());
        ::close(fd);
        if (written != contents.size()) {
            ::unlink(QFile::encodeName(info).constData());
            if (error)
                *error = errorString();
            return false;
        }

        // The name is only truly free if files/ does not already hold it. A stale entry
        // there (someone else's crash) means we keep looking.
        const QString target = trashDir + QStringLiteral("/files/") + name;
        if (QFileInfo::exists(target)) {
            ::unlink(QFile::encodeName(info).constData());
            continue;
        }

        *chosenName = name;
        *infoPath = info;
        return true;
    }

    if (error)
        *error = QStringLiteral("no free name in the trash");
    return false;
}

bool Trash::moveToTrash(const QString &path, Item *item, QString *error)
{
    const QFileInfo source(path);
    if (!source.exists() && !source.isSymLink()) {
        if (error)
            *error = QStringLiteral("%1 does not exist").arg(path);
        return false;
    }

    const QString absolute = source.absoluteFilePath();
    const QString trashDir = trashDirFor(absolute, error);
    if (trashDir.isEmpty())
        return false;

    // Inside a volume trash the recorded path is relative to the mount point, so the
    // trash still restores correctly if the volume is mounted somewhere else next time.
    QString recorded = absolute;
    if (trashDir != homeTrashDir()) {
        const QString topDir = topDirFor(absolute);
        if (topDir != QLatin1String("/") && absolute.startsWith(topDir + QLatin1Char('/')))
            recorded = absolute.mid(topDir.size() + 1);
    }

    QString name;
    QString infoPath;
    if (!reserveName(trashDir, source.fileName(), recorded, &name, &infoPath, error))
        return false;

    const QString target = trashDir + QStringLiteral("/files/") + name;
    if (::rename(QFile::encodeName(absolute).constData(),
                 QFile::encodeName(target).constData())
        != 0) {
        if (error)
            *error = errorString();
        ::unlink(QFile::encodeName(infoPath).constData());
        return false;
    }

    if (item) {
        item->originalPath = absolute;
        item->trashedPath = target;
        item->infoPath = infoPath;
    }
    return true;
}

bool Trash::restore(const Item &item, QString *error)
{
    if (!item.isValid()) {
        if (error)
            *error = QStringLiteral("nothing to restore");
        return false;
    }

    QDir().mkpath(QFileInfo(item.originalPath).absolutePath());

    if (::rename(QFile::encodeName(item.trashedPath).constData(),
                 QFile::encodeName(item.originalPath).constData())
        != 0) {
        if (error)
            *error = errorString();
        return false;
    }

    ::unlink(QFile::encodeName(item.infoPath).constData());
    return true;
}
