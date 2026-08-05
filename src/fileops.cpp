#include "fileops.h"

#include "bulkrename.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <climits>
#include <unistd.h>

namespace {

QString errorString()
{
    return QString::fromLocal8Bit(::strerror(errno));
}

QString joinPath(const QString &directory, const QString &name)
{
    return directory + QLatin1Char('/') + name;
}

// Preserve mode and mtime, per §8. Ownership is deliberately not copied: omafile does not
// run privileged, and a failed chown would abort a copy that otherwise succeeded.
void copyMetadata(const QString &source, const QString &target)
{
    struct stat info;
    if (::lstat(QFile::encodeName(source).constData(), &info) != 0)
        return;

    const QByteArray targetBytes = QFile::encodeName(target);
    ::chmod(targetBytes.constData(), info.st_mode & 07777);

    struct timespec times[2];
    times[0] = info.st_atim;
    times[1] = info.st_mtim;
    ::utimensat(AT_FDCWD, targetBytes.constData(), times, AT_SYMLINK_NOFOLLOW);
}

bool isSameFile(const QString &a, const QString &b)
{
    struct stat first;
    struct stat second;
    if (::lstat(QFile::encodeName(a).constData(), &first) != 0)
        return false;
    if (::lstat(QFile::encodeName(b).constData(), &second) != 0)
        return false;
    return first.st_dev == second.st_dev && first.st_ino == second.st_ino;
}

} // namespace

FileOps::FileOps(QObject *parent)
    : QObject(parent)
{
}

void FileOps::cancel()
{
    m_cancelled.store(true, std::memory_order_relaxed);
    // A worker parked on a conflict question must not stay parked.
    QMutexLocker lock(&m_conflictMutex);
    m_choice = Cancel;
    m_answered = true;
    m_conflictWait.wakeAll();
}

void FileOps::resolveConflict(Conflict choice, bool applyToAll)
{
    QMutexLocker lock(&m_conflictMutex);
    m_choice = choice;
    m_applyToAll = applyToAll;
    m_answered = true;
    m_conflictWait.wakeAll();
}

void FileOps::beginOperation()
{
    m_cancelled.store(false, std::memory_order_relaxed);
    QMutexLocker lock(&m_conflictMutex);
    m_applyToAll = false;
    m_answered = false;
    m_choice = Replace;
    m_bytesTotal = 0;
    m_bytesDone = 0;
}

FileOps::Conflict FileOps::askConflict(quint64 id, const QString &target,
                                       const QString &suggestion)
{
    QMutexLocker lock(&m_conflictMutex);
    if (m_applyToAll)
        return m_choice;
    if (cancelled())
        return Cancel;

    m_answered = false;
    lock.unlock();
    emit conflict(id, target, suggestion);
    lock.relock();

    while (!m_answered)
        m_conflictWait.wait(&m_conflictMutex);
    m_answered = false;

    return m_choice;
}

qint64 FileOps::treeSize(const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink())
        return 0;
    if (!info.isDir())
        return info.size();

    qint64 total = 0;
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
                                    | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo item = iterator.fileInfo();
        if (!item.isSymLink() && item.isFile())
            total += item.size();
    }
    return total;
}

// "report.txt" -> "report (2).txt", per §8's suggested form.
QString FileOps::suggestName(const QString &directory, const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString suffix = dot > 0 ? name.mid(dot) : QString();

    for (int counter = 2; counter < 10000; ++counter) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(counter).arg(suffix);
        if (!QFileInfo::exists(joinPath(directory, candidate)))
            return candidate;
    }
    return name;
}

