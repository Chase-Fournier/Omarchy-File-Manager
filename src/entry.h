#pragma once

#include <QMetaType>
#include <QString>

// One directory entry. Deliberately cheap to copy in bulk: the Lister emits these in
// batches across a thread boundary on every directory change.
//
// The first listing pass fills in only `name` and `type`, both of which come straight
// out of getdents64 with no stat call. `size`/`mtime`/`mode` arrive later, for visible
// rows only — see DirectoryModel::ensureStats.
struct Entry
{
    enum Type : quint8 {
        Unknown = 0, // d_type was DT_UNKNOWN; the stat pass resolves it
        File,
        Directory,
        Symlink,
        Other, // fifo, socket, device...
    };

    QString name;
    Type type = Unknown;

    // Negative size means "not stat'd yet", which the UI renders as blank rather than 0.
    qint64 size = -1;
    qint64 mtime = 0;
    quint32 mode = 0;

    // For symlinks, what the link resolves to. Staying Unknown after a stat means broken.
    Type linkTarget = Unknown;
    bool statted = false;

    bool isHidden() const { return name.startsWith(QLatin1Char('.')); }

    // Symlinks to directories navigate like directories, so they sort with them too.
    bool isDir() const
    {
        return type == Directory || (type == Symlink && linkTarget == Directory);
    }

    bool isBrokenSymlink() const
    {
        return type == Symlink && statted && linkTarget == Unknown;
    }
};

Q_DECLARE_METATYPE(Entry)
