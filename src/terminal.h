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

// One word, quoted so a shell reads it literally.
//
// Needed wherever a command is a *string* rather than an argument list: `sh -c`, and
// anything handed to ssh, which always runs its command through the remote shell. A path
// is attacker-controlled input in the same way a filename is — §14's newline-in-a-filename
// case is this one wearing a different hat — and "wrap it in single quotes" is not
// quoting, because the one character it fails on is the single quote.
QString shellQuote(const QString &word);

// ── Editors ──────────────────────────────────────────────────────────────────
//
// A terminal editor is handed to a terminal, which stays open for as long as it runs. A
// graphical one must not be: given to a terminal it hands the file to the instance
// already running and returns within a fifth of a second, so the window opens and closes
// again before it can be read. That is what made "edit ~/.ssh/config", "edit omafile
// config" and bulk rename all flash.
//
// So the terminal editors are the ones named, not the graphical ones. The list is short
// and it does not change, and an editor nobody here has heard of is opened the way the
// desktop opens it rather than gambling a terminal on it.

// $VISUAL, else $EDITOR, split into a command line — it may carry arguments
// ("nvim -u NONE"), so it is not simply a program name.
QStringList editorCommand();

// Whether that program lives in a terminal. Anything else belongs to the desktop.
bool editorIsTerminal(const QString &program);

// What makes a graphical editor stay in the foreground until the file is closed — the
// one thing bulk rename cannot do without, since it waits on the editor exiting. Empty
// means there is no way to ask, which is a refusal rather than something to paper over.
QStringList editorWaitArgs(const QString &program);

} // namespace Terminal
