#pragma once

#include <QList>
#include <QString>

// One application that can open a file.
struct Handler
{
    QString name;      // "Firefox"
    QString desktopId; // "firefox.desktop"
    QString desktopFile;
    bool isDefault = false;
};

// "Open with…" (Ctrl+Enter). §1 is explicit that omafile is not a file-association editor
// — that is `xdg-mime`'s job — so this only *reads* the desktop's own registry and never
// writes to it. Plain Enter still goes through xdg-open and whatever default is set.
namespace Handlers {

// Applications registered for this file's MIME type, the default first.
QList<Handler> forFile(const QString &path);

// Launches a handler on a file. Uses `gio launch`, which applies the .desktop Exec
// semantics (field codes, terminal apps, startup notification) rather than reimplementing
// them badly.
bool launch(const Handler &handler, const QString &path);

// Split out for testing: parses the `[MIME Cache]`-style sections that map a MIME type to
// desktop ids.
QStringList parseAssociations(const QString &text, const QString &mimeType,
                              const QString &section);

// Reads the display name out of a .desktop file, honouring the current locale's Name[..].
QString displayNameOf(const QString &desktopFileText, const QString &fallback);

} // namespace Handlers
