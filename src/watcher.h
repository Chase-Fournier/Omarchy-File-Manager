#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QFileSystemWatcher;

// inotify on the current directory, coalesced. Lives on the worker thread alongside the
// Lister, both because QFileSystemWatcher::addPath stats the path and because the
// re-list it triggers should not have to hop threads to start.
//
// It reports only "something changed" — working out *what* is DirectoryModel's job,
// which diffs a fresh listing against the rows it already has rather than resetting.
class Watcher : public QObject
{
    Q_OBJECT

public:
    explicit Watcher(QObject *parent = nullptr);

public slots:
    // Watch exactly one directory; any previous one is dropped.
    void watch(const QString &path);
    void stop();

signals:
    void changed();

private:
    QFileSystemWatcher *m_watcher = nullptr;
    // Parented, not a value member: moveToThread only carries QObject children, and a
    // timer left behind on the GUI thread cannot be started from the worker.
    QTimer *m_coalesce = nullptr;
    QString m_path;
};
