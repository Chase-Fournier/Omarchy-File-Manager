#include "places.h"

#include "hosts.h"
#include "location.h"
#include "mounts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>

namespace {

QString bookmarksFile()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/omafile");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/bookmarks");
}

QString runCommand(const QString &program, const QStringList &arguments, int timeoutMs,
                   QString *error)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(3000)) {
        *error = QStringLiteral("could not run %1").arg(program);
        return {};
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        *error = QStringLiteral("%1 timed out").arg(program);
        return {};
    }
    if (process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? QStringLiteral("%1 failed").arg(program)
                                      : stderrText.section(QLatin1Char('\n'), -1);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

bool isMountedAt(const QString &path)
{
    const QList<MountPoint> mounts = Mounts::current();
    for (const MountPoint &mount : mounts) {
        if (mount.path == path)
            return true;
    }
    return false;
}

} // namespace

Places::Places(QObject *parent)
    : QAbstractListModel(parent)
{
    QFile file(bookmarksFile());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (!line.isEmpty())
                m_bookmarks.append(line);
        }
    }
    rebuild();
}

Places::~Places()
{
    releaseMounts();
}

int Places::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_places.size());
}

QHash<int, QByteArray> Places::roleNames() const
{
    return {
        { NameRole, "name" },         { GlyphRole, "glyph" },
        { TargetRole, "target" },     { NoteRole, "note" },
        { AvailableRole, "available" }, { MountedRole, "mounted" },
        { EjectableRole, "ejectable" }, { KindRole, "kind" },
    };
}

QVariant Places::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_places.size())
        return {};

    const Place &place = m_places.at(index.row());
    switch (role) {
    case NameRole:
        return place.name;
    case GlyphRole:
        return place.glyph;
    case TargetRole:
        return place.target;
    case NoteRole:
        return place.note;
    case AvailableRole:
        return place.available;
    case MountedRole:
        return place.mounted;
    case EjectableRole:
        return place.ejectable;
    case KindRole:
        return int(place.kind);
    default:
        return {};
    }
}

void Places::refresh()
{
    beginResetModel();
    rebuild();
    endResetModel();
    emit countsChanged();
}

void Places::rebuild()
{
    m_places.clear();

    const auto folder = [this](QStandardPaths::StandardLocation location,
                               const QString &glyph) {
        const QString path = QStandardPaths::writableLocation(location);
        if (path.isEmpty() || !QFileInfo::exists(path))
            return;
        Place place;
        place.kind = Place::Folder;
        place.name = QFileInfo(path).fileName();
        place.glyph = glyph;
        place.target = path;
        m_places.append(place);
    };

    folder(QStandardPaths::HomeLocation, QStringLiteral("\uF015"));
    folder(QStandardPaths::DownloadLocation, QStringLiteral("\uF019"));
    folder(QStandardPaths::DocumentsLocation, QStringLiteral("\uF15C"));
    folder(QStandardPaths::PicturesLocation, QStringLiteral("\uF03E"));

    for (const QString &path : std::as_const(m_bookmarks)) {
        Place place;
        place.kind = Place::Bookmark;
        place.name = QFileInfo(path).fileName();
        if (place.name.isEmpty())
            place.name = path;
        place.glyph = QStringLiteral("\uF02E");
        place.target = path;
        place.available = QFileInfo::exists(path);
        if (!place.available)
            place.note = QStringLiteral("missing");
        m_places.append(place);
    }

    // Volumes: network shares and removable media, however they got mounted (§10.3/10.4).
    const QList<MountPoint> mounted = Mounts::current();
    QSet<QString> mountedPaths;
    for (const MountPoint &mount : mounted) {
        mountedPaths.insert(mount.path);
        Place place;
        place.kind = Place::Volume;
        place.name = mount.label();
        place.glyph = mount.isRemovable ? QStringLiteral("\uF287")
                                        : QStringLiteral("\uF0A0");
        place.target = mount.path;
        place.mounted = true;
        place.ejectable = mount.isRemovable || mount.isNetwork;
        m_places.append(place);
    }

    // SSH hosts, straight out of OpenSSH's own config (§10.1).
    const bool sshfs = Mounts::hasSshfs();
    const QList<SshHost> hosts = Hosts::all();
    for (const SshHost &host : hosts) {
        Place place;
        place.kind = Place::SshHost;
        place.name = host.alias;
        place.glyph = QStringLiteral("\uF233");
        place.target = host.alias;
        place.mountPath = Mounts::runtimeMountRoot() + QLatin1Char('/') + host.alias;
        place.mounted = mountedPaths.contains(place.mountPath);
        place.available = sshfs;
        // §10.1: degrade honestly — the host is still listed, it just says why it cannot
        // be opened rather than failing when clicked.
        if (!sshfs)
            place.note = QStringLiteral("install sshfs to browse");
        m_places.append(place);
    }

    // Cloud, via rclone's own configuration. omafile never configures rclone (§10.3).
    if (Mounts::hasRclone()) {
        const QStringList remotes = Mounts::rcloneRemotes();
        for (const QString &remote : remotes) {
            Place place;
            place.kind = Place::RcloneRemote;
            place.name = remote;
            place.glyph = QStringLiteral("\uF0C2");
            place.target = remote;
            place.mountPath = Mounts::runtimeMountRoot() + QStringLiteral("/rclone/") + remote;
            place.mounted = mountedPaths.contains(place.mountPath);
            m_places.append(place);
        }
    }
}

