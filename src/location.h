#pragma once

#include <QString>
#include <QStringList>

// A place omafile can browse. Local paths and remote URIs are the same type so that no
// caller has to care which it holds — the remote work in M4 slots in behind isRemote()
// rather than by threading a second path type through the model and the UI.
//
// v1 only *resolves* local paths; remote schemes parse and round-trip so the URI
// handling in §10.5 and its tests can exist before the backends do.
class Location
{
public:
    Location() = default;

    // Accepts "/home/x", "~/Projects", "file:///tmp", "ssh://host/srv", "smb://…",
    // "rclone:remote:path". A bare relative path resolves against `base`, or the
    // working directory when no base is given.
    static Location parse(const QString &input, const Location &base = Location());
    static Location fromLocalPath(const QString &path);
    static Location home();

    bool isValid() const { return !m_path.isEmpty(); }
    bool isLocal() const { return m_scheme == QLatin1String("file"); }
    bool isRemote() const { return isValid() && !isLocal(); }

    QString scheme() const { return m_scheme; }
    QString host() const { return m_host; }
    QString path() const { return m_path; }

    // The filesystem path, or empty when this location is not local.
    QString localPath() const { return isLocal() ? m_path : QString(); }

    // Canonical round-trippable form: "/home/x" for local, "ssh://host/srv" otherwise.
    QString toString() const;

    // For the UI: "~/Projects/omafile", or "host:/srv" for remote.
    QString displayPath() const;

    // Basename, or the host for the root of a remote location.
    QString displayName() const;

    // Breadcrumb pieces, left to right. The first is the root ("/", "~" or "host").
    QStringList segments() const;

    Location parent() const;
    Location child(const QString &name) const;

    // True when this is the topmost location for its scheme — no parent to go to.
    bool isRoot() const;

    bool operator==(const Location &other) const
    {
        return m_scheme == other.m_scheme && m_host == other.m_host && m_path == other.m_path;
    }
    bool operator!=(const Location &other) const { return !(*this == other); }

private:
    // Collapse "//", "/./" and trailing slashes without touching the filesystem.
    static QString normalize(const QString &path);

    QString m_scheme;
    QString m_host;
    QString m_path;
};
