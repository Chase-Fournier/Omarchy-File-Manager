#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

// org.freedesktop.FileManager1 — the interface applications call when they mean "show me
// this in the file manager".
//
// It is not xdg-open and it is not the MIME default: VS Code's "Reveal in File Explorer"
// and Chromium's "Show in folder" both make a D-Bus call to this well-known name, and
// whoever owns it answers. On a machine with Nautilus installed, that is Nautilus —
// setting `xdg-mime default omafile.desktop inode/directory` does not change it, because
// nothing about this path consults the MIME database.
//
// omafile answers it in a mode of its own, started by D-Bus on demand
// (`omafile --dbus-service`) rather than by every ordinary launch. That keeps the name
// registration off the startup path §12 measures, and means a machine that never calls
// the interface never pays for it.
//
// Each call spawns an ordinary omafile process, exactly as Ctrl+N does — §3's "a second
// window is a second process: no daemon, no IPC, no shared state" applies here too, so
// this service holds nothing and can exit whenever it is idle.
class FileManager1 : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1(QObject *parent = nullptr);

    // Registers the object and claims the well-known name. False when the name is already
    // owned — another file manager got there first, which is not an error worth crashing
    // over, just a reason to stand down.
    bool registerService();

    // file:// URIs to local paths, dropping anything that is not a local file. Exposed
    // for testing: the awkward cases here are spaces, '#' and non-ASCII, which is the
    // same hazard the clipboard's uri-list has.
    static QStringList localPaths(const QStringList &uris);

public slots:
    // The interface, verbatim. Names and signatures are the contract — a typo here is a
    // method the caller cannot find.
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowItems(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowItemProperties(const QStringList &uris, const QString &startupId);

private:
    void touch();

    // Nothing is held between calls, so sitting resident forever earns nothing. D-Bus
    // starts it again the next time somebody asks.
    QTimer m_idle;
};
