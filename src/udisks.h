#pragma once

#include <QList>
#include <QObject>
#include <QString>

// Drives that exist but are not mounted.
//
// /proc/self/mountinfo answers "what is mounted", which is a different question from
// "what is plugged in": a stick that nothing has mounted yet appears nowhere in it. That
// was the whole of the gap — plug a drive in, and omafile showed nothing until some other
// application mounted it, at which point the mount watcher noticed immediately.
//
// udisks2 answers the other question, and every desktop already runs it. This is a
// read-and-mount client, not a device manager: no formatting, no partitioning, no
// unlocking — those are what `udisksctl` and the disks application are for (§1).
struct RemovableDevice
{
    QString objectPath; // /org/freedesktop/UDisks2/block_devices/sda1
    QString device;     // /dev/sda1
    QString label;      // what the filesystem calls itself, if anything
    QString fsType;
    QString mountPath;  // empty when it is not mounted
    bool mounted = false;

    // What the sidebar shows: the label, else the device name.
    QString name() const;
};

// Emits changed() when a drive is attached or detached. Constructed only when the sidebar
// is first drawn, so a window that never shows it never talks to the system bus.
class UDisks : public QObject
{
    Q_OBJECT

public:
    explicit UDisks(QObject *parent = nullptr);

    // Whether udisks2 is answering at all. False degrades to mountinfo alone, which is
    // exactly the behaviour that existed before this class (§13's "gray out, never fail").
    static bool available();

    // Removable filesystems worth showing: skips anything udisks itself hints should be
    // ignored, anything it calls a system device, and every non-removable drive.
    QList<RemovableDevice> devices() const;

    // Mounts it and returns where. Empty on failure, with `error` set.
    QString mount(const QString &objectPath, QString *error) const;

signals:
    void changed();
};