bool FileOps::copyFileContents(const QString &source, const QString &target, quint64 id)
{
    const int in = ::open(QFile::encodeName(source).constData(), O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return false;

    struct stat info;
    if (::fstat(in, &info) != 0) {
        ::close(in);
        return false;
    }

    const int out = ::open(QFile::encodeName(target).constData(),
                           O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, info.st_mode & 07777);
    if (out < 0) {
        ::close(in);
        return false;
    }

    bool ok = true;
    off_t remaining = info.st_size;

    // copy_file_range lets the kernel (and a reflinking filesystem) avoid moving bytes
    // through userspace at all. It fails across some filesystem pairs, hence the fallback.
    while (remaining > 0 && !cancelled()) {
        const ssize_t written = ::copy_file_range(in, nullptr, out, nullptr,
                                                  size_t(remaining), 0);
        if (written <= 0)
            break;
        remaining -= written;
        m_bytesDone += written;
        if (m_bytesTotal > 0)
            emit progress(id, double(m_bytesDone) / double(m_bytesTotal),
                          QFileInfo(source).fileName());
    }

    if (remaining > 0 && !cancelled()) {
        // Fallback: plain read/write, which works everywhere including /proc-like sources.
        if (::lseek(in, info.st_size - remaining, SEEK_SET) < 0
            || ::lseek(out, info.st_size - remaining, SEEK_SET) < 0) {
            ok = false;
        } else {
            QByteArray buffer(64 * 1024, Qt::Uninitialized);
            while (remaining > 0 && !cancelled()) {
                const ssize_t got = ::read(in, buffer.data(), size_t(qMin<qint64>(buffer.size(),
                                                                                 remaining)));
                if (got < 0) {
                    ok = false;
                    break;
                }
                if (got == 0)
                    break;
                if (::write(out, buffer.constData(), size_t(got)) != got) {
                    ok = false;
                    break;
                }
                remaining -= got;
                m_bytesDone += got;
                if (m_bytesTotal > 0)
                    emit progress(id, double(m_bytesDone) / double(m_bytesTotal),
                                  QFileInfo(source).fileName());
            }
        }
    }

    ::close(in);
    ::close(out);

    if (!ok || cancelled()) {
        ::unlink(QFile::encodeName(target).constData());
        return false;
    }

    copyMetadata(source, target);
    return true;
}

bool FileOps::copyTree(quint64 id, const QString &source, const QString &target, QString *error)
{
    const QFileInfo info(source);

    // Symlinks are copied as links, never followed (§8) — following them would silently
    // duplicate whole trees and break relative links.
    if (info.isSymLink()) {
        // readlink, not QFileInfo::symLinkTarget: Qt resolves the target to an absolute
        // path, which would rewrite a relative link into one pointing back at the source
        // tree — so a copied directory's internal links would all still refer to the
        // original.
        QByteArray linkTarget(PATH_MAX, Qt::Uninitialized);
        const ssize_t length = ::readlink(QFile::encodeName(source).constData(),
                                          linkTarget.data(), size_t(linkTarget.size()));
        if (length < 0) {
            *error = errorString();
            return false;
        }
        linkTarget.resize(int(length));

        ::unlink(QFile::encodeName(target).constData());
        if (::symlink(linkTarget.constData(), QFile::encodeName(target).constData()) != 0) {
            *error = errorString();
            return false;
        }
        return true;
    }

    if (info.isDir()) {
        if (!QDir().mkpath(target)) {
            *error = QStringLiteral("could not create %1").arg(target);
            return false;
        }
        const QDir directory(source);
        const QFileInfoList children = directory.entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &child : children) {
            if (cancelled())
                return false;
            if (!copyTree(id, child.absoluteFilePath(), joinPath(target, child.fileName()), error))
                return false;
        }
        copyMetadata(source, target);
        return true;
    }

    if (!copyFileContents(source, target, id)) {
        *error = cancelled() ? QStringLiteral("cancelled") : errorString();
        return false;
    }
    return true;
}

