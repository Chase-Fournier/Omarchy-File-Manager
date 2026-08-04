#include "operations.h"

#include "clipboard.h"
#include "opener.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <sys/stat.h>

namespace {

dev_t deviceOf(const QString &path)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0)
        return 0;
    return info.st_dev;
}

QStringList localPathsFromUris(const QStringList &uris)
{
    QStringList paths;
    for (const QString &uri : uris) {
        const QUrl url(uri);
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
        else if (!uri.startsWith(QLatin1Char('#')) && uri.startsWith(QLatin1Char('/')))
            paths.append(uri); // some apps hand over bare paths in text/plain
    }
    return paths;
}

} // namespace

Operations::Operations(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<JournalEntry>("JournalEntry");

    m_ops = new FileOps;
    m_ops->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_ops, &QObject::deleteLater);

    connect(m_ops, &FileOps::progress, this, &Operations::onProgress);
    connect(m_ops, &FileOps::conflict, this, &Operations::onConflict);
    connect(m_ops, &FileOps::finished, this, &Operations::onFinished);
    connect(m_ops, &FileOps::failed, this, &Operations::onFailed);

    m_thread.start();

    m_statusTimer.setSingleShot(true);
    m_statusTimer.setInterval(5000);
    connect(&m_statusTimer, &QTimer::timeout, this, [this] {
        m_status.clear();
        emit statusChanged();
    });

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &Operations::clipboardChanged);
}

Operations::~Operations()
{
    m_ops->cancel();
    m_thread.quit();
    m_thread.wait();
}

bool Operations::canPaste() const
{
    return Clipboard::hasPaths();
}

QString Operations::uriList(const QStringList &paths)
{
    QStringList lines;
    lines.reserve(paths.size());
    for (const QString &path : paths)
        lines.append(QString::fromLatin1(QUrl::fromLocalFile(path).toEncoded()));
    // text/uri-list is CRLF-delimited by RFC 2483.
    return lines.join(QStringLiteral("\r\n"));
}

QStringList Operations::producedNames(const JournalEntry &journal)
{
    QStringList names;
    for (const QString &path : journal.created)
        names.append(QFileInfo(path).fileName());
    for (const auto &move : journal.moves)
        names.append(QFileInfo(move.second).fileName());
    return names;
}

quint64 Operations::begin()
{
    m_progress = 0.0;
    m_progressName.clear();
    if (!m_busy) {
        m_busy = true;
        emit busyChanged();
    }
    emit progressChanged();
    return ++m_id;
}

void Operations::setStatus(const QString &text)
{
    m_status = text;
    emit statusChanged();
    m_statusTimer.start();
}

void Operations::cut(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    Clipboard::setPaths(paths, true);
    setStatus(QStringLiteral("Cut %1").arg(paths.size()));
}

void Operations::copyToClipboard(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    Clipboard::setPaths(paths, false);
    setStatus(QStringLiteral("Copied %1").arg(paths.size()));
}

void Operations::copyPathToClipboard(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    Clipboard::setText(paths.join(QLatin1Char('\n')));
    setStatus(paths.size() == 1 ? paths.first()
                                : QStringLiteral("Copied %1 paths").arg(paths.size()));
}

void Operations::paste(const QString &destinationDir)
{
    bool cut = false;
    const QStringList paths = Clipboard::paths(&cut);
    if (paths.isEmpty() || destinationDir.isEmpty())
        return;

    const quint64 operation = begin();

    QMetaObject::invokeMethod(m_ops, cut ? "move" : "copy", Qt::QueuedConnection,
                              Q_ARG(QStringList, paths), Q_ARG(QString, destinationDir),
                              Q_ARG(quint64, operation));

    // A cut is consumed by its paste; leaving it armed invites a second, surprising move.
    if (cut)
        QGuiApplication::clipboard()->clear();
}

void Operations::trash(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "trash", Qt::QueuedConnection,
                              Q_ARG(QStringList, paths), Q_ARG(quint64, operation));
}

void Operations::deletePermanently(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "removePermanently", Qt::QueuedConnection,
                              Q_ARG(QStringList, paths), Q_ARG(quint64, operation));
}

void Operations::newFolder(const QString &parentDir, const QString &name)
{
    if (parentDir.isEmpty() || name.isEmpty())
        return;
    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "makeDirectory", Qt::QueuedConnection,
                              Q_ARG(QString, parentDir), Q_ARG(QString, name),
                              Q_ARG(quint64, operation));
}

void Operations::rename(const QString &path, const QString &newName)
{
    if (path.isEmpty() || newName.isEmpty())
        return;
    if (QFileInfo(path).fileName() == newName)
        return; // nothing to do, and journalling it would waste an undo slot

    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "renameEntry", Qt::QueuedConnection,
                              Q_ARG(QString, path), Q_ARG(QString, newName),
                              Q_ARG(quint64, operation));
}

void Operations::undo()
{
    if (!m_journal.canUndo())
        return;

    const JournalEntry entry = m_journal.takeLast();
    emit canUndoChanged();

    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "undo", Qt::QueuedConnection,
                              Q_ARG(JournalEntry, entry), Q_ARG(quint64, operation));
}

bool Operations::sameFilesystem(const QStringList &paths, const QString &directory)
{
    if (paths.isEmpty())
        return false;
    const dev_t target = deviceOf(directory);
    if (target == 0)
        return false;
    for (const QString &path : paths) {
        if (deviceOf(QFileInfo(path).absolutePath()) != target)
            return false;
    }
    return true;
}

