#include "places.h"

#include "hosts.h"
#include "location.h"
#include "mounts.h"
#include "opener.h"
#include "terminal.h"

#include <QTimer>

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
                   QString *error, const QString &stdinData = QString())
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!stdinData.isEmpty()) {
        process.write(stdinData.toUtf8());
        process.write("\n");
        // sshfs reads exactly one line and then expects the pipe to close.
        process.closeWriteChannel();
    }

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
        // The whole of stderr, not just its last line: ssh prints the diagnosis
        // ("Permission denied (publickey,password)") and sshfs then prints a generic
        // "read: Connection reset by peer" after it. Keeping only the last line threw
        // away the one part that explains what happened.
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? QStringLiteral("%1 failed").arg(program) : stderrText;
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
    sweepOrphanedMounts();
}

Places::~Places()
{
    releaseMounts();
}

int Places::rowCount(const QModelIndex &parent) const
{
    if (!m_built)
        const_cast<Places *>(this)->rebuild();
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
    m_built = true;
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

    // Pins. A file can be pinned as well as a folder, and the two behave differently when
    // clicked, so they are drawn differently: a pinned file carries the same generic file
    // glyph the list uses, and says what it will do before you click it.
    for (const QString &path : std::as_const(m_bookmarks)) {
        const QFileInfo info(path);

        Place place;
        place.kind = Place::Bookmark;
        place.name = info.fileName();
        if (place.name.isEmpty())
            place.name = path;
        place.glyph = info.isDir() ? QStringLiteral("\uF02E")   // pin
                                   : QStringLiteral("\uF15B");  // generic file
        place.target = path;
        place.available = info.exists();
        if (!place.available)
            place.note = QStringLiteral("missing");
        m_places.append(place);
    }

    // Volumes: network shares and removable media, however they got mounted (§10.3/10.4).
    const QList<MountPoint> mounted = Mounts::current();
    QSet<QString> mountedPaths;
    for (const MountPoint &mount : mounted) {
        mountedPaths.insert(mount.path);
        // A mount omafile made is already listed as the host or remote it came from,
        // and that entry lights up when it is mounted. Listing it again as a volume
        // showed the same machine twice.
        if (mount.path.startsWith(Mounts::runtimeMountRoot()))
            continue;

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

// A claim whose process is gone is stale. If every claim on a mount is stale, nobody is
// using it and it should not still be there — which is what stops a crash or a logout
// leaving an sshfs mount wedged in the runtime directory (§14).
void Places::sweepOrphanedMounts()
{
    const QString root = Mounts::runtimeMountRoot();
    const QDir refs(root + QStringLiteral("/.refs"));
    if (!refs.exists())
        return;

    const QStringList keys = refs.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &key : keys) {
        const QString dir = refsDir(key);
        bool live = false;
        const QStringList claims = QDir(dir).entryList(QDir::Files);
        for (const QString &claim : claims) {
            const pid_t pid = claim.toInt();
            if (pid > 0 && ::kill(pid, 0) == 0) {
                live = true;
                continue;
            }
            QFile::remove(dir + QLatin1Char('/') + claim);
        }
        if (live)
            continue;

        QDir().rmdir(dir);
        const QString path = root + QLatin1Char('/') + key;
        if (!isMountedAt(path))
            continue;

        QProcess unmount;
        unmount.setProgram(QStringLiteral("fusermount3"));
        unmount.setArguments({ QStringLiteral("-u"), path });
        unmount.start();
        unmount.waitForFinished(3000);
    }
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

bool Places::looksLikeAuthFailure(const QString &error)
{
    const QString lower = error.toLower();
    // Only genuine authentication signatures. A bare "connection reset by peer" is *not*
    // one of them: the probe has already ruled auth out by that point, so treating a
    // reset as an auth failure asked for a password the server does not even accept —
    // and then failed again with the identical message.
    return lower.contains(QStringLiteral("denied")) || lower.contains(QStringLiteral("authenticat"))
        || lower.contains(QStringLiteral("password"))
        || lower.contains(QStringLiteral("passphrase"));
}

// The most informative line to actually show someone, since `error` is now the whole of
// stderr rather than one line of it.
static QString bestErrorLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString lower = line.toLower();
        if (lower.contains(QStringLiteral("subsystem")))
            return QStringLiteral("the server has no sftp subsystem, so sshfs cannot "
                                  "mount it");
        if (lower.contains(QStringLiteral("denied")) || lower.contains(QStringLiteral("authenticat"))
            || lower.contains(QStringLiteral("not known")) || lower.contains(QStringLiteral("no such"))
            || lower.contains(QStringLiteral("timed out")) || lower.contains(QStringLiteral("refused"))
            || lower.contains(QStringLiteral("unreachable")))
            return line.trimmed();
    }
    return lines.isEmpty() ? QString() : lines.last().trimmed();
}

