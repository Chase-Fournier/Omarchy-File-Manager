#include "bulkrename.h"

#include <QSet>

namespace {

// A name no editor would produce and no directory is likely to hold, used only as the
// stepping stone that breaks a rename cycle.
QString temporaryFor(const QString &name, int counter)
{
    return QStringLiteral(".omafile-rename-%1-%2").arg(counter).arg(name);
}

} // namespace

namespace BulkRename {

QStringList linesOf(const QString &text)
{
    QString body = text;
    // Editors add a trailing newline; that is not an extra (empty) filename.
    if (body.endsWith(QLatin1Char('\n')))
        body.chop(1);
    if (body.isEmpty())
        return {};
    return body.split(QLatin1Char('\n'));
}

Plan plan(const QStringList &originals, const QStringList &edited)
{
    Plan result;

    // §9: a changed line count aborts with an error and no partial renames. Silently
    // pairing up mismatched lists is how a bulk rename destroys a directory.
    if (originals.size() != edited.size()) {
        result.error = QStringLiteral("expected %1 lines, got %2 — nothing was renamed")
                           .arg(originals.size())
                           .arg(edited.size());
        return result;
    }
    if (originals.isEmpty()) {
        result.ok = true;
        return result;
    }

    QList<Rename> wanted;
    QSet<QString> targets;
    for (int i = 0; i < originals.size(); ++i) {
        const QString &from = originals.at(i);
        const QString to = edited.at(i);

        if (to.isEmpty()) {
            result.error = QStringLiteral("line %1 is empty").arg(i + 1);
            return result;
        }
        if (to.contains(QLatin1Char('/'))) {
            result.error = QStringLiteral("\"%1\" contains a slash").arg(to);
            return result;
        }
        // Two files cannot end up with one name, and finding that out afterwards means
        // one of them is already gone.
        if (targets.contains(to)) {
            result.error = QStringLiteral("\"%1\" is used twice").arg(to);
            return result;
        }
        targets.insert(to);

        if (from != to)
            wanted.append({ from, to });
    }

    result.changed = int(wanted.size());

    // Order the work so nothing is overwritten while it is still needed. A rename may
    // proceed as soon as its target is not the source of some other pending rename.
    QSet<QString> pendingSources;
    for (const Rename &rename : wanted)
        pendingSources.insert(rename.first);

    QList<Rename> remaining = wanted;
    int temporaries = 0;

    while (!remaining.isEmpty()) {
        int ready = -1;
        for (int i = 0; i < remaining.size(); ++i) {
            if (!pendingSources.contains(remaining.at(i).second)) {
                ready = i;
                break;
            }
        }

        if (ready >= 0) {
            const Rename step = remaining.takeAt(ready);
            pendingSources.remove(step.first);
            result.steps.append(step);
            continue;
        }

        // Everything left is part of a cycle (a -> b, b -> a, or longer). Move one member
        // out of the way to a temporary name; that frees its slot and lets the rest
        // unwind normally, with the temporary renamed into place at the end.
        Rename step = remaining.takeFirst();
        const QString temporary = temporaryFor(step.first, ++temporaries);
        result.steps.append({ step.first, temporary });
        pendingSources.remove(step.first);
        remaining.append({ temporary, step.second });
        pendingSources.insert(temporary);
    }

    result.ok = true;
    return result;
}

} // namespace BulkRename
