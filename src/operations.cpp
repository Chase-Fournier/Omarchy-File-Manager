#include "operations.h"

#include "bulkrename.h"
#include "clipboard.h"
#include "handlers.h"
#include "mounts.h"
#include "opener.h"
#include "terminal.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

#include <sys/stat.h>

namespace {

// ── Launching an editor ───────────────────────────────────────────────────────
//
// Two things here are not interchangeable and were being treated as though they were.
//
// A *terminal editor* is handed to a terminal, which stays open for as long as the editor
// runs. A *graphical editor* must be launched directly: given to a terminal it hands the
// file to an already-running instance and returns within a fifth of a second, so the
// window opens and closes again before you can read it. That is the whole bug — with
// $EDITOR=code, "edit ~/.ssh/config", "edit omafile config" and bulk rename all flashed.
//
// And a terminal is not always told the same way. The spec launchers take the command
// directly; the emulators want -e. Passing -e to xdg-terminal-exec opened nothing at all.

// $EDITOR may carry arguments ("code --wait", "nvim -u NONE"), so it is a command line,
// not a program name. $VISUAL wins where both are set, which is the usual convention.
QStringList editorCommand()
{
    QString editor = QString::fromLocal8Bit(qgetenv("VISUAL"));
    if (editor.isEmpty())
        editor = QString::fromLocal8Bit(qgetenv("EDITOR"));
    return QProcess::splitCommand(editor);
}

bool isGraphicalEditor(const QString &program)
{
    static const QSet<QString> graphical = {
        QStringLiteral("code"),   QStringLiteral("code-insiders"),
        QStringLiteral("codium"), QStringLiteral("vscodium"),
        QStringLiteral("cursor"), QStringLiteral("windsurf"),
        QStringLiteral("subl"),   QStringLiteral("sublime_text"),
        QStringLiteral("zed"),    QStringLiteral("zeditor"),
        QStringLiteral("kate"),   QStringLiteral("kwrite"),
        QStringLiteral("gedit"),  QStringLiteral("gnome-text-editor"),
        QStringLiteral("geany"),  QStringLiteral("mousepad"),
        QStringLiteral("xed"),    QStringLiteral("pluma"),
        QStringLiteral("notepadqq"), QStringLiteral("atom"),
        QStringLiteral("gvim"),   QStringLiteral("nvim-qt"),
    };
    return graphical.contains(program);
}

// What makes a graphical editor stay in the foreground until the file is closed. Bulk
// rename is meaningless without it: the edit would be "finished" before it began. Empty
// means there is no way to ask, which is a refusal rather than something to paper over.
QStringList blockingArgsFor(const QString &program)
{
    static const QSet<QString> waits = {
        QStringLiteral("code"),   QStringLiteral("code-insiders"),
        QStringLiteral("codium"), QStringLiteral("vscodium"),
        QStringLiteral("cursor"), QStringLiteral("windsurf"),
        QStringLiteral("subl"),   QStringLiteral("sublime_text"),
        QStringLiteral("zed"),    QStringLiteral("zeditor"),
    };
    if (waits.contains(program))
        return { QStringLiteral("--wait") };
    if (program == QLatin1String("kate") || program == QLatin1String("kwrite"))
        return { QStringLiteral("--block") };
    if (program == QLatin1String("gvim"))
        return { QStringLiteral("-f") };
    if (program == QLatin1String("nvim-qt"))
        return { QStringLiteral("--nofork") };
    return {};
}

// Opening at a line, which every editor spells differently.
QStringList lineArgsFor(const QString &program, const QString &path, int line)
{
    if (line <= 0)
        return { path };

    static const QSet<QString> gotoFlag = {
        QStringLiteral("code"),   QStringLiteral("code-insiders"),
        QStringLiteral("codium"), QStringLiteral("vscodium"),
        QStringLiteral("cursor"), QStringLiteral("windsurf"),
    };
    // subl, zed and helix all take the line as a suffix on the path instead.
    static const QSet<QString> suffix = {
        QStringLiteral("subl"), QStringLiteral("sublime_text"),
        QStringLiteral("zed"),  QStringLiteral("zeditor"),
        QStringLiteral("hx"),   QStringLiteral("helix"),
    };

    if (gotoFlag.contains(program))
        return { QStringLiteral("--goto"), QStringLiteral("%1:%2").arg(path).arg(line) };
    if (suffix.contains(program))
        return { QStringLiteral("%1:%2").arg(path).arg(line) };
    if (program == QLatin1String("kate") || program == QLatin1String("kwrite"))
        return { QStringLiteral("-l"), QString::number(line), path };
    // vi, vim, nvim, nano, emacs, micro and kakoune all understand +N.
    return { QStringLiteral("+%1").arg(line), path };
}

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

