#include "terminal.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

// The launchers that implement the Default Terminal specification, which take a command
// directly rather than behind -e. They are preferred over naming an emulator ourselves,
// because they open whatever the desktop is actually configured to use.
bool isSpecLauncher(const QString &name)
{
    return name == QLatin1String("xdg-terminal-exec")
        || name == QLatin1String("omarchy-launch-terminal");
}

} // namespace

namespace Terminal {

QString find()
{
    const QString configured = QString::fromLocal8Bit(qgetenv("TERMINAL"));
    if (!configured.isEmpty() && !QStandardPaths::findExecutable(configured).isEmpty())
        return configured;

    for (const QString &fallback : { QStringLiteral("omarchy-launch-terminal"),
                                     QStringLiteral("xdg-terminal-exec"),
                                     QStringLiteral("alacritty"), QStringLiteral("ghostty"),
                                     QStringLiteral("kitty"), QStringLiteral("foot"),
                                     QStringLiteral("xterm") }) {
        if (!QStandardPaths::findExecutable(fallback).isEmpty())
            return fallback;
    }
    return QString();
}

QStringList argsFor(const QString &terminal, const QStringList &command)
{
    if (command.isEmpty())
        return {};

    const QString name = QFileInfo(terminal).fileName();
    if (isSpecLauncher(name)) {
        // `--` so a command that begins with a dash is not read as an option.
        if (name == QLatin1String("xdg-terminal-exec"))
            return QStringList{ QStringLiteral("--") } + command;
        return command;
    }
    return QStringList{ QStringLiteral("-e") } + command;
}

bool run(const QStringList &command, const QString &workingDir)
{
    const QString terminal = find();
    if (terminal.isEmpty())
        return false;

    QProcess process;
    process.setProgram(terminal);
    process.setArguments(argsFor(terminal, command));
    if (!workingDir.isEmpty())
        process.setWorkingDirectory(workingDir);
    return process.startDetached();
}

bool runHeld(const QString &shellCommand, const QString &workingDir)
{
    return run({ QStringLiteral("sh"), QStringLiteral("-c"),
                 shellCommand + QStringLiteral("; echo; echo '[press enter to close]'; read _") },
               workingDir);
}

bool openAt(const QString &directory)
{
    const QString terminal = find();
    if (terminal.isEmpty())
        return false;

    QProcess process;
    process.setProgram(terminal);
    process.setWorkingDirectory(directory);
    return process.startDetached();
}

} // namespace Terminal
