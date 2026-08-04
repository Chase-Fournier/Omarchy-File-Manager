#include "watcher.h"

#include <QFileSystemWatcher>

Watcher::Watcher(QObject *parent)
    : QObject(parent)
{
    // A single file operation produces a burst of inotify events; 50 ms turns an
    // unpacked tarball into one re-list instead of hundreds.
    m_coalesce = new QTimer(this);
    m_coalesce->setSingleShot(true);
    m_coalesce->setInterval(50);
    connect(m_coalesce, &QTimer::timeout, this, &Watcher::changed);
}

void Watcher::watch(const QString &path)
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
                [this] { m_coalesce->start(); });
    }

    if (m_path == path)
        return;

    stop();
    m_path = path;
    if (!path.isEmpty())
        m_watcher->addPath(path);
}

void Watcher::stop()
{
    m_coalesce->stop();
    m_path.clear();
    if (!m_watcher)
        return;

    const QStringList watched = m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);
}
