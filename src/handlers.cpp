#include "handlers.h"

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Where .desktop files live, in XDG precedence order.
QStringList applicationDirs()
{
    QStringList dirs;
    const QStringList data = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &base : data)
        dirs.append(base + QStringLiteral("/applications"));
    return dirs;
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString findDesktopFile(const QString &desktopId)
{
    const QStringList dirs = applicationDirs();
    for (const QString &dir : dirs) {
        const QString candidate = dir + QLatin1Char('/') + desktopId;
        if (QFileInfo::exists(candidate))
            return candidate;
        // Ids may encode a subdirectory as "vendor-app.desktop".
        QString nested = desktopId;
        nested.replace(QLatin1Char('-'), QLatin1Char('/'));
        if (QFileInfo::exists(dir + QLatin1Char('/') + nested))
            return dir + QLatin1Char('/') + nested;
    }
    return {};
}

} // namespace

namespace Handlers {

QStringList parseAssociations(const QString &text, const QString &mimeType,
                              const QString &section)
{
    QStringList ids;
    bool inSection = false;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QLatin1Char('['))) {
            inSection = (line == QLatin1Char('[') + section + QLatin1Char(']'));
            continue;
        }
        if (!inSection)
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0 || line.left(equals).trimmed() != mimeType)
            continue;

        const QStringList entries =
            line.mid(equals + 1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &entry : entries) {
            const QString id = entry.trimmed();
            if (!id.isEmpty() && !ids.contains(id))
                ids.append(id);
        }
    }
    return ids;
}

QString displayNameOf(const QString &desktopFileText, const QString &fallback)
{
    const QString language = QLocale().name().section(QLatin1Char('_'), 0, 0);
    QString generic;
    QString localised;
    bool inEntry = false;

    const QStringList lines = desktopFileText.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inEntry)
            continue;

        if (line.startsWith(QLatin1String("Name=")))
            generic = line.mid(5).trimmed();
        else if (line.startsWith(QStringLiteral("Name[%1]=").arg(language)))
            localised = line.section(QLatin1Char('='), 1).trimmed();
    }

    if (!localised.isEmpty())
        return localised;
    return generic.isEmpty() ? fallback : generic;
}

QList<Handler> forFile(const QString &path)
{
    const QString mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    if (mimeType.isEmpty())
        return {};

    QStringList ordered;

    // The user's own choices win, which is what makes `xdg-mime default` take effect
    // here without omafile keeping any preference of its own.
    const QStringList configDirs =
        QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    for (const QString &dir : configDirs) {
        const QString text = readFile(dir + QStringLiteral("/mimeapps.list"));
        if (text.isEmpty())
            continue;
        for (const QString &id : parseAssociations(text, mimeType, QStringLiteral("Default Applications")))
            if (!ordered.contains(id))
                ordered.append(id);
        for (const QString &id : parseAssociations(text, mimeType, QStringLiteral("Added Associations")))
            if (!ordered.contains(id))
                ordered.append(id);
    }

    // Then whatever the installed applications registered for themselves.
    const QStringList dirs = applicationDirs();
    for (const QString &dir : dirs) {
        for (const QString &name : { QStringLiteral("mimeinfo.cache"),
                                     QStringLiteral("mimeapps.list") }) {
            const QString text = readFile(dir + QLatin1Char('/') + name);
            if (text.isEmpty())
                continue;
            for (const QString &section : { QStringLiteral("MIME Cache"),
                                            QStringLiteral("Added Associations") }) {
                for (const QString &id : parseAssociations(text, mimeType, section))
                    if (!ordered.contains(id))
                        ordered.append(id);
            }
        }
    }

    QList<Handler> handlers;
    QSet<QString> seenNames;
    for (const QString &id : std::as_const(ordered)) {
        const QString file = findDesktopFile(id);
        if (file.isEmpty())
            continue;

        Handler handler;
        handler.desktopId = id;
        handler.desktopFile = file;
        handler.name = displayNameOf(readFile(file), id);

        // One application often ships several .desktop ids — a native package and a
        // Flatpak, say — all with the same Name. Offering "Google Chrome" twice with no
        // way to tell them apart is worse than offering it once; precedence already put
        // the better one first.
        if (seenNames.contains(handler.name))
            continue;
        seenNames.insert(handler.name);

        handler.isDefault = handlers.isEmpty();
        handlers.append(handler);
    }
    return handlers;
}

bool launch(const Handler &handler, const QString &path)
{
    if (handler.desktopFile.isEmpty() || path.isEmpty())
        return false;

    // gio applies the .desktop Exec semantics — field codes, Terminal=true, startup
    // notification — which is a surprising amount of behaviour to reimplement wrongly.
    if (!QStandardPaths::findExecutable(QStringLiteral("gio")).isEmpty()) {
        return QProcess::startDetached(QStringLiteral("gio"),
                                       { QStringLiteral("launch"), handler.desktopFile, path });
    }
    return QProcess::startDetached(QStringLiteral("xdg-open"), { path });
}

} // namespace Handlers