void Places::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

QString Places::refsDir(const QString &key)
{
    return Mounts::runtimeMountRoot() + QStringLiteral("/.refs/") + key;
}

void Places::claimMount(const QString &key)
{
    const QString dir = refsDir(key);
    QDir().mkpath(dir);
    QFile marker(dir + QStringLiteral("/%1").arg(::getpid()));
    if (marker.open(QIODevice::WriteOnly))
        marker.close();
    if (!m_heldMounts.contains(key))
        m_heldMounts.append(key);
}

bool Places::releaseMount(const QString &key)
{
    const QString dir = refsDir(key);
    QFile::remove(dir + QStringLiteral("/%1").arg(::getpid()));
    m_heldMounts.removeAll(key);

    // A claim from a process that no longer exists is stale — that is how an unclean
    // exit stops leaving an orphaned mount behind forever (§14).
    const QStringList claims = QDir(dir).entryList(QDir::Files);
    for (const QString &claim : claims) {
        const pid_t pid = claim.toInt();
        if (pid > 0 && ::kill(pid, 0) == 0)
            return false;
        QFile::remove(dir + QLatin1Char('/') + claim);
    }
    QDir().rmdir(dir);
    return true;
}

void Places::releaseMounts()
{
    const QStringList held = m_heldMounts;
    for (const QString &key : held) {
        if (!releaseMount(key))
            continue;

        const QString path = Mounts::runtimeMountRoot() + QLatin1Char('/') + key;
        if (!isMountedAt(path))
            continue;

        QProcess unmount;
        unmount.setProgram(QStringLiteral("fusermount3"));
        unmount.setArguments({ QStringLiteral("-u"), path });
        unmount.start();
        unmount.waitForFinished(3000);
    }
}

QString Places::mountSsh(const QString &hostAlias, QString *error)
{
    if (!Mounts::hasSshfs()) {
        *error = QStringLiteral("sshfs is not installed");
        return {};
    }

    const QString target = Mounts::runtimeMountRoot() + QLatin1Char('/') + hostAlias;
    if (isMountedAt(target)) {
        claimMount(hostAlias);
        return target;
    }
    QDir().mkpath(target);

    // The options from §10.1: survive a dropped link, notice a dead one within ~45 s,
    // and cache aggressively because every round trip is expensive.
    const QStringList options = {
        QStringLiteral("reconnect"),
        QStringLiteral("ServerAliveInterval=15"),
        QStringLiteral("ServerAliveCountMax=3"),
        QStringLiteral("cache_timeout=60"),
        QStringLiteral("kernel_cache"),
        QStringLiteral("compression=no"),
        QStringLiteral("idmap=user"),
    };

    runCommand(QStringLiteral("sshfs"),
               { hostAlias + QLatin1Char(':'), target, QStringLiteral("-o"),
                 options.join(QLatin1Char(',')) },
               20000, error);
    if (!error->isEmpty()) {
        QDir().rmdir(target);
        return {};
    }

    claimMount(hostAlias);
    return target;
}

QString Places::mountRclone(const QString &remote, QString *error)
{
    if (!Mounts::hasRclone()) {
        *error = QStringLiteral("run `rclone config` to set up cloud remotes");
        return {};
    }

    const QString key = QStringLiteral("rclone/") + remote;
    const QString target = Mounts::runtimeMountRoot() + QLatin1Char('/') + key;
    if (isMountedAt(target)) {
        claimMount(key);
        return target;
    }
    QDir().mkpath(target);

    runCommand(QStringLiteral("rclone"),
               { QStringLiteral("mount"), remote + QLatin1Char(':'), target,
                 QStringLiteral("--vfs-cache-mode"), QStringLiteral("writes"),
                 QStringLiteral("--dir-cache-time"), QStringLiteral("30s"),
                 QStringLiteral("--daemon") },
               20000, error);
    if (!error->isEmpty())
        return {};

    claimMount(key);
    return target;
}