QString FileOps::copyOne(quint64 id, const QString &source, const QString &destinationDir,
                         QString *error)
{
    const QFileInfo info(source);
    QString targetName = info.fileName();
    QString target = joinPath(destinationDir, targetName);

    // Copying something onto itself would otherwise truncate it to nothing.
    if (isSameFile(source, target))
        targetName = suggestName(destinationDir, targetName);
    target = joinPath(destinationDir, targetName);

    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
        const QString suggestion = suggestName(destinationDir, info.fileName());
        switch (askConflict(id, target, suggestion)) {
        case Skip:
            return {};
        case Cancel:
            *error = QStringLiteral("cancelled");
            return {};
        case Rename:
            target = joinPath(destinationDir, suggestion);
            break;
        case Replace:
            if (QFileInfo(target).isDir() && !QFileInfo(target).isSymLink())
                QDir(target).removeRecursively();
            else
                QFile::remove(target);
            break;
        }
    }

    if (!copyTree(id, source, target, error))
        return {};
    return target;
}

void FileOps::copy(const QStringList &sources, const QString &destinationDir, quint64 id)
{
    beginOperation();

    for (const QString &source : sources)
        m_bytesTotal += treeSize(source);

    JournalEntry entry;
    entry.kind = JournalEntry::Copied;
    QString error;

    for (const QString &source : sources) {
        if (cancelled()) {
            error = QStringLiteral("cancelled");
            break;
        }
        const QString written = copyOne(id, source, destinationDir, &error);
        if (!error.isEmpty())
            break;
        if (!written.isEmpty())
            entry.created.append(written);
    }

    if (!error.isEmpty() && error != QLatin1String("cancelled")) {
        emit failed(id, error);
        return;
    }

    entry.summary = QStringLiteral("Copied %1 to %2")
                        .arg(entry.created.size())
                        .arg(QFileInfo(destinationDir).fileName());
    if (entry.created.isEmpty())
        entry.kind = JournalEntry::None;
    emit finished(id, entry);
}

void FileOps::move(const QStringList &sources, const QString &destinationDir, quint64 id)
{
    beginOperation();

    JournalEntry entry;
    entry.kind = JournalEntry::Moved;
    QString error;
    int index = 0;

    for (const QString &source : sources) {
        if (cancelled())
            break;

        ++index;
        const QFileInfo info(source);
        QString target = joinPath(destinationDir, info.fileName());

        if (isSameFile(source, target))
            continue; // moving something onto itself is a no-op, not an error

        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
            const QString suggestion = suggestName(destinationDir, info.fileName());
            const Conflict choice = askConflict(id, target, suggestion);
            if (choice == Skip)
                continue;
            if (choice == Cancel)
                break;
            if (choice == Rename) {
                target = joinPath(destinationDir, suggestion);
            } else if (QFileInfo(target).isDir() && !QFileInfo(target).isSymLink()) {
                QDir(target).removeRecursively();
            } else {
                QFile::remove(target);
            }
        }

        emit progress(id, double(index) / double(sources.size()), info.fileName());

        // Same filesystem: rename(2), which is instant and atomic (§8). Across
        // filesystems there is no such thing, so it becomes copy-then-delete.
        if (::rename(QFile::encodeName(source).constData(),
                     QFile::encodeName(target).constData())
            == 0) {
            entry.moves.append({ source, target });
            continue;
        }
        if (errno != EXDEV) {
            error = errorString();
            break;
        }

        m_bytesTotal = treeSize(source);
        m_bytesDone = 0;
        if (!copyTree(id, source, target, &error))
            break;

        if (QFileInfo(source).isDir() && !QFileInfo(source).isSymLink())
            QDir(source).removeRecursively();
        else
            QFile::remove(source);
        entry.moves.append({ source, target });
    }

    if (!error.isEmpty() && error != QLatin1String("cancelled")) {
        emit failed(id, error);
        return;
    }

    entry.summary = QStringLiteral("Moved %1 to %2")
                        .arg(entry.moves.size())
                        .arg(QFileInfo(destinationDir).fileName());
    if (entry.moves.isEmpty())
        entry.kind = JournalEntry::None;
    emit finished(id, entry);
}

