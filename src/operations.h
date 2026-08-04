#pragma once

#include "fileops.h"
#include "journal.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>

// The QML-facing front for everything in §8: the clipboard, the ops thread, the journal,
// the conflict prompt and the transient status message. The UI knows this object and the
// DirectoryModel, and nothing else.
class Operations : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressName READ progressName NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)

    Q_PROPERTY(bool conflictActive READ conflictActive NOTIFY conflictChanged)
    Q_PROPERTY(QString conflictName READ conflictName NOTIFY conflictChanged)
    Q_PROPERTY(QString conflictSuggestion READ conflictSuggestion NOTIFY conflictChanged)

public:
    explicit Operations(QObject *parent = nullptr);
    ~Operations() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString progressName() const { return m_progressName; }
    QString status() const { return m_status; }
    bool canUndo() const { return m_journal.canUndo(); }
    bool canPaste() const;

    bool conflictActive() const { return m_conflictActive; }
    QString conflictName() const { return m_conflictName; }
    QString conflictSuggestion() const { return m_conflictSuggestion; }

    Q_INVOKABLE void cut(const QStringList &paths);
    Q_INVOKABLE void copyToClipboard(const QStringList &paths);
    Q_INVOKABLE void paste(const QString &destinationDir);
    Q_INVOKABLE void copyPathToClipboard(const QStringList &paths);

    Q_INVOKABLE void trash(const QStringList &paths);
    Q_INVOKABLE void deletePermanently(const QStringList &paths);
    Q_INVOKABLE void newFolder(const QString &parentDir, const QString &name);
    Q_INVOKABLE void rename(const QString &path, const QString &newName);
    Q_INVOKABLE void undo();

    // A drop from another app, or from another omafile window.
    // action: 0 = default (move within a filesystem, copy across), 1 = copy, 2 = move.
    Q_INVOKABLE void dropUris(const QStringList &uris, const QString &destinationDir,
                              int action);

    // Properly percent-encoded file:// URIs, one per line. Built here rather than in
    // QML because encodeURI leaves '#' and '?' unescaped, which corrupts any path
    // containing them the moment another app parses the list.
    Q_INVOKABLE static QString uriList(const QStringList &paths);

    Q_INVOKABLE void resolveConflict(int choice, bool applyToAll);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openTerminal(const QString &directory);

    // True when moving these paths into this directory would stay on one filesystem,
    // which is what makes move the sensible default for a drop (§7).
    Q_INVOKABLE static bool sameFilesystem(const QStringList &paths, const QString &directory);

signals:
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void canUndoChanged();
    void clipboardChanged();
    void conflictChanged();

    // The model listens for this to refresh and to select whatever the operation
    // produced, so a drop or a paste leaves its results selected and ready to act on.
    void completed(const QStringList &selectNames);

private slots:
    void onProgress(quint64 id, double fraction, const QString &name);
    void onConflict(quint64 id, const QString &targetPath, const QString &suggestedName);
    void onFinished(quint64 id, const JournalEntry &journal);
    void onFailed(quint64 id, const QString &message);

private:
    // Every call goes through here so busy/progress bookkeeping cannot be forgotten.
    quint64 begin();
    // What an operation produced, derived from its journal entry rather than guessed up
    // front — so a file renamed by conflict resolution is still the one selected.
    static QStringList producedNames(const JournalEntry &journal);
    void setStatus(const QString &text);

    FileOps *m_ops = nullptr;
    QThread m_thread;
    Journal m_journal;

    quint64 m_id = 0;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_progressName;
    QString m_status;

    bool m_conflictActive = false;
    QString m_conflictName;
    QString m_conflictSuggestion;

    // §8: the status line clears itself after five seconds.
    QTimer m_statusTimer;
};