// SMB, WebDAV and MTP all go through gio, which also owns the authentication prompt via
// the portal — which is why omafile stores no credentials at all (§10.7).
QString Places::mountGio(const QString &uri, QString *error)
{
    if (!Mounts::hasGio()) {
        *error = QStringLiteral("gio is not installed");
        return {};
    }

    QString mountError;
    runCommand(QStringLiteral("gio"), { QStringLiteral("mount"), uri }, 60000, &mountError);
    // "already mounted" is success as far as we are concerned.
    if (!mountError.isEmpty() && !mountError.contains(QStringLiteral("already mounted"))) {
        *error = mountError;
        return {};
    }

    // gio knows where it put it; asking is more reliable than reconstructing the gvfs
    // path from the URI.
    QString infoError;
    const QString resolved = runCommand(
        QStringLiteral("gio"),
        { QStringLiteral("info"), QStringLiteral("--attribute=standard::target-uri"), uri },
        10000, &infoError);

    const QString gvfs = QStringLiteral("/run/user/%1/gvfs").arg(::getuid());
    const QDir gvfsDir(gvfs);
    if (gvfsDir.exists()) {
        // The freshest entry under gvfs is the one just mounted.
        const QStringList entries = gvfsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Time);
        if (!entries.isEmpty())
            return gvfs + QLatin1Char('/') + entries.first();
    }

    if (!resolved.isEmpty())
        return resolved;

    *error = QStringLiteral("mounted, but could not find where");
    return {};
}

void Places::activate(int row)
{
    if (row < 0 || row >= m_places.size())
        return;

    const Place place = m_places.at(row);
    if (!place.available) {
        emit status(place.note.isEmpty() ? QStringLiteral("unavailable") : place.note);
        return;
    }

    if (place.kind == Place::Folder || place.kind == Place::Bookmark
        || place.kind == Place::Volume) {
        emit navigate(place.target);
        return;
    }

    setBusy(true);
    QString error;
    const QString path = place.kind == Place::SshHost ? mountSsh(place.target, &error)
                                                      : mountRclone(place.target, &error);
    setBusy(false);

    if (path.isEmpty()) {
        emit status(error);
        return;
    }

    refresh();
    emit navigate(path);
}

void Places::eject(int row)
{
    if (row < 0 || row >= m_places.size())
        return;

    const Place place = m_places.at(row);
    if (!place.ejectable && !place.mounted)
        return;

    const QString path = place.mountPath.isEmpty() ? place.target : place.mountPath;
    QString error;

    // FUSE mounts are ours to unmount; a udisks volume belongs to udisks.
    if (path.startsWith(Mounts::runtimeMountRoot())) {
        runCommand(QStringLiteral("fusermount3"), { QStringLiteral("-u"), path }, 10000,
                   &error);
    } else if (Mounts::hasUdisks()) {
        runCommand(QStringLiteral("udisksctl"),
                   { QStringLiteral("unmount"), QStringLiteral("-b"), path }, 15000, &error);
    } else {
        error = QStringLiteral("udisks2 is not installed");
    }

    emit status(error.isEmpty() ? QStringLiteral("Ejected %1").arg(place.name) : error);
    refresh();
}

void Places::addBookmark(const QString &path)
{
    if (path.isEmpty() || m_bookmarks.contains(path))
        return;
    m_bookmarks.append(path);

    QFile file(bookmarksFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        file.write(m_bookmarks.join(QLatin1Char('\n')).toUtf8() + '\n');

    refresh();
    emit status(QStringLiteral("Bookmarked %1").arg(QFileInfo(path).fileName()));
}

bool Places::isBookmarked(const QString &path) const
{
    return m_bookmarks.contains(path);
}

void Places::removeBookmark(const QString &path)
{
    if (!m_bookmarks.removeAll(path))
        return;

    QFile file(bookmarksFile());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        file.write(m_bookmarks.join(QLatin1Char('\n')).toUtf8() + '\n');

    refresh();
}

void Places::connectTo(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
        return;

    const Location location = Location::parse(trimmed);
    QString error;
    QString path;

    if (location.isLocal()) {
        // A bare word is far more likely to be a configured host than a relative path.
        const QList<SshHost> hosts = Hosts::all();
        for (const SshHost &host : hosts) {
            if (host.alias == trimmed) {
                setBusy(true);
                path = mountSsh(trimmed, &error);
                setBusy(false);
                break;
            }
        }
        if (path.isEmpty() && error.isEmpty()) {
            emit navigate(location.toString());
            return;
        }
    } else if (location.scheme() == QLatin1String("ssh")
               || location.scheme() == QLatin1String("sftp")) {
        setBusy(true);
        path = mountSsh(location.host(), &error);
        setBusy(false);
        if (!path.isEmpty() && location.path() != QLatin1String("/"))
            path += location.path();
    } else if (location.scheme() == QLatin1String("rclone")) {
        setBusy(true);
        path = mountRclone(location.host(), &error);
        setBusy(false);
        if (!path.isEmpty() && location.path() != QLatin1String("/"))
            path += location.path();
    } else {
        setBusy(true);
        path = mountGio(trimmed, &error);
        setBusy(false);
    }

    if (path.isEmpty()) {
        emit status(error.isEmpty() ? QStringLiteral("could not connect") : error);
        return;
    }

    refresh();
    emit navigate(path);
}

QStringList Places::completions() const
{
    QStringList out;
    const QList<SshHost> hosts = Hosts::all();
    for (const SshHost &host : hosts)
        out.append(host.alias);
    out += Mounts::rcloneRemotes();
    return out;
}
