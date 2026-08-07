#include "fileops.h"

#include "bulkrename.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <climits>
#include <unistd.h>

namespace {

// How much written-but-not-yet-durable data a copy is allowed to run ahead by.
//
// This is the whole of the honesty fix. write() and copy_file_range() return as soon as
// the bytes are in the page cache, so with no bound the bar reaches 100% at RAM speed and
// the operation then reports itself finished while the drive is still draining — minutes,
// on a USB stick. Measured on this machine: writing a 3 GB file to btrfs on NVMe left
// 45 MB still unwritten at the moment write() claimed the whole file was done, and the
// "speed" that implies (2.8 GB/s) is not a rate any device here actually sustains.
//
// 64 MB is picked so a fast device never blocks on the throttle at all — an NVMe drains
// that in well under the time it takes to refill it — while a slow one is held to within
// 64 MB of the truth rather than gigabytes. It doubles as the flush budget across files,
// so the overshoot cannot accumulate over a copy of many of them, and whatever is left at
// the end is waited for by flushFilesystemAt() before the operation reports itself done.
constexpr off_t kWritebackWindow = 64 * 1024 * 1024;

// The throttle only gets a say between chunks, so a chunk has to be small relative to the
// window. 8 MB is large enough that the per-call overhead is irrelevant.
constexpr off_t kCopyChunk = 8 * 1024 * 1024;

// Rates are measured over at least this long. Chunk-to-chunk timings swing through orders
// of magnitude and are unreadable; this plus the smoothing below gives a number that
// settles without lagging a real change in speed.
//
// 250 ms rather than something longer because the progress overlay appears at 300 ms
// (§4's "no spinner for under 300 ms of work"): a window wider than that would show the
// panel with the rate line still empty, which reads as a stalled transfer.
constexpr qint64 kSpeedWindowMs = 250;

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

// Waits for everything this operation wrote to reach the device before it is called done.
//
// This is the difference the complaint was actually about: without it, "finished" means
// "the page cache accepted the last byte", the overlay closes, and the drive keeps writing
// for as long as it needs — which on a stick is minutes, with nothing on screen saying so
// and an eject that would truncate the copy. syncfs rather than a per-file fsync because
// by this point the files are closed, and one call covers whatever the budget above left
// outstanding.
void flushFilesystemAt(const QString &directory)
{
    const int fd = ::open(QFile::encodeName(directory).constData(),
                          O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return;
    ::syncfs(fd);
    ::close(fd);
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

// Is `directory` the same as `source`, or somewhere inside it?
//
// Copying a directory into its own descendant walks a tree that it is simultaneously
// growing: each level's children are listed *after* the level below has been created, so
// the new copy shows up in the next listing and is copied again. It stops only at
// PATH_MAX, roughly a thousand levels down, having rewritten every file in the source
// several hundred times — enough to fill a disk with a folder of any size.
//
// The check is on canonical paths so a symlink or a `..` cannot be used to slip past it.
// An empty canonical path means the entry does not exist, which is not this function's
// problem to report: say no and let the copy fail on its own terms.
bool destinationIsInsideSource(const QString &source, const QString &directory)
{
    const QString from = QFileInfo(source).canonicalFilePath();
    const QString into = QFileInfo(directory).canonicalFilePath();
    if (from.isEmpty() || into.isEmpty())
        return false;

    // The separator matters: without it, /home/a would swallow /home/abc.
    return into == from || into.startsWith(from + QLatin1Char('/'));
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
    restartByteAccounting(0);
    m_speed = 0.0;
    m_unsyncedBytes = 0;
}

// Byte counter and rate mark together: they are two halves of one measurement, and moving
// one without the other yields a rate computed against a count that no longer exists.
void FileOps::restartByteAccounting(qint64 total)
{
    m_bytesTotal = total;
    m_bytesDone = 0;
    m_speedMarkBytes = 0;
    m_speedMarkMs = 0;
    m_speedClock.start();
}

// An exponential moving average. Sampling every chunk instead would report the page
// cache's speed on one chunk and the drive's on the next; averaging over the whole
// operation would take minutes to notice a drive that had slowed down.
//
// Pure and separate from the clock so the blending is testable without a copy slow enough
// to sample — the arithmetic is the part that can be wrong, and on any real filesystem a
// test fixture finishes long inside one window.
double FileOps::blendRate(double previous, qint64 bytes, qint64 ms)
{
    if (ms <= 0)
        return previous;
    // The counter restarts mid-operation on a cross-filesystem move, so a sample can span
    // a reset and come out negative. Nothing moved backwards; treat it as no progress.
    const double instant = double(qMax<qint64>(0, bytes)) * 1000.0 / double(ms);
    // Seed with the first real sample rather than easing up from zero, which would spend
    // the opening seconds of every copy showing a rate far below the truth.
    return previous <= 0.0 ? instant : 0.6 * previous + 0.4 * instant;
}

double FileOps::measureSpeed()
{
    const qint64 elapsed = m_speedClock.elapsed();
    const qint64 sinceMark = elapsed - m_speedMarkMs;
    if (sinceMark < kSpeedWindowMs)
        return m_speed;

    m_speed = blendRate(m_speed, m_bytesDone - m_speedMarkBytes, sinceMark);
    m_speedMarkBytes = m_bytesDone;
    m_speedMarkMs = elapsed;
    return m_speed;
}

void FileOps::reportBytes(quint64 id, const QString &name)
{
    if (m_bytesTotal <= 0)
        return;
    emit progress(id, double(m_bytesDone) / double(m_bytesTotal), name, measureSpeed());
}

// The last stretch, and the one the complaint was about. Waiting here silently would only
// trade "the overlay closed too early" for "the overlay sat at 100% doing nothing", so the
// phase says what it is — and goes indeterminate, because how long a drive needs to drain
// its own cache is not something that can be known from this side.
void FileOps::flushAndReport(quint64 id, const QString &destinationDir)
{
    // Announced only when there were bytes to move — a copy of empty files has nothing to
    // say. The flush itself is *not* conditional: move() calls this before deleting the
    // original, and that guarantee must not depend on how large the file happened to be.
    if (m_bytesTotal > 0)
        emit progress(id, -1.0, QStringLiteral("Flushing to drive…"), 0.0);
    flushFilesystemAt(destinationDir);
    m_unsyncedBytes = 0;
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
    const QString name = QFileInfo(source).fileName();

    // How far writeback has been waited for. Everything between this and the write offset
    // is in the page cache and not yet on the device.
    off_t synced = 0;
    const auto throttle = [&](off_t writtenTo) {
        if (writtenTo > synced)
            ::sync_file_range(out, synced, writtenTo - synced, SYNC_FILE_RANGE_WRITE);
        // Only wait once more than a window's worth is outstanding, so a fast device is
        // never actually held up here — it drains faster than this loop can refill it.
        const off_t outstanding = writtenTo - synced;
        if (outstanding <= kWritebackWindow)
            return;
        const off_t slice = outstanding - kWritebackWindow;
        if (::sync_file_range(out, synced, slice, SYNC_FILE_RANGE_WAIT_BEFORE) == 0)
            synced += slice;
        else
            synced = writtenTo; // unsupported here; the fsync below still covers it
    };

    // copy_file_range lets the kernel (and a reflinking filesystem) avoid moving bytes
    // through userspace at all. It fails across some filesystem pairs, hence the fallback.
    while (remaining > 0 && !cancelled()) {
        // Capped rather than handed the whole file: the throttle below can only bound
        // what has been written when it gets a say between chunks.
        const ssize_t written = ::copy_file_range(in, nullptr, out, nullptr,
                                                  size_t(qMin<off_t>(remaining, kCopyChunk)), 0);
        if (written <= 0)
            break;
        remaining -= written;
        m_bytesDone += written;
        throttle(info.st_size - remaining);
        reportBytes(id, name);
    }

    if (remaining > 0 && !cancelled()) {
        // Fallback: plain read/write, which works everywhere including /proc-like sources.
        if (::lseek(in, info.st_size - remaining, SEEK_SET) < 0
            || ::lseek(out, info.st_size - remaining, SEEK_SET) < 0) {
            ok = false;
        } else {
            QByteArray buffer(1024 * 1024, Qt::Uninitialized);
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
                throttle(info.st_size - remaining);
                reportBytes(id, name);
            }
        }
    }

    // The tail the window deliberately left outstanding, plus whatever earlier small files
    // left behind. Deliberately *not* an unconditional fsync per file: on a slow drive
    // each one is a round trip and a metadata commit, so a folder of ten thousand small
    // files would pay ten thousand of them — far worse than the stall being fixed.
    // Charging every file against one shared budget instead makes a large file flush on
    // its own while tiny ones flush once between them.
    if (ok && !cancelled()) {
        m_unsyncedBytes += qMax<off_t>(0, info.st_size - synced);
        if (m_unsyncedBytes >= kWritebackWindow) {
            ::fsync(out);
            m_unsyncedBytes = 0;
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

    // Refused before anything is written, not part-way through: by the time the recursion
    // is visible on disk there are already hundreds of gigabytes of it.
    for (const QString &source : sources) {
        if (destinationIsInsideSource(source, destinationDir)) {
            emit failed(id, QStringLiteral("cannot copy %1 into itself")
                                .arg(QFileInfo(source).fileName()));
            return;
        }
    }

    qint64 total = 0;
    for (const QString &source : sources)
        total += treeSize(source);
    // The rate is measured from here rather than from beginOperation(): walking a large
    // tree to size it takes real time, and counting it as time spent copying would report
    // the first seconds of every big copy as far slower than the drive is really going.
    restartByteAccounting(total);

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

    // Before finished(), never after: the overlay closes on that signal, so anything still
    // in flight afterwards is a transfer the user has been told is over.
    flushAndReport(id, destinationDir);

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

    // rename(2) refuses this itself with EINVAL, but a move across filesystems is a copy
    // followed by a delete and would hit the same runaway recursion the copy does.
    for (const QString &source : sources) {
        if (destinationIsInsideSource(source, destinationDir)) {
            emit failed(id, QStringLiteral("cannot move %1 into itself")
                                .arg(QFileInfo(source).fileName()));
            return;
        }
    }

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

        emit progress(id, double(index) / double(sources.size()), info.fileName(), 0.0);

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

        // Each cross-filesystem source is measured on its own, so the byte counter starts
        // again here — and the rate has to start again with it, or the next sample reads a
        // negative delta against a mark from the previous, larger, count.
        restartByteAccounting(treeSize(source));
        if (!copyTree(id, source, target, &error))
            break;

        // Before the original is removed, not merely before the operation is reported
        // done. A cross-filesystem move is a copy followed by a delete, so deleting while
        // the copy is still only in the page cache is the one case here where the stall
        // is a data-loss risk rather than a cosmetic one.
        flushAndReport(id, destinationDir);

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
        emit progress(id, double(index) / double(paths.size()), QFileInfo(path).fileName(),
                      0.0);

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
        emit progress(id, double(index) / double(paths.size()), QFileInfo(path).fileName(),
                      0.0);

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

// Counting entries up front is what turns an indeterminate spinner into a real progress
// bar: bsdtar prints one "a <path>" line per entry it writes, so the two can be matched.
static int countEntries(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isDir() || info.isSymLink())
        return 1;

    int total = 1; // the directory itself is an entry in the archive
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
                                    | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++total;
    }
    return total;
}

void FileOps::compress(const QStringList &sources, const QString &destinationDir,
                       const QString &archiveName, quint64 id)
{
    beginOperation();

    if (sources.isEmpty() || destinationDir.isEmpty() || archiveName.isEmpty()) {
        emit failed(id, QStringLiteral("nothing to compress"));
        return;
    }

    // libarchive, which is a dependency of pacman itself, so this is not a soft dependency
    // on Arch — but say so rather than failing silently if it is somehow missing.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (program.isEmpty()) {
        emit failed(id, QStringLiteral("bsdtar is not installed (libarchive)"));
        return;
    }

    // Everything comes from one listing, so one -C covers the lot and the archive holds
    // relative names rather than the absolute paths of this machine.
    const QString parent = QFileInfo(sources.first()).absolutePath();
    QStringList names;
    for (const QString &source : sources) {
        const QFileInfo info(source);
        if (info.absolutePath() != parent) {
            emit failed(id, QStringLiteral("can only compress items from one folder"));
            return;
        }
        names.append(info.fileName());
    }

    // suggestName always steps to "(2)" — it exists to resolve a collision, not to name
    // the first one. Only reach for it when the plain name is actually taken.
    QString target = joinPath(destinationDir, archiveName);
    if (QFileInfo::exists(target))
        target = joinPath(destinationDir, suggestName(destinationDir, archiveName));

    int expected = 0;
    for (const QString &source : sources)
        expected += countEntries(source);

    QProcess process;
    process.setProgram(program);
    // -a: the format comes from the extension. -C: names stay relative to their folder.
    process.setArguments(QStringList{ QStringLiteral("-a"), QStringLiteral("-c"),
                                      QStringLiteral("-v"), QStringLiteral("-f"), target,
                                      QStringLiteral("-C"), parent } + names);
    // bsdtar writes its per-entry lines *and* its errors to stderr, and waitForReadyRead
    // watches the current read channel — which defaults to stdout. Left alone, the wait
    // times out on every pass and nothing is read until the process has already exited,
    // so the bar sits empty for the whole operation and then jumps to done.
    process.setReadChannel(QProcess::StandardError);
    process.start();

    if (!process.waitForStarted(5000)) {
        emit failed(id, QStringLiteral("could not run bsdtar"));
        return;
    }

    // bsdtar writes one "a <path>" line per entry to stderr, and its errors there too.
    int done = 0;
    QByteArray diagnostics;
    while (process.state() != QProcess::NotRunning) {
        if (cancelled()) {
            process.kill();
            process.waitForFinished(3000);
            // A half-written archive is not a result anybody wants left behind.
            QFile::remove(target);
            emit failed(id, QStringLiteral("cancelled"));
            return;
        }

        if (!process.waitForReadyRead(200))
            continue;

        const QByteArray chunk = process.readAllStandardError();
        diagnostics.append(chunk);
        for (const QByteArray &line : chunk.split('\n')) {
            if (!line.startsWith("a "))
                continue;
            ++done;
            if (expected > 0) {
                emit progress(id, qMin(1.0, double(done) / double(expected)),
                              QString::fromLocal8Bit(line.mid(2)), 0.0);
            }
        }
    }
    process.waitForFinished(3000);
    diagnostics.append(process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QFile::remove(target);
        // bsdtar names the file it choked on; that line is the useful half of its output.
        QString reason;
        for (const QByteArray &line : diagnostics.split('\n')) {
            if (line.startsWith("bsdtar: ")) {
                reason = QString::fromLocal8Bit(line.mid(8)).trimmed();
                break;
            }
        }
        emit failed(id, reason.isEmpty() ? QStringLiteral("could not create the archive")
                                         : reason);
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::Created;
    entry.created.append(target);
    entry.summary = QStringLiteral("Compressed %1 to %2")
                        .arg(sources.size())
                        .arg(QFileInfo(target).fileName());
    emit finished(id, entry);
}

// "photos.tar.gz" -> "photos". The two-part suffixes have to come off together or the
// folder ends up called "photos.tar".
static QString archiveStem(const QString &fileName)
{
    static const QStringList doubles = { QStringLiteral(".tar.gz"),  QStringLiteral(".tar.bz2"),
                                         QStringLiteral(".tar.xz"),  QStringLiteral(".tar.zst"),
                                         QStringLiteral(".tar.lz"),  QStringLiteral(".tar.lzma") };
    for (const QString &suffix : doubles) {
        if (fileName.endsWith(suffix, Qt::CaseInsensitive))
            return fileName.left(fileName.size() - suffix.size());
    }
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? fileName.left(dot) : fileName;
}

void FileOps::extract(const QString &archivePath, const QString &destinationDir, quint64 id)
{
    beginOperation();

    if (archivePath.isEmpty() || destinationDir.isEmpty()) {
        emit failed(id, QStringLiteral("nothing to extract"));
        return;
    }
    if (!QFileInfo::exists(archivePath)) {
        emit failed(id, QStringLiteral("%1 does not exist")
                            .arg(QFileInfo(archivePath).fileName()));
        return;
    }

    const QString program = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (program.isEmpty()) {
        emit failed(id, QStringLiteral("bsdtar is not installed (libarchive)"));
        return;
    }

    // Its own folder, named after the archive, disambiguated like a copy would be.
    const QString wanted = archiveStem(QFileInfo(archivePath).fileName());
    QString target = joinPath(destinationDir, wanted);
    if (QFileInfo::exists(target))
        target = joinPath(destinationDir, suggestName(destinationDir, wanted));

    if (!QDir().mkpath(target)) {
        emit failed(id, QStringLiteral("could not create %1").arg(QFileInfo(target).fileName()));
        return;
    }

    QProcess process;
    process.setProgram(program);
    // No -P: bsdtar's default refuses entries containing ".." and strips a leading "/",
    // so a hostile archive cannot write outside the folder made for it. Verified against
    // a crafted archive rather than assumed.
    process.setArguments({ QStringLiteral("-x"), QStringLiteral("-v"),
                           QStringLiteral("-f"), archivePath,
                           QStringLiteral("-C"), target });
    process.setReadChannel(QProcess::StandardError);
    process.start();

    if (!process.waitForStarted(5000)) {
        QDir(target).removeRecursively();
        emit failed(id, QStringLiteral("could not run bsdtar"));
        return;
    }

    // How many entries an archive holds cannot be known without reading it, and reading
    // it costs about as much as extracting it on a solid format — so the bar is
    // indeterminate (-1) and the names carry the information instead.
    QByteArray diagnostics;
    while (process.state() != QProcess::NotRunning) {
        if (cancelled()) {
            process.kill();
            process.waitForFinished(3000);
            QDir(target).removeRecursively();
            emit failed(id, QStringLiteral("cancelled"));
            return;
        }
        if (!process.waitForReadyRead(200))
            continue;

        const QByteArray chunk = process.readAllStandardError();
        diagnostics.append(chunk);
        for (const QByteArray &line : chunk.split('\n')) {
            if (line.startsWith("x "))
                emit progress(id, -1.0, QString::fromLocal8Bit(line.mid(2)), 0.0);
        }
    }
    process.waitForFinished(3000);
    diagnostics.append(process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        // Half an archive is not a result: take the folder with it so there is nothing
        // to mistake for a finished extraction.
        QDir(target).removeRecursively();
        QString reason;
        for (const QByteArray &line : diagnostics.split('\n')) {
            if (line.startsWith("bsdtar: ")) {
                reason = QString::fromLocal8Bit(line.mid(8)).trimmed();
                break;
            }
        }
        emit failed(id, reason.isEmpty() ? QStringLiteral("could not extract the archive")
                                         : reason);
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::Created;
    entry.created.append(target);
    entry.summary = QStringLiteral("Extracted %1").arg(QFileInfo(archivePath).fileName());
    emit finished(id, entry);
}

// rwxr-xr-x, the way ls writes it — the form people actually read permissions in.
static QString modeString(mode_t mode)
{
    static const char *rwx[] = { "---", "--x", "-w-", "-wx",
                                 "r--", "r-x", "rw-", "rwx" };
    QString out;
    if (S_ISDIR(mode))       out += QLatin1Char('d');
    else if (S_ISLNK(mode))  out += QLatin1Char('l');
    else if (S_ISCHR(mode))  out += QLatin1Char('c');
    else if (S_ISBLK(mode))  out += QLatin1Char('b');
    else if (S_ISFIFO(mode)) out += QLatin1Char('p');
    else if (S_ISSOCK(mode)) out += QLatin1Char('s');
    else                     out += QLatin1Char('-');

    out += QLatin1String(rwx[(mode >> 6) & 7]);
    out += QLatin1String(rwx[(mode >> 3) & 7]);
    out += QLatin1String(rwx[mode & 7]);
    return out;
}

void FileOps::describe(const QString &path, quint64 id)
{
    // No beginOperation(): this answers a question, it does not change anything, and
    // marking the window busy for a stat would put a progress bar over a panel.
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
        emit failed(id, errorString());
        return;
    }

    QVariantMap details;
    const QFileInfo fileInfo(path);
    details.insert(QStringLiteral("name"), fileInfo.fileName());
    details.insert(QStringLiteral("path"), path);
    details.insert(QStringLiteral("size"), qint64(info.st_size));
    details.insert(QStringLiteral("mode"), modeString(info.st_mode));
    details.insert(QStringLiteral("octal"),
                   QStringLiteral("%1").arg(info.st_mode & 07777, 4, 8, QLatin1Char('0')));
    details.insert(QStringLiteral("isDir"), bool(S_ISDIR(info.st_mode)));
    details.insert(QStringLiteral("isLink"), bool(S_ISLNK(info.st_mode)));
    details.insert(QStringLiteral("executable"), bool(info.st_mode & S_IXUSR));
    details.insert(QStringLiteral("writable"), bool(info.st_mode & S_IWUSR));
    details.insert(QStringLiteral("modified"),
                   QDateTime::fromSecsSinceEpoch(info.st_mtim.tv_sec)
                       .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    // Names where the system can supply them, numbers where it cannot — an id with no
    // passwd entry is still the honest answer.
    if (const passwd *owner = ::getpwuid(info.st_uid))
        details.insert(QStringLiteral("owner"), QString::fromLocal8Bit(owner->pw_name));
    else
        details.insert(QStringLiteral("owner"), QString::number(info.st_uid));
    if (const group *grp = ::getgrgid(info.st_gid))
        details.insert(QStringLiteral("group"), QString::fromLocal8Bit(grp->gr_name));
    else
        details.insert(QStringLiteral("group"), QString::number(info.st_gid));

    if (S_ISLNK(info.st_mode)) {
        QByteArray target(PATH_MAX, Qt::Uninitialized);
        const ssize_t length = ::readlink(QFile::encodeName(path).constData(),
                                          target.data(), size_t(target.size()));
        if (length > 0) {
            target.resize(int(length));
            details.insert(QStringLiteral("linkTarget"), QFile::decodeName(target));
        }
    }

    emit described(id, details);
}

// The executable bit follows the read bits: a file the group can read becomes one the
// group can run, and a private one stays private. Blindly setting 0755 would quietly
// publish something that was deliberately 0600.
void FileOps::setExecutable(const QString &path, bool executable, quint64 id)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
        emit failed(id, errorString());
        return;
    }

    mode_t mode = info.st_mode & 07777;
    if (executable) {
        if (mode & S_IRUSR) mode |= S_IXUSR;
        if (mode & S_IRGRP) mode |= S_IXGRP;
        if (mode & S_IROTH) mode |= S_IXOTH;
    } else {
        mode &= ~mode_t(S_IXUSR | S_IXGRP | S_IXOTH);
    }

    if (::chmod(QFile::encodeName(path).constData(), mode) != 0) {
        emit failed(id, errorString());
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::None; // a mode change is not undoable by the journal
    entry.summary = executable ? QStringLiteral("%1 is now executable")
                                     .arg(QFileInfo(path).fileName())
                               : QStringLiteral("%1 is no longer executable")
                                     .arg(QFileInfo(path).fileName());
    emit finished(id, entry);
}

void FileOps::setWritable(const QString &path, bool writable, quint64 id)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
        emit failed(id, errorString());
        return;
    }

    mode_t mode = info.st_mode & 07777;
    if (writable)
        mode |= S_IWUSR;
    else
        mode &= ~mode_t(S_IWUSR | S_IWGRP | S_IWOTH);

    if (::chmod(QFile::encodeName(path).constData(), mode) != 0) {
        emit failed(id, errorString());
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::None;
    entry.summary = writable ? QStringLiteral("%1 is now writable")
                                   .arg(QFileInfo(path).fileName())
                             : QStringLiteral("%1 is now read-only")
                                   .arg(QFileInfo(path).fileName());
    emit finished(id, entry);
}

void FileOps::makeSymlink(const QString &targetPath, const QString &destinationDir,
                          const QString &linkName, quint64 id)
{
    beginOperation();

    if (targetPath.isEmpty() || destinationDir.isEmpty() || linkName.isEmpty()) {
        emit failed(id, QStringLiteral("nothing to link to"));
        return;
    }

    QString link = joinPath(destinationDir, linkName);
    if (QFileInfo::exists(link) || QFileInfo(link).isSymLink())
        link = joinPath(destinationDir, suggestName(destinationDir, linkName));

    // An absolute target, so the link keeps working when it is moved elsewhere. Copying
    // a link preserves whatever it already said (see copyTree); creating one has no such
    // history to respect, and absolute is the answer that surprises least.
    if (::symlink(QFile::encodeName(targetPath).constData(),
                  QFile::encodeName(link).constData()) != 0) {
        emit failed(id, errorString());
        return;
    }

    JournalEntry entry;
    entry.kind = JournalEntry::Created;
    entry.created.append(link);
    entry.summary = QStringLiteral("Linked %1").arg(QFileInfo(link).fileName());
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
