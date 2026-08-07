#pragma once

#include "journal.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QVariantMap>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>

// Copy, move, trash, delete, mkdir and rename, all on a worker thread (§8).
//
// Conflicts are resolved by blocking the worker on a condition variable while the GUI
// asks the question. That is the only way to honor "Rename" and "Apply to all remaining"
// without either pre-scanning (which cannot see conflicts that appear mid-operation) or
// interrogating per file (which §8 explicitly rejects).
class FileOps : public QObject
{
    Q_OBJECT

public:
    enum Conflict {
        Replace,
        Skip,
        Rename, // use the suggested "name (2).ext"
        Cancel,
    };
    Q_ENUM(Conflict)

    explicit FileOps(QObject *parent = nullptr);

    // Callable from the GUI thread while an operation runs.
    void cancel();
    void resolveConflict(Conflict choice, bool applyToAll);

public slots:
    void copy(const QStringList &sources, const QString &destinationDir, quint64 id);
    void move(const QStringList &sources, const QString &destinationDir, quint64 id);
    void trash(const QStringList &paths, quint64 id);
    void removePermanently(const QStringList &paths, quint64 id);
    void makeDirectory(const QString &parentDir, const QString &name, quint64 id);
    void makeFile(const QString &parentDir, const QString &name, quint64 id);
    void renameEntry(const QString &path, const QString &newName, quint64 id);

    // Pack `sources` into `destinationDir/archiveName`. The extension chooses the format,
    // because that is what libarchive's -a does and inventing a second way to say it
    // would only be a way to disagree with the filename. Nothing is deleted: an archive
    // is a copy, and the originals are still there afterwards.
    void compress(const QStringList &sources, const QString &destinationDir,
                  const QString &archiveName, quint64 id);

    // Unpack `archivePath` into a new folder named after it. Always a folder, never
    // loose into the current directory: an archive with no single root would otherwise
    // strew its contents across whatever you were looking at, and that is not undoable
    // by eye. The archive itself is left alone.
    void extract(const QString &archivePath, const QString &destinationDir, quint64 id);

    // What one entry actually is: mode, owner, exact byte count, times, link target.
    // On the worker like everything else — a single stat is cheap locally and can hang
    // for the length of a timeout on a mount whose server has gone away.
    void describe(const QString &path, quint64 id);

    // Set the executable or writable bits, following what the read bits already say so a
    // group-readable file becomes group-executable and a private one does not.
    void setExecutable(const QString &path, bool executable, quint64 id);
    void setWritable(const QString &path, bool writable, quint64 id);

    // A symlink at `destinationDir/linkName` pointing at `targetPath`.
    void makeSymlink(const QString &targetPath, const QString &destinationDir,
                     const QString &linkName, quint64 id);
    // §9: the whole edit lands as *one* undoable operation, or none of it does.
    void bulkRename(const QString &directory, const QStringList &originals,
                    const QStringList &edited, quint64 id);
    void undo(const JournalEntry &entry, quint64 id);

signals:
    // A fraction rather than a count, because copies measure bytes and everything else
    // measures items, and the status bar only ever draws a thin line.
    //
    // `bytesPerSecond` is 0 for the operations that count items rather than bytes — there
    // is no honest rate to show for "renamed 4 of 9" — and the UI hides it when it is.
    void progress(quint64 id, double fraction, const QString &currentName,
                  double bytesPerSecond);
    void conflict(quint64 id, const QString &targetPath, const QString &suggestedName);
    void finished(quint64 id, const JournalEntry &journal);
    void failed(quint64 id, const QString &message);
    // describe()'s answer: a map because the UI only ever formats it into a panel.
    void described(quint64 id, const QVariantMap &details);

private:
    // Blocks until the GUI answers. Returns Cancel if the operation was cancelled.
    Conflict askConflict(quint64 id, const QString &target, const QString &suggestion);
    void beginOperation();
    bool cancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

    // Returns the path actually written, or empty when skipped or failed.
    QString copyOne(quint64 id, const QString &source, const QString &destinationDir,
                    QString *error);
    bool copyFileContents(const QString &source, const QString &target, quint64 id);
    bool copyTree(quint64 id, const QString &source, const QString &target, QString *error);

    static qint64 treeSize(const QString &path);
    static QString suggestName(const QString &directory, const QString &name);

public:
    // The rate smoothing, pure so it can be pinned without a copy slow enough to sample.
    // `bytes` may be negative when a sample spans a counter reset; that is not a rate.
    static double blendRate(double previous, qint64 bytes, qint64 ms);

private:

    std::atomic<bool> m_cancelled { false };

    QMutex m_conflictMutex;
    QWaitCondition m_conflictWait;
    Conflict m_choice = Replace;
    bool m_applyToAll = false;
    bool m_answered = false;

    // Byte accounting for the current copy, so progress is smooth across many files.
    qint64 m_bytesTotal = 0;
    qint64 m_bytesDone = 0;

    // Emits progress for a byte-counting operation, with the rate attached. Every copy
    // path goes through here so the smoothing cannot be forgotten at one of them.
    void reportBytes(quint64 id, const QString &name);
    // Resets the byte counter and the rate mark together — see the comment at the definition.
    void restartByteAccounting(qint64 total);
    // Waits for the destination to actually take the data, announcing the wait. Called
    // before finished(), and on a cross-filesystem move before the original is removed.
    void flushAndReport(quint64 id, const QString &destinationDir);
    // A rate measured over a window rather than per chunk: chunk times are far too jumpy
    // to read, and an unsmoothed number flickers through two orders of magnitude.
    double measureSpeed();

    // Bytes written but not yet known to be on the device, charged against one shared
    // budget so a pile of small files does not pay an fsync each.
    qint64 m_unsyncedBytes = 0;

    QElapsedTimer m_speedClock;
    qint64 m_speedMarkBytes = 0;
    qint64 m_speedMarkMs = 0;
    double m_speed = 0.0;
};
