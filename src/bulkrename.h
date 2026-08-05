#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// §9's bulk rename: the vimv approach. The selected basenames are written to a temp file,
// `$EDITOR` opens it, and the edited lines are diffed against the originals on exit. That
// gives regex rename, sequential numbering, case changes and sorting for free, with no UI
// at all.
//
// The planning is pure so the awkward parts — a line count that changed, a swap, a
// three-way rotation — are testable without touching a filesystem.
namespace BulkRename {

using Rename = QPair<QString, QString>; // from -> to, both basenames

struct Plan
{
    bool ok = false;
    QString error;
    // Only the entries that actually changed, in the order they must be executed.
    QList<Rename> steps;
    // How many files the user is really renaming, ignoring the temporary hops a cycle
    // needs. Used for the status line, which should say 2, not 3, for a simple swap.
    int changed = 0;
};

// Validates the edit and orders the work. A changed line count aborts the whole thing
// rather than renaming a prefix of it (§9).
Plan plan(const QStringList &originals, const QStringList &edited);

// Splits an editor buffer into lines the way the plan expects: trailing newline ignored,
// nothing else trimmed away, because a filename may legitimately have leading spaces.
QStringList linesOf(const QString &text);

} // namespace BulkRename