bool Places::hasGvfsSftp()
{
    return Mounts::hasGio()
        && QFileInfo::exists(QStringLiteral("/usr/share/gvfs/mounts/sftp.mount"));
}

// How Nautilus, Files and Thunar all do it. gvfs speaks sftp itself and owns the
// authentication dialogs, so a password, a key passphrase, an unknown host key and
// keyboard-interactive 2FA are all handled without omafile touching a credential (§10.7).
// sshfs cannot ask the user anything at all, which is why it was the wrong default.
QString Places::mountSftpViaGio(const SshHost &host, QString *error)
{
    // gvfs does not read ~/.ssh/config, so the alias has to be resolved to the real
    // host, user and port first — which is exactly what Hosts already parsed.
    QString uri = QStringLiteral("sftp://");
    if (!host.user.isEmpty())
        uri += host.user + QLatin1Char('@');
    uri += host.hostName;
    if (host.port != 22)
        uri += QLatin1Char(':') + QString::number(host.port);
    uri += QLatin1Char('/');

    const QString gvfs = QStringLiteral("/run/user/%1/gvfs").arg(::getuid());
    const QStringList before = QDir(gvfs).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    QString mountError;
    runCommand(QStringLiteral("gio"), { QStringLiteral("mount"), uri }, 120000, &mountError);
    if (!mountError.isEmpty() && !mountError.contains(QStringLiteral("already mounted"))) {
        *error = mountError;
        return {};
    }

    // Whatever appeared that was not there before is ours; comparing beats guessing at
    // gvfs' path-mangling rules.
    const QStringList after = QDir(gvfs).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : after) {
        if (!before.contains(entry))
            return gvfs + QLatin1Char('/') + entry;
    }
    for (const QString &entry : after) {
        if (entry.contains(QStringLiteral("sftp")) && entry.contains(host.hostName))
            return gvfs + QLatin1Char('/') + entry;
    }

    *error = QStringLiteral("mounted, but could not find where");
    return {};
}

SshHost Places::hostFor(const QString &alias)
{
    SshHost host;
    host.alias = alias;
    host.hostName = alias;
    const QList<SshHost> known = Hosts::all();
    for (const SshHost &candidate : known) {
        if (candidate.alias == alias)
            return candidate;
    }
    return host;
}