    // §10.6: there is no dependable trash on a network mount, and silently creating one
    // on the far end would be worse than refusing. The caller is expected to have asked
    // isRemote() and offered a permanent delete instead.
    for (const QString &path : paths) {
        if (isRemote(path)) {
            setStatus(QStringLiteral("no trash on a remote location — Shift+Delete "
                                     "deletes permanently"));
            emit completed({});
            return;
        }
    }

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

void Operations::newFile(const QString &parentDir, const QString &name)
{
    if (parentDir.isEmpty() || name.isEmpty())
        return;
    const quint64 operation = begin();
    QMetaObject::invokeMethod(m_ops, "makeFile", Qt::QueuedConnection,
                              Q_ARG(QString, parentDir), Q_ARG(QString, name),
                              Q_ARG(quint64, operation));
}

void Operations::runInTerminal(const QString &command, const QString &workingDir)
{
    if (!Terminal::runHeld(command, workingDir))
        setStatus(QStringLiteral("no terminal found"));
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

void Operations::bulkRename(const QString &directory, const QStringList &names)
{
    if (names.isEmpty() || directory.isEmpty())
        return;
    if (!m_renameFile.isEmpty()) {
        setStatus(QStringLiteral("a bulk rename is already open"));
        return;
    }

    const QStringList editor = editorCommand();
    if (editor.isEmpty()) {
        setStatus(QStringLiteral("set $EDITOR to use bulk rename"));
        return;
    }

    // Everything here waits on the editor exiting, so an editor that cannot be asked to
    // wait has to be refused up front rather than "succeeding" with an unedited file.
    const QString program = editor.first();
    const bool graphical = isGraphicalEditor(program);
    QStringList blocking;
    QString terminal;

    if (graphical) {
        blocking = blockingArgsFor(program);
        if (blocking.isEmpty()) {
            setStatus(QStringLiteral("%1 cannot be asked to wait — set $EDITOR to a "
                                     "terminal editor for bulk rename").arg(program));
            return;
        }
    } else {
        terminal = Terminal::find();
        if (terminal.isEmpty()) {
            setStatus(QStringLiteral("no terminal found"));
            return;
        }
    }

    // A real file rather than a pipe: $EDITOR needs something to open, and keeping it
    // out of the directory being renamed avoids it appearing in its own listing.
    auto *file = new QTemporaryFile(QDir::tempPath() + QStringLiteral("/omafile-rename-XXXXXX"));
    file->setAutoRemove(false);
    if (!file->open()) {
        delete file;
        setStatus(QStringLiteral("could not create the rename file"));
        return;
    }
    file->write(names.join(QLatin1Char('\n')).toUtf8());
    file->write("\n");
    m_renameFile = file->fileName();
    file->close();
    delete file;

    m_renameDirectory = directory;
    m_renameOriginals = names;

    auto *process = new QProcess(this);
    if (graphical) {
        process->setProgram(program);
        process->setArguments(editor.mid(1) + blocking + QStringList{ m_renameFile });
    } else {
        process->setProgram(terminal);
        process->setArguments(Terminal::argsFor(terminal, editor + QStringList{ m_renameFile }));
    }
    connect(process, &QProcess::finished, this,
            [this, process](int, QProcess::ExitStatus) {
                process->deleteLater();

                QFile edited(m_renameFile);
                QStringList lines;
                if (edited.open(QIODevice::ReadOnly | QIODevice::Text))
                    lines = BulkRename::linesOf(QString::fromUtf8(edited.readAll()));
                edited.remove();

                const QString directory = m_renameDirectory;
                const QStringList originals = m_renameOriginals;
                m_renameFile.clear();
                m_renameDirectory.clear();
                m_renameOriginals.clear();

                if (lines.isEmpty()) {
                    setStatus(QStringLiteral("bulk rename cancelled"));
                    return;
                }

                const quint64 operation = begin();
                QMetaObject::invokeMethod(m_ops, "bulkRename", Qt::QueuedConnection,
                                          Q_ARG(QString, directory),
                                          Q_ARG(QStringList, originals),
                                          Q_ARG(QStringList, lines),
                                          Q_ARG(quint64, operation));
            });
    process->start();
    setStatus(QStringLiteral("editing %1 names…").arg(names.size()));
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

bool Operations::isRemote(const QString &path)
{
    return !Mounts::networkRootFor(path).isEmpty();
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

void Operations::reportStatus(const QString &message)
{
    if (!message.isEmpty())
        setStatus(message);
}

void Operations::openInNewWindow(const QString &location)
{
    Opener::openInNewWindow(location);
}

void Operations::openTerminal(const QString &directory)
{
    if (!Terminal::openAt(directory))
        setStatus(QStringLiteral("no terminal found"));
}

QVariantList Operations::handlersFor(const QString &path) const
{
    QVariantList out;
    const QList<Handler> handlers = Handlers::forFile(path);
    for (const Handler &handler : handlers) {
        out.append(QVariantMap { { QStringLiteral("name"), handler.name },
                                 { QStringLiteral("desktopFile"), handler.desktopFile },
                                 { QStringLiteral("isDefault"), handler.isDefault } });
    }
    return out;
}

void Operations::openWith(const QString &desktopFile, const QString &path)
{
    Handler handler;
    handler.desktopFile = desktopFile;
    if (!Handlers::launch(handler, path))
        setStatus(QStringLiteral("could not launch that application"));
}

void Operations::openAtLine(const QString &path, int line)
{
    const QStringList editor = editorCommand();
    if (editor.isEmpty()) {
        Opener::open(path); // no $EDITOR: whatever the desktop opens text with will do
        return;
    }

    const QString program = editor.first();
    const QStringList args = editor.mid(1) + lineArgsFor(program, path, line);
    const QString workingDir = QFileInfo(path).absolutePath();

    // A graphical editor is launched directly. Handing it to a terminal gives a window
    // that opens and closes again immediately, because it returns as soon as it has
    // passed the file to the instance already running.
    if (isGraphicalEditor(program)) {
        QProcess::startDetached(program, args, workingDir);
        return;
    }

    const QString terminal = Terminal::find();
    if (terminal.isEmpty()) {
        Opener::open(path);
        return;
    }

    QProcess process;
    process.setProgram(terminal);
    process.setArguments(Terminal::argsFor(terminal, QStringList{ program } + args));
    process.setWorkingDirectory(workingDir);
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