void Operations::dropUris(const QStringList &uris, const QString &destinationDir, int action)
{
    const QStringList paths = localPathsFromUris(uris);
    if (paths.isEmpty() || destinationDir.isEmpty()) {
        setStatus(QStringLiteral("nothing usable in that drop"));
        return;
    }

    // Dropping something into the directory it already lives in is a no-op, not a
    // duplicate — the alternative is a stray "file (2)" every time a drag misses.
    QStringList usable;
    for (const QString &path : paths) {
        if (QFileInfo(path).absolutePath() != QFileInfo(destinationDir).absoluteFilePath())
            usable.append(path);
    }
    if (usable.isEmpty()) {
        setStatus(QStringLiteral("already here"));
        return;
    }

    // §7: move within a filesystem, copy across one, unless the modifier said otherwise.
    bool asMove = action == 2;
    if (action == 0)
        asMove = sameFilesystem(usable, destinationDir);

    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, asMove ? "move" : "copy", Qt::QueuedConnection,
                              Q_ARG(QStringList, usable), Q_ARG(QString, destinationDir),
                              Q_ARG(quint64, operation));
}

void Operations::resolveConflict(int choice, bool applyToAll)
{
    if (!m_conflictActive)
        return;

    m_conflictActive = false;
    emit conflictChanged();
    m_ops->resolveConflict(static_cast<FileOps::Conflict>(choice), applyToAll);
}

void Operations::cancel()
{
    m_ops->cancel();
    if (m_conflictActive) {
        m_conflictActive = false;
        emit conflictChanged();
    }
}

// $TERMINAL if it is set and real, otherwise the first of the usual suspects present.
static QString findTerminal()
{
    QString terminal = QString::fromLocal8Bit(qgetenv("TERMINAL"));
    if (!terminal.isEmpty() && !QStandardPaths::findExecutable(terminal).isEmpty())
        return terminal;

    for (const QString &fallback : { QStringLiteral("alacritty"), QStringLiteral("ghostty"),
                                     QStringLiteral("kitty"), QStringLiteral("foot"),
                                     QStringLiteral("xterm") }) {
        if (!QStandardPaths::findExecutable(fallback).isEmpty())
            return fallback;
    }
    return QString();
}

void Operations::openTerminal(const QString &directory)
{
    // Omarchy's own launcher first, so omafile opens whatever the desktop is configured
    // to use rather than second-guessing it.
    static const QStringList candidates = {
        QStringLiteral("omarchy-launch-terminal"),
        QStringLiteral("xdg-terminal-exec"),
    };

    for (const QString &candidate : candidates) {
        if (!QStandardPaths::findExecutable(candidate).isEmpty()) {
            QProcess process;
            process.setWorkingDirectory(directory);
            process.setProgram(candidate);
            process.startDetached();
            return;
        }
    }

    const QString terminal = findTerminal();
    if (terminal.isEmpty()) {
        setStatus(QStringLiteral("no terminal found"));
        return;
    }

    QProcess process;
    process.setWorkingDirectory(directory);
    process.setProgram(terminal);
    process.startDetached();
}

void Operations::openAtLine(const QString &path, int line)
{
    const QString editor = QString::fromLocal8Bit(qgetenv("EDITOR"));
    const QString terminal = findTerminal();
    if (line <= 0 || editor.isEmpty() || terminal.isEmpty()) {
        Opener::open(path);
        return;
    }

    // "+N" is understood by vi, vim, nvim, helix and emacs alike.
    QProcess process;
    process.setProgram(terminal);
    process.setArguments({ QStringLiteral("-e"), editor,
                           QStringLiteral("+%1").arg(line), path });
    process.setWorkingDirectory(QFileInfo(path).absolutePath());
    process.startDetached();
}

void Operations::onProgress(quint64 id, double fraction, const QString &name)
{
    if (id != m_id)
        return;
    m_progress = fraction;
    m_progressName = name;
    emit progressChanged();
}

void Operations::onConflict(quint64 id, const QString &targetPath, const QString &suggestedName)
{
    if (id != m_id) {
        // A stale conflict would park the worker forever; answer it and move on.
        m_ops->resolveConflict(FileOps::Cancel, true);
        return;
    }

    m_conflictActive = true;
    m_conflictName = QFileInfo(targetPath).fileName();
    m_conflictSuggestion = suggestedName;
    emit conflictChanged();
}

void Operations::onFinished(quint64 id, const JournalEntry &journal)
{
    if (id != m_id)
        return;

    if (journal.isUndoable()) {
        m_journal.record(journal);
        emit canUndoChanged();
    }

    m_busy = false;
    m_progress = 0.0;
    m_progressName.clear();
    emit busyChanged();
    emit progressChanged();

    if (!journal.summary.isEmpty()) {
        setStatus(journal.isUndoable()
                      ? QStringLiteral("%1 — Ctrl+Z to undo").arg(journal.summary)
                      : journal.summary);
    }

    emit completed(producedNames(journal));
}

void Operations::onFailed(quint64 id, const QString &message)
{
    if (id != m_id)
        return;

    m_busy = false;
    m_progress = 0.0;
    emit busyChanged();
    emit progressChanged();
    setStatus(message);
    emit completed({});
}
