#include "mounts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QUrl>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include <dirent.h>
#include <unistd.h>

namespace {

// Filesystems that live on another machine. The tier of search, the thumbnail policy and
// the trash policy all hang off this (§10.6).
const QSet<QString> &networkTypes()
{
    static const QSet<QString> types = {
        QStringLiteral("nfs"),          QStringLiteral("nfs4"),
        QStringLiteral("cifs"),         QStringLiteral("smb3"),
        QStringLiteral("smbfs"),        QStringLiteral("afs"),
        QStringLiteral("ceph"),         QStringLiteral("glusterfs"),
        QStringLiteral("davfs"),        QStringLiteral("fuse.sshfs"),
        QStringLiteral("fuse.rclone"),  QStringLiteral("fuse.gvfsd-fuse"),
        QStringLiteral("fuse.davfs2"),  QStringLiteral("fuse.s3fs"),
        QStringLiteral("fuse.jmtpfs"),  QStringLiteral("fuse.curlftpfs"),
    };
    return types;
}

// Kernel bookkeeping, not places. Everything here would only clutter a sidebar.
bool isPseudoFs(const QString &fsType)
{
    static const QSet<QString> types = {
        QStringLiteral("proc"),      QStringLiteral("sysfs"),
        QStringLiteral("devtmpfs"),  QStringLiteral("devpts"),
        QStringLiteral("cgroup"),    QStringLiteral("cgroup2"),
        QStringLiteral("securityfs"),QStringLiteral("debugfs"),
        QStringLiteral("tracefs"),   QStringLiteral("configfs"),
        QStringLiteral("pstore"),    QStringLiteral("bpf"),
        QStringLiteral("autofs"),    QStringLiteral("mqueue"),
        QStringLiteral("hugetlbfs"), QStringLiteral("fusectl"),
        QStringLiteral("binfmt_misc"), QStringLiteral("efivarfs"),
        QStringLiteral("ramfs"),     QStringLiteral("squashfs"),
        QStringLiteral("nsfs"),      QStringLiteral("tmpfs"),
        QStringLiteral("fuse.portal"),
    };
    return types.contains(fsType);
}

// udisks2 mounts removable media under /run/media/$USER; older setups use /media.
bool looksRemovable(const QString &path)
{
    return path.startsWith(QLatin1String("/run/media/"))
        || path.startsWith(QLatin1String("/media/"));
}

// What gvfs currently holds, one entry per connected share.
//
// readdir and nothing else: no stat on the children. The listing itself is answered by
// gvfsd out of its own table, but stat-ing a share would reach for the far end, and a
// share whose server has gone away would then block the caller — which is the GUI thread.
QList<MountPoint> gvfsShares()
{
    QList<MountPoint> shares;

    DIR *dir = ::opendir(QFile::encodeName(Mounts::gvfsRoot()).constData());
    if (!dir)
        return shares;

    while (const dirent *entry = ::readdir(dir)) {
        const QString name = QFile::decodeName(entry->d_name);
        if (name == QLatin1String(".") || name == QLatin1String(".."))
            continue;

        MountPoint share;
        share.path = Mounts::gvfsRoot() + QLatin1Char('/') + name;
        share.source = name;
        share.fsType = QStringLiteral("fuse.gvfsd-fuse");
        share.isNetwork = true;
        share.displayName = Mounts::gvfsShareName(name);
        shares.append(share);
    }
    ::closedir(dir);
    return shares;
}

// mountinfo escapes these in paths, because a mount point may contain them.
QString unescapeField(const QString &field)
{
    QString out;
    out.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int code = QStringView(field).mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(QChar(code));
                i += 3;
                continue;
            }
        }
        out.append(field.at(i));
    }
    return out;
}

QString executable(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

} // namespace

QString MountPoint::label() const
{
    if (!displayName.isEmpty())
        return displayName;
    const QString name = path.section(QLatin1Char('/'), -1);
    return name.isEmpty() ? path : name;
}

namespace Mounts {

bool isNetworkFs(const QString &fsType)
{
    return networkTypes().contains(fsType);
}

// Format: id parent major:minor root mountpoint options [optional...] - fstype source super
// The optional fields are variable in number and terminated by a lone "-", which is why
// the separator has to be found rather than counted to.
QList<MountPoint> parseMountInfo(const QString &text)
{
    QList<MountPoint> mounts;

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const int separator = fields.indexOf(QStringLiteral("-"));
        if (separator < 0 || separator + 2 >= fields.size() || fields.size() < 5)
            continue;

        MountPoint mount;
        mount.path = unescapeField(fields.at(4));
        mount.fsType = fields.at(separator + 1);
        mount.source = unescapeField(fields.at(separator + 2));
        mount.isNetwork = isNetworkFs(mount.fsType);
        mount.isRemovable = looksRemovable(mount.path);
        mounts.append(mount);
    }

    return mounts;
}