void FileOps::trash(const QStringList &paths, quint64 id)
{
    beginOperation();

    JournalEntry entry;
    entry.kind = JournalEntry::Trashed;
    int index = 0;

    for (const QString &path : paths) {
        if (cancelled())
            break;

        ++index;
        emit progress(id, double(index) / double(paths.size()), QFileInfo(path).fileName());

        Trash::Item item;
        QString error;
        if (!Trash::moveToTrash(path, &item, &error)) {
            emit failed(id, error);
            return;
        }
        entry.trashed.append(item);
    }

    entry.summary = entry.trashed.size() == 1
        ? QStringLiteral("Trashed %1").arg(QFileInfo(entry.trashed.first().originalPath).fileName())
        : QStringLiteral("Trashed %1 items").arg(entry.trashed.size());
    if (entry.trashed.isEmpty())
        entry.kind = JournalEntry::None;
    emit finished(id, entry);
}

void FileOps::removePermanently(const QStringList &paths, quint64 id)
{
    beginOperation();

    int index = 0;
    for (const QString &path : paths) {
        if (cancelled())
            break;

        ++index;
        emit progress(id, double(index) / double(paths.size()), QFileInfo(path).fileName());

        const QFileInfo info(path);
        const bool ok = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively()
                                                          : QFile::remove(path);
        if (!ok) {
            emit failed(id, QStringLiteral("could not delete %1").arg(info.fileName()));
            return;
        }
    }

    // Deliberately not journalled: §8 says permanent delete is not undoable, and the
    // confirm dialog promises exactly that.
    JournalEntry entry;
    entry.summary = index == 1
        ? QStringLiteral("Deleted 1 item permanently")
        : QStringLiteral("Deleted %1 items permanently").arg(index);
    emit finished(id, entry);
}