QString Places::mountSsh(const QString &hostAlias, const QString &password, QString *error)
{
    // Look the alias up once: what it carries decides which backend can even work.
    const SshHost host = hostFor(hostAlias);

    // Which backend depends on where the host came from. A host written down in
    // ~/.ssh/config may carry an IdentityFile, a ProxyJump or a Match block, none of
    // which gvfs can see — so those go through real ssh, which honours all of it. A host
    // known only from known_hosts (or typed as a URI) has no such configuration, and
    // there gvfs is better because it can actually ask for a password.
    if (password.isEmpty() && !host.needsOpenSsh() && hasGvfsSftp()) {
        QString gioError;
        const QString path = mountSftpViaGio(host, &gioError);
        if (!path.isEmpty())
            return path;
        *error = gioError;
        // Fall through to sshfs only if it is even available.
        if (!Mounts::hasSshfs())
            return {};
    }

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

    // Ask sftp what would happen before asking sshfs to do it. sshfs discards ssh's
    // stderr and reports every possible failure as "read: Connection reset by peer", so
    // an unresolvable host, a refused connection, a missing sftp subsystem and "this
    // server wants a password" are indistinguishable through sshfs alone.
    //
    // The probe is sftp rather than ssh because sshfs mounts over the *sftp subsystem*
    // (`-s sftp`). A locked-down box can accept `ssh host true` perfectly well and still
    // have no sftp-server, which is precisely the case that produced a bare "connection
    // reset by peer" and no explanation.
    if (password.isEmpty()) {
        QString probeError;
        runCommand(QStringLiteral("sftp"),
                   { QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                     QStringLiteral("-o"), QStringLiteral("ConnectTimeout=8"),
                     QStringLiteral("-b"), QStringLiteral("/dev/null"), hostAlias },
                   15000, &probeError);
        if (!probeError.isEmpty()) {
            *error = probeError;
            QDir().rmdir(target);
            return {};
        }
    }

    // The options from §10.1: survive a dropped link, notice a dead one within ~45 s,
    // and cache aggressively because every round trip is expensive.
    QStringList options = {
        QStringLiteral("reconnect"),
        QStringLiteral("ServerAliveInterval=15"),
        QStringLiteral("ServerAliveCountMax=3"),
        QStringLiteral("cache_timeout=60"),
        QStringLiteral("kernel_cache"),
        QStringLiteral("compression=no"),
        QStringLiteral("idmap=user"),
    };

    // Without a password, refuse to let ssh prompt: there is no terminal behind this, so
    // an interactive prompt would simply hang until the timeout and then fail with
    // nothing useful to say. BatchMode turns that into an immediate, legible refusal.
    if (password.isEmpty())
        options.append(QStringLiteral("BatchMode=yes"));
    else
        options.append(QStringLiteral("password_stdin"));

    // "host:/" — the remote *root*, not "host:" which is only the remote home. §10.1
    // specifies the root, and it matters twice over: you cannot navigate above the mount
    // point, and `ssh://host/etc/nginx` has to resolve to the real /etc/nginx rather than
    // to ~/etc/nginx.
    runCommand(QStringLiteral("sshfs"),
               { hostAlias + QStringLiteral(":/"), target, QStringLiteral("-o"),
                 options.join(QLatin1Char(',')) },
               20000, error, password);
    if (!error->isEmpty()) {
        QDir().rmdir(target);
        return {};
    }

    claimMount(hostAlias);
    return target;
}

// The mount is rooted at the remote /, so opening it lands on a filesystem root rather
// than anywhere useful. If the configured user's home is identifiable, start there — the
// rest of the machine is then simply *up*, which is what people expect.
QString Places::landingPathFor(const QString &mountPath, const SshHost &host)
{
    if (!host.user.isEmpty()) {
        const QString home = mountPath + QStringLiteral("/home/") + host.user;
        if (QFileInfo(home).isDir())
            return home;
        if (host.user == QLatin1String("root")
            && QFileInfo(mountPath + QStringLiteral("/root")).isDir())
            return mountPath + QStringLiteral("/root");
    }
    return mountPath;
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

    // A pinned file opens, because navigating to a file is not a thing you can do. This
    // is the same answer Enter gives it in the list, so a pin behaves like the row it
    // was made from.
    if (place.kind == Place::Bookmark && !QFileInfo(place.target).isDir()) {
        Opener::open(place.target);
        return;
    }

    if (place.kind == Place::Folder || place.kind == Place::Bookmark
        || place.kind == Place::Volume) {
        emit navigate(place.target);
        return;
    }

    setBusy(true);
    QString error;
    QString path = place.kind == Place::SshHost
        ? mountSsh(place.target, QString(), &error)
        : mountRclone(place.target, &error);
    if (!path.isEmpty() && place.kind == Place::SshHost)
        path = landingPathFor(path, hostFor(place.target));
    setBusy(false);

    if (path.isEmpty()) {
        // The agent and the keys were not enough, but a password might be.
        if (place.kind == Place::SshHost && looksLikeAuthFailure(error)) {
            m_pendingHost = place.target;
            m_pendingSubPath.clear();
            emit passwordRequired(QStringLiteral("Password for %1").arg(place.target));
            return;
        }
        emit connectFailed(place.target, bestErrorLine(error));
        return;
    }

    finishConnect(path);
}

void Places::finishConnect(const QString &mountPath)
{
    refresh();
    emit navigate(mountPath);
}

