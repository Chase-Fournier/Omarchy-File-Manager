#pragma once

#include <QList>
#include <QString>

// A filesystem currently mounted. §10.3/§10.4: NFS, removable media and anything another
// application mounted need no code beyond noticing them.
struct MountPoint
{
    QString path;
    QString source;
    QString fsType;
    bool isNetwork = false;
    bool isRemovable = false;
    // Set for gvfs shares, whose directory name is a machine-readable description rather
    // than anything worth showing. Empty for everything else.
    QString displayName;

    // What the sidebar shows: the display name, or the last path component.
    QString label() const;
};

namespace Mounts {

// Split out from current() so it can be tested against captured mountinfo rather than
// whatever this machine happens to have mounted.
QList<MountPoint> parseMountInfo(const QString &text);

// Everything mounted right now, filtered to what a person would call a place: network
// shares, removable media and gvfs/rclone/sshfs mounts. Pseudo-filesystems are excluded.
QList<MountPoint> current();

bool isNetworkFs(const QString &fsType);

// The network mount `path` lives under, or empty when it is ordinary local storage.
// §10.6 wants every remote-specific behaviour keyed off one check rather than scattered
// `if`s, and this is it.
QString networkRootFor(const QString &path);

// For a path inside an sshfs mount omafile itself made, the host it came from. That is
// what allows a search to run on the far end instead of dragging the whole tree over
// the wire — §10.1 calls it the single biggest reason to build a remote path at all.
QString sshHostFor(const QString &path);

// True when `path` is the root of a mount omafile itself created. Navigating above one
// leads only into omafile's runtime plumbing, so it is treated as a boundary — the mount
// root *is* "the server".
bool isOwnMountRoot(const QString &path);

// Where omafile puts the mounts it owns: $XDG_RUNTIME_DIR/omafile/...
QString runtimeMountRoot();

// Where gvfs puts everything it has mounted: $XDG_RUNTIME_DIR/gvfs.
//
// This is a single FUSE mount that exists for as long as gvfsd-fuse is running — which,
// on a desktop with a portal, is always — and every share gvfs holds is a *subdirectory*
// of it rather than a mount of its own. So the mount itself is never a place: with
// nothing connected it is an empty directory, and with something connected it is a
// directory of them.
QString gvfsRoot();

// True for a path that is one of those subdirectories, which is what decides how it gets
// unmounted: gvfs shares answer to `gio mount -u`, not to fusermount3 or udisks.
bool isGvfsShare(const QString &path);

// A readable name for one of those subdirectories, whose real name looks like
// "smb-share:server=nas,share=media". Exposed so it can be tested against the real
// spellings without needing anything mounted.
QString gvfsShareName(const QString &directoryName);

// Soft dependencies (§2), detected once and used to gray out rather than to fail.
bool hasSshfs();
bool hasRclone();
bool hasGio();
bool hasUdisks();

// rclone's configured remotes, or empty when rclone is absent or unconfigured. omafile
// never configures rclone itself (§10.3).
QStringList rcloneRemotes();

} // namespace Mounts