void FileOps::makeDirectory(const QString &parentDir, const QString &name, quint64 id)
{
    beginOperation();

    const QString target = joinPath(parentDir, name);
    if (QFileInfo::exists(target)) {
        emit failed(id, QStringLiteral("%1 already exists").arg(name));
        return;
    }
    if (!QDir().mkdir(target)) {
        emit failed(id, QStringLiteral("could not create %1").arg(name));
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::Created;
    entry.created.append(target);
    entry.summary = QStringLiteral("Created %1").arg(name);
    emit finished(id, entry);
}

void FileOps::makeFile(const QString &parentDir, const QString &name, quint64 id)
{
    beginOperation();

    const QString target = joinPath(parentDir, name);
    if (QFileInfo::exists(target)) {
        emit failed(id, QStringLiteral("%1 already exists").arg(name));
        return;
    }

    // O_EXCL so a file that appeared between the check and here is never truncated.
    const int fd = ::open(QFile::encodeName(target).constData(),
                          O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        emit failed(id, errorString());
        return;
    }
    ::close(fd);

    JournalEntry entry;
    entry.kind = JournalEntry::Created;
    entry.created.append(target);
    entry.summary = QStringLiteral("Created %1").arg(name);
    emit finished(id, entry);
}

void FileOps::renameEntry(const QString &path, const QString &newName, quint64 id)
{
    beginOperation();

    const QFileInfo info(path);
    if (newName.isEmpty() || newName.contains(QLatin1Char('/'))) {
        emit failed(id, QStringLiteral("invalid name"));
        return;
    }

    const QString target = joinPath(info.absolutePath(), newName);
    // A case-only rename on a case-insensitive filesystem hits this check wrongly, but
    // rename(2) handles it correctly, so only reject a genuinely different existing file.
    if (QFileInfo::exists(target) && !isSameFile(path, target)) {
        emit failed(id, QStringLiteral("%1 already exists").arg(newName));
        return;
    }

    if (::rename(QFile::encodeName(path).constData(), QFile::encodeName(target).constData())
        != 0) {
        emit failed(id, errorString());
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::Renamed;
    entry.moves.append({ path, target });
    entry.summary = QStringLiteral("Renamed to %1").arg(newName);
    emit finished(id, entry);
}

void FileOps::bulkRename(const QString &directory, const QStringList &originals,
                         const QStringList &edited, quint64 id)
{
    beginOperation();

    const BulkRename::Plan plan = BulkRename::plan(originals, edited);
    if (!plan.ok) {
        emit failed(id, plan.error);
        return;
    }
    if (plan.steps.isEmpty()) {
        emit finished(id, JournalEntry {});
        return;
    }

    // Applied in order, rolling the whole thing back on the first failure — a bulk
    // rename that half-succeeded is worse than one that did not run (§9).
    QList<QPair<QString, QString>> done;
    for (const auto &step : plan.steps) {
        const QString from = joinPath(directory, step.first);
        const QString to = joinPath(directory, step.second);

        if (QFileInfo::exists(to) || ::rename(QFile::encodeName(from).constData(),
                                              QFile::encodeName(to).constData()) != 0) {
            const QString reason = QFileInfo::exists(to)
                ? QStringLiteral("\"%1\" already exists").arg(step.second)
                : errorString();

            for (int i = int(done.size()) - 1; i >= 0; --i) {
                ::rename(QFile::encodeName(done.at(i).second).constData(),
                         QFile::encodeName(done.at(i).first).constData());
            }
            emit failed(id, QStringLiteral("%1 — nothing was renamed").arg(reason));
            return;
        }
        done.append({ from, to });
    }

    // The journal records the user's intent, not the temporary hops a cycle needed, so
    // one Ctrl+Z puts every name back.
    JournalEntry entry;
    entry.kind = JournalEntry::Renamed;
    for (int i = 0; i < originals.size(); ++i) {
        if (originals.at(i) != edited.at(i)) {
            entry.moves.append({ joinPath(directory, originals.at(i)),
                                 joinPath(directory, edited.at(i)) });
        }
    }
    entry.summary = plan.changed == 1 ? QStringLiteral("Renamed 1 file")
                                      : QStringLiteral("Renamed %1 files").arg(plan.changed);
    emit finished(id, entry);
}

void FileOps::undo(const JournalEntry &entry, quint64 id)
{
    beginOperation();

    JournalEntry result; // undoing is not itself undoable
    QString error;

    switch (entry.kind) {
    case JournalEntry::Trashed:
        for (const Trash::Item &item : entry.trashed) {
            if (!Trash::restore(item, &error)) {
                emit failed(id, error);
                return;
            }
        }
        result.summary = entry.trashed.size() == 1
            ? QStringLiteral("Restored 1 item")
            : QStringLiteral("Restored %1 items").arg(entry.trashed.size());
        break;

    case JournalEntry::Moved:
    case JournalEntry::Renamed:
        // Reverse order, so a sequence that moved a directory then its contents unwinds
        // in the order that keeps every intermediate state valid.
        for (int i = int(entry.moves.size()) - 1; i >= 0; --i) {
            const auto &pair = entry.moves.at(i);
            QDir().mkpath(QFileInfo(pair.first).absolutePath());
            if (::rename(QFile::encodeName(pair.second).constData(),
                         QFile::encodeName(pair.first).constData())
                != 0) {
                emit failed(id, errorString());
                return;
            }
        }
        result.summary = entry.kind == JournalEntry::Renamed
            ? QStringLiteral("Renamed back")
            : QStringLiteral("Moved %1 back").arg(entry.moves.size());
        break;

    case JournalEntry::Copied:
    case JournalEntry::Created:
        // Undoing a copy trashes the copies rather than deleting them: undo must never
        // be the destructive operation.
        for (const QString &path : entry.created) {
            Trash::Item item;
            if (!QFileInfo::exists(path))
                continue;
            if (!Trash::moveToTrash(path, &item, &error)) {
                emit failed(id, error);
                return;
            }
        }
        result.summary = entry.created.size() == 1
            ? QStringLiteral("Undid 1 item")
            : QStringLiteral("Undid %1 items").arg(entry.created.size());
        break;

    case JournalEntry::None:
        break;
    }

    emit finished(id, result);
}
