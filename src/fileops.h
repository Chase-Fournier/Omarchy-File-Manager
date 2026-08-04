#pragma once

#include "journal.h"

#include <QMutex>
#include <QObject>
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
    void renameEntry(const QString &path, const QString &newName, quint64 id);
    void undo(const JournalEntry &entry, quint64 id);

signals:
    // A fraction rather than a count, because copies measure bytes and everything else
    // measures items, and the status bar only ever draws a thin line.
    void progress(quint64 id, double fraction, const QString &currentName);
    void conflict(quint64 id, const QString &targetPath, const QString &suggestedName);
    void finished(quint64 id, const JournalEntry &journal);
    void failed(quint64 id, const QString &message);

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

    std::atomic<bool> m_cancelled { false };

    QMutex m_conflictMutex;
    QWaitCondition m_conflictWait;
    Conflict m_choice = Replace;
    bool m_applyToAll = false;
    bool m_answered = false;

    // Byte accounting for the current copy, so progress is smooth across many files.
    qint64 m_bytesTotal = 0;
    qint64 m_bytesDone = 0;
};