QList<MountPoint> current()
{
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QList<MountPoint> interesting;
    const QList<MountPoint> all = parseMountInfo(QString::fromUtf8(file.readAll()));
    for (const MountPoint &mount : all) {
        if (isPseudoFs(mount.fsType))
            continue;

        // The gvfs mount is a container, not a place. It is present whenever gvfsd-fuse
        // is running and holds every share gvfs has as a subdirectory, so listing it
        // gives one permanent entry called "gvfs" that points at an empty directory when
        // nothing is connected, and still says only "gvfs" when something is. The shares
        // inside it are the places, so those get listed instead.
        if (mount.path == gvfsRoot()) {
            interesting.append(gvfsShares());
            continue;
        }

        // A place is somewhere that is not simply part of this machine's own disk: a
        // share, a stick, or something FUSE put there.
        if (mount.isNetwork || mount.isRemovable
            || mount.path.startsWith(runtimeMountRoot())) {
            interesting.append(mount);
        }
    }
    return interesting;
}

QString networkRootFor(const QString &path)
{
    QString best;
    const QList<MountPoint> mounts = current();
    for (const MountPoint &mount : mounts) {
        if (!mount.isNetwork)
            continue;
        if (path != mount.path && !path.startsWith(mount.path + QLatin1Char('/')))
            continue;
        // Mounts nest; the longest match is the one actually serving this path.
        if (mount.path.size() > best.size())
            best = mount.path;
    }
    return best;
}

QString sshHostFor(const QString &path)
{
    const QString root = runtimeMountRoot() + QLatin1Char('/');
    if (!path.startsWith(root))
        return {};

    // $XDG_RUNTIME_DIR/omafile/<host>/... — the first component is the alias we mounted.
    const QString rest = path.mid(root.size());
    const QString alias = rest.section(QLatin1Char('/'), 0, 0);
    if (alias.isEmpty() || alias == QLatin1String("rclone") || alias.startsWith(QLatin1Char('.')))
        return {};

    // Only claim it if it really is an sshfs mount right now.
    const QList<MountPoint> mounts = current();
    for (const MountPoint &mount : mounts) {
        if (mount.path == root + alias && mount.fsType == QLatin1String("fuse.sshfs"))
            return alias;
    }
    return {};
}

bool isOwnMountRoot(const QString &path)
{
    if (path.isEmpty() || !path.startsWith(runtimeMountRoot()))
        return false;
    const QList<MountPoint> mounts = current();
    for (const MountPoint &mount : mounts) {
        if (mount.path == path)
            return true;
    }
    return false;
}

QString gvfsRoot()
{
    QString base = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (base.isEmpty())
        base = QStringLiteral("/run/user/%1").arg(::getuid());
    return base + QStringLiteral("/gvfs");
}

bool isGvfsShare(const QString &path)
{
    const QString root = gvfsRoot() + QLatin1Char('/');
    return path.startsWith(root) && path.size() > root.size();
}

QString gvfsShareName(const QString &directoryName)
{
    // gvfs names a share by how to reach it: "smb-share:server=nas,share=media",
    // "sftp:host=example.com,user=chase", "dav:host=x,ssl=true". Everything after the
    // scheme is comma-separated key=value, percent-encoded.
    const int colon = directoryName.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return directoryName;

    QHash<QString, QString> fields;
    const QStringList pairs =
        directoryName.mid(colon + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const int equals = pair.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        fields.insert(pair.left(equals),
                      QUrl::fromPercentEncoding(pair.mid(equals + 1).toUtf8()));
    }

    // What it is, then where it is: "media on nas" reads better than either alone, and
    // is what the share is actually called in the places people already know.
    const QString what = fields.value(QStringLiteral("share"),
                                      fields.value(QStringLiteral("volume")));
    const QString where = fields.value(QStringLiteral("server"),
                                       fields.value(QStringLiteral("host")));

    if (!what.isEmpty() && !where.isEmpty())
        return QStringLiteral("%1 on %2").arg(what, where);
    if (!what.isEmpty())
        return what;
    if (!where.isEmpty())
        return where;
    return directoryName;
}

QString runtimeMountRoot()
{
    QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/%1").arg(::getuid());
    return runtime + QStringLiteral("/omafile");
}

bool hasSshfs()
{
    return !executable(QStringLiteral("sshfs")).isEmpty();
}

bool hasRclone()
{
    return !executable(QStringLiteral("rclone")).isEmpty();
}

bool hasGio()
{
    return !executable(QStringLiteral("gio")).isEmpty();
}

bool hasUdisks()
{
    return !executable(QStringLiteral("udisksctl")).isEmpty();
}

QStringList rcloneRemotes()
{
    if (!hasRclone())
        return {};

    QProcess process;
    process.setProgram(executable(QStringLiteral("rclone")));
    process.setArguments({ QStringLiteral("listremotes") });
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start();
    if (!process.waitForFinished(3000))
        return {};

    QStringList remotes;
    const QList<QByteArray> lines = process.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines) {
        QString remote = QString::fromUtf8(line).trimmed();
        if (remote.endsWith(QLatin1Char(':')))
            remote.chop(1);
        if (!remote.isEmpty())
            remotes.append(remote);
    }
    return remotes;
}

} // namespace Mounts
