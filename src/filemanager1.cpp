#include "filemanager1.h"

#include "opener.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

namespace {

// Long enough that a burst of calls reuses one process, short enough that omafile is not
// sitting in the session for the rest of the day having been asked one question.
constexpr int kIdleMs = 30000;

} // namespace

FileManager1::FileManager1(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, QCoreApplication::instance(), &QCoreApplication::quit);
    m_idle.start();
}

void FileManager1::touch()
{
    m_idle.start();
}

bool FileManager1::registerService()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    // The object first: a caller that wins the race to the name must not find nothing
    // at the path.
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), this,
                            QDBusConnection::ExportScriptableSlots)) {
        return false;
    }
    return bus.registerService(QStringLiteral("org.freedesktop.FileManager1"));
}

QStringList FileManager1::localPaths(const QStringList &uris)
{
    QStringList paths;
    for (const QString &uri : uris) {
        const QUrl url(uri);
        // Only local files: this interface is defined over file:// and omafile has
        // nothing to show for a URI it cannot reach as a path.
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
            continue;
        }
        // Some callers hand over a bare path rather than a URI.
        if (!uri.isEmpty() && uri.startsWith(QLatin1Char('/')))
            paths.append(uri);
    }
    return paths;
}

void FileManager1::ShowFolders(const QStringList &uris, const QString &)
{
    touch();
    const QStringList paths = localPaths(uris);
    for (const QString &path : paths)
        Opener::openInNewWindow(path);
}

void FileManager1::ShowItems(const QStringList &uris, const QString &)
{
    touch();

    // "Show me this file" means its parent directory, with the file selected — which is
    // exactly what --select already does for the command line.
    const QStringList paths = localPaths(uris);
    for (const QString &path : paths) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                { QStringLiteral("--select"), path });
    }
}

void FileManager1::ShowItemProperties(const QStringList &uris, const QString &)
{
    touch();

    // There is a properties panel now, so this can mean what the interface says rather
    // than approximating it with "here is the file, work it out".
    const QStringList paths = localPaths(uris);
    for (const QString &path : paths) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                { QStringLiteral("--properties"), path });
    }
}