void Places::providePassword(const QString &password)
{
    if (m_pendingHost.isEmpty() || password.isEmpty()) {
        cancelPassword();
        return;
    }

    const QString host = m_pendingHost;
    const QString subPath = m_pendingSubPath;
    m_pendingHost.clear();
    m_pendingSubPath.clear();

    setBusy(true);
    QString error;
    QString path = mountSsh(host, password, &error);
    setBusy(false);
    // Nothing keeps a reference to the password past this point: it went to the child's
    // stdin and the local copy dies with this call (§10.7).

    if (path.isEmpty()) {
        emit status(error.isEmpty() ? QStringLiteral("could not connect")
                                    : bestErrorLine(error));
        return;
    }
    if (!subPath.isEmpty() && subPath != QLatin1String("/"))
        path += subPath;
    finishConnect(path);
}

// Hands the whole problem to a real terminal: ssh can then prompt for anything it likes
// — passphrase, verification code, an unknown host key — and the user answers it. Once
// the mount appears, omafile picks it up and navigates there.
void Places::connectInTerminal(const QString &hostAlias)
{
    if (!Mounts::hasSshfs()) {
        emit status(QStringLiteral("install sshfs to connect from a terminal"));
        return;
    }

    const QString target = Mounts::runtimeMountRoot() + QLatin1Char('/') + hostAlias;
    QDir().mkpath(target);

    // No BatchMode and no password_stdin: the entire point is that ssh may ask.
    const QString command =
        QStringLiteral("sshfs %1: %2 -o reconnect,ServerAliveInterval=15,"
                       "ServerAliveCountMax=3,cache_timeout=60,kernel_cache,"
                       "compression=no,idmap=user")
            .arg(Terminal::shellQuote(hostAlias), Terminal::shellQuote(target));

    // Held open on failure so the error is readable rather than flashing past.
    if (!Terminal::runHeld(command)) {
        emit status(QStringLiteral("no terminal found"));
        return;
    }

    emit status(QStringLiteral("connecting to %1 in a terminal…").arg(hostAlias));

    // Poll for the mount rather than trying to follow the detached process.
    m_terminalHost = hostAlias;
    if (!m_terminalWatch) {
        m_terminalWatch = new QTimer(this);
        m_terminalWatch->setInterval(1000);
        connect(m_terminalWatch, &QTimer::timeout, this, [this] {
            const QString path =
                Mounts::runtimeMountRoot() + QLatin1Char('/') + m_terminalHost;
            if (!isMountedAt(path))
                return;
            m_terminalWatch->stop();
            claimMount(m_terminalHost);
            finishConnect(path);
        });
    }
    m_terminalWatch->start();
    // Give up watching after two minutes rather than polling forever.
    QTimer::singleShot(120000, this, [this] {
        if (m_terminalWatch)
            m_terminalWatch->stop();
    });
}

void Places::cancelPassword()
{
    m_pendingHost.clear();
    m_pendingSubPath.clear();
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
    emit status(QStringLiteral("Pinned %1").arg(QFileInfo(path).fileName()));
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
    // Said out loud, like pinning is: unpinning from a row menu otherwise gives no sign
    // it happened at all when the sidebar is closed.
    emit status(QStringLiteral("Unpinned %1").arg(QFileInfo(path).fileName()));
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
                path = mountSsh(trimmed, QString(), &error);
                if (!path.isEmpty())
                    path = landingPathFor(path, host);
                setBusy(false);
                if (path.isEmpty() && looksLikeAuthFailure(error)) {
                    m_pendingHost = trimmed;
                    m_pendingSubPath.clear();
                    emit passwordRequired(QStringLiteral("Password for %1").arg(trimmed));
                    return;
                }
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
        path = mountSsh(location.host(), QString(), &error);
        setBusy(false);
        if (path.isEmpty() && looksLikeAuthFailure(error)) {
            m_pendingHost = location.host();
            m_pendingSubPath = location.path();
            emit passwordRequired(QStringLiteral("Password for %1").arg(location.host()));
            return;
        }
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
        emit status(error.isEmpty() ? QStringLiteral("could not connect")
                                    : bestErrorLine(error));
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
