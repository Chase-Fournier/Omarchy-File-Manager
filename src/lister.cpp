#include "lister.h"

#include <QFile>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {

// The first flush is small so rows appear immediately on a huge directory; later ones
// are large because per-batch signal overhead is what costs at 100k entries.
constexpr int kFirstBatch = 128;
constexpr int kBatch = 2048;

Entry::Type typeFromDirent(unsigned char dType)
{
    switch (dType) {
    case DT_DIR:
        return Entry::Directory;
    case DT_REG:
        return Entry::File;
    case DT_LNK:
        return Entry::Symlink;
    case DT_UNKNOWN:
        return Entry::Unknown; // some filesystems don't fill d_type; stat resolves it
    default:
        return Entry::Other;
    }
}

Entry::Type typeFromMode(mode_t mode)
{
    if (S_ISDIR(mode))
        return Entry::Directory;
    if (S_ISREG(mode))
        return Entry::File;
    if (S_ISLNK(mode))
        return Entry::Symlink;
    return Entry::Other;
}

bool isDotOrDotDot(const char *name)
{
    return name[0] == '.'
        && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

} // namespace

Lister::Lister(QObject *parent)
    : QObject(parent)
{
}

void Lister::list(const QString &path, quint64 generation)
{
    if (cancelled(generation))
        return;

    // opendir/readdir rather than QDirIterator: Qt's iterator stats entries to satisfy
    // its filters, and skipping that is the whole §12 listing budget.
    DIR *dir = ::opendir(QFile::encodeName(path).constData());
    if (!dir) {
        emit failed(generation, QString::fromLocal8Bit(::strerror(errno)));
        return;
    }

    QList<Entry> batchBuffer;
    batchBuffer.reserve(kFirstBatch);
    int total = 0;
    int flushAt = kFirstBatch;

    while (struct dirent *item = ::readdir(dir)) {
        if (cancelled(generation)) {
            ::closedir(dir);
            return;
        }
        if (isDotOrDotDot(item->d_name))
            continue;

        Entry entry;
        // decodeName, not fromUtf8: filenames are bytes and need not be valid UTF-8.
        entry.name = QFile::decodeName(item->d_name);
        entry.type = typeFromDirent(item->d_type);
        batchBuffer.append(std::move(entry));
        ++total;

        if (batchBuffer.size() >= flushAt) {
            emit batch(generation, batchBuffer);
            batchBuffer.clear();
            batchBuffer.reserve(kBatch);
            flushAt = kBatch;
        }
    }

    ::closedir(dir);

    if (!batchBuffer.isEmpty())
        emit batch(generation, batchBuffer);
    emit finished(generation, total);
}

void Lister::statRange(const QString &path, quint64 generation, int firstRow,
                       const QStringList &names)
{
    if (cancelled(generation))
        return;

    QList<Entry> out;
    out.reserve(names.size());

    const QByteArray dirBytes = QFile::encodeName(path);
    for (const QString &name : names) {
        if (cancelled(generation))
            return;

        Entry entry;
        entry.name = name;

        QByteArray full = dirBytes;
        full += '/';
        full += QFile::encodeName(name);

        struct stat info;
        if (::lstat(full.constData(), &info) == 0) {
            entry.type = typeFromMode(info.st_mode);
            entry.mode = info.st_mode;
            entry.size = info.st_size;
            entry.mtime = info.st_mtime;

            if (entry.type == Entry::Symlink) {
                // Report the target's size and mtime, which is what the user means by
                // "how big is this". A stat that fails leaves linkTarget Unknown, which
                // is how the UI knows the link is broken.
                struct stat target;
                if (::stat(full.constData(), &target) == 0) {
                    entry.linkTarget = typeFromMode(target.st_mode);
                    entry.size = target.st_size;
                    entry.mtime = target.st_mtime;
                }
            }
            entry.statted = true;
        } else {
            // Permission denied or a race with deletion: mark it done so the row stops
            // being re-requested forever.
            entry.statted = true;
        }

        out.append(std::move(entry));
    }

    emit statsReady(generation, firstRow, out);
}
