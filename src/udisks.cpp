#include "udisks.h"

#include <QDBusArgument>
#include <QFile>
#include <QRegularExpression>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QFileInfo>

namespace {

const char *kService = "org.freedesktop.UDisks2";
const char *kRoot = "/org/freedesktop/UDisks2";
const char *kBlock = "org.freedesktop.UDisks2.Block";
const char *kFilesystem = "org.freedesktop.UDisks2.Filesystem";
const char *kDrive = "org.freedesktop.UDisks2.Drive";

// udisksd is a local daemon and these calls are made from the GUI thread, so they are
// bounded rather than left on Qt's 25-second default: a wedged daemon should cost a
// pause, not the window.
constexpr int kTimeoutMs = 3000;

QVariant readProperty(const QString &objectPath, const char *interface, const char *name)
{
    QDBusInterface properties(QLatin1String(kService), objectPath,
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::systemBus());
    properties.setTimeout(kTimeoutMs);
    const QDBusReply<QDBusVariant> reply =
        properties.call(QStringLiteral("Get"), QLatin1String(interface), QLatin1String(name));
    return reply.isValid() ? reply.value().variant() : QVariant();
}

// udisks hands back paths as NUL-terminated byte arrays, not strings.
QString fromByteArray(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    return QFile::decodeName(bytes);
}

} // namespace

QString RemovableDevice::name() const
{
    if (!label.isEmpty())
        return label;
    const QString base = device.section(QLatin1Char('/'), -1);
    return base.isEmpty() ? device : base;
}

UDisks::UDisks(QObject *parent)
    : QObject(parent)
{
    // Attached and detached, straight from the daemon. A drive appearing changes nothing
    // about the mount table, so this is the only thing that can report it.
    QDBusConnection::systemBus().connect(
        QLatin1String(kService), QLatin1String(kRoot),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("InterfacesAdded"), this, SIGNAL(changed()));
    QDBusConnection::systemBus().connect(
        QLatin1String(kService), QLatin1String(kRoot),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("InterfacesRemoved"), this, SIGNAL(changed()));
}

bool UDisks::available()
{
    if (!QDBusConnection::systemBus().isConnected())
        return false;
    QDBusInterface manager(QLatin1String(kService), QLatin1String(kRoot),
                           QStringLiteral("org.freedesktop.DBus.Peer"),
                           QDBusConnection::systemBus());
    manager.setTimeout(kTimeoutMs);
    return manager.call(QStringLiteral("Ping")).type() != QDBusMessage::ErrorMessage;
}

QList<RemovableDevice> UDisks::devices() const
{
    QList<RemovableDevice> found;
    if (!QDBusConnection::systemBus().isConnected())
        return found;

    // The introspection call returns everything udisks knows in one round trip, but
    // decoding its nested a{oa{sa{sv}}} needs a registered metatype; asking per object is
    // a few more calls on a list that is single digits long, and needs no marshalling.
    QDBusInterface manager(QLatin1String(kService), QLatin1String(kRoot),
                           QStringLiteral("org.freedesktop.DBus.Introspectable"),
                           QDBusConnection::systemBus());
    manager.setTimeout(kTimeoutMs);
    const QDBusReply<QString> introspection = manager.call(QStringLiteral("Introspect"));
    if (!introspection.isValid())
        return found;

    QDBusInterface blocks(QLatin1String(kService),
                          QLatin1String(kRoot) + QStringLiteral("/block_devices"),
                          QStringLiteral("org.freedesktop.DBus.Introspectable"),
                          QDBusConnection::systemBus());
    blocks.setTimeout(kTimeoutMs);
    const QDBusReply<QString> blockList = blocks.call(QStringLiteral("Introspect"));
    if (!blockList.isValid())
        return found;

    // <node name="sda1"/> for each block device.
    static const QRegularExpression nodeName(QStringLiteral("<node name=\"([^\"]+)\""));
    auto matches = nodeName.globalMatch(blockList.value());
    while (matches.hasNext()) {
        const QString path = QLatin1String(kRoot) + QStringLiteral("/block_devices/")
                           + matches.next().captured(1);

        // No Filesystem interface means there is nothing to open — a partition table, a
        // swap area, an unformatted disk.
        const QVariant mountPoints = readProperty(path, kFilesystem, "MountPoints");
        if (!mountPoints.isValid())
            continue;

        // udisks' own opinion about what a person should be shown, which is what keeps
        // EFI partitions and the like out of the sidebar. Nautilus honours these too.
        if (readProperty(path, kBlock, "HintIgnore").toBool())
            continue;
        if (readProperty(path, kBlock, "HintSystem").toBool())
            continue;

        const QString drivePath = readProperty(path, kBlock, "Drive").value<QDBusObjectPath>().path();
        if (drivePath.isEmpty() || drivePath == QLatin1String("/"))
            continue;
        if (!readProperty(drivePath, kDrive, "Removable").toBool())
            continue;

        RemovableDevice device;
        device.objectPath = path;
        device.device = fromByteArray(readProperty(path, kBlock, "Device"));
        device.label = readProperty(path, kBlock, "IdLabel").toString();
        device.fsType = readProperty(path, kBlock, "IdType").toString();

        // MountPoints is aay — a list of byte arrays. The first is where it lives.
        const QDBusArgument argument = mountPoints.value<QDBusArgument>();
        QList<QByteArray> paths;
        argument >> paths;
        if (!paths.isEmpty()) {
            QByteArray first = paths.first();
            while (first.endsWith('\0'))
                first.chop(1);
            device.mountPath = QFile::decodeName(first);
            device.mounted = !device.mountPath.isEmpty();
        }
        found.append(device);
    }
    return found;
}

QString UDisks::mount(const QString &objectPath, QString *error) const
{
    QDBusInterface filesystem(QLatin1String(kService), objectPath,
                              QLatin1String(kFilesystem), QDBusConnection::systemBus());
    // Mounting can prompt through polkit and spin a disk up, so this one is allowed
    // longer than the read calls.
    filesystem.setTimeout(20000);

    const QDBusReply<QString> reply =
        filesystem.call(QStringLiteral("Mount"), QVariantMap());
    if (!reply.isValid()) {
        if (error)
            *error = reply.error().message();
        return {};
    }
    return reply.value();
}
