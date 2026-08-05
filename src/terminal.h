#pragma once

#include <QString>
#include <QStringList>

// Launching a terminal, in one place.
//
// This lived in two places and grew the same bug in both: every caller wrote `-e` in
// front of its command, which is right for the emulators and wrong for the launchers that
// implement the Default Terminal specification — those take the command directly, and
// silently open nothing when handed a `-e` they do not understand. $TERMINAL on Omarchy
// is `xdg-terminal-exec`, so that was every terminal omafile tried to open.
namespace Terminal {

// $TERMINAL if it is set and real, otherwise the first of the usual suspects present.
QString find();

// The argument list that runs `command` in `terminal`.
QStringList argsFor(const QString &terminal, const QStringList &command);

// Run a command, detached. False if no terminal could be found.
bool run(const QStringList &command, const QString &workingDir = QString());

// Run a shell command and hold the window open afterwards, so whatever it printed can
// still be read rather than flashing past.
bool runHeld(const QString &shellCommand, const QString &workingDir = QString());

// Just a terminal, no command — "terminal here".
bool openAt(const QString &directory);

} // namespace Terminal
