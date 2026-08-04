#pragma once

#include <QString>

// The freedesktop.org trash specification, implemented directly rather than delegated to
// gio — so that Nautilus and gio can restore what omafile trashed, and vice versa (§8).
//
// A file is trashed by moving it, never copying: the trash directory serving a path is
// always on that path's own filesystem, so the move is a rename(2) and cannot half-fail.
class Trash
{
public:
    // Everything the Journal needs to undo a trash operation.
    struct Item
    {
        QString originalPath; // where it came from
        QString trashedPath;  // where it lives now, inside files/
        QString infoPath;     // the matching .trashinfo

        bool isValid() const { return !trashedPath.isEmpty(); }
    };

    static bool moveToTrash(const QString &path, Item *item, QString *error);

    // Put a trashed file back where it came from, removing the .trashinfo.
    static bool restore(const Item &item, QString *error);

    // The trash directory serving `path`, created if needed. Empty on failure.
    //   same filesystem as $HOME -> $XDG_DATA_HOME/Trash
    //   anywhere else            -> $topdir/.Trash/$uid, or $topdir/.Trash-$uid
    static QString trashDirFor(const QString &path, QString *error = nullptr);

    static QString homeTrashDir();

    // The mount point `path` sits on, found by walking up until st_dev changes.
    static QString topDirFor(const QString &path);

private:
    // Reserves a free name inside the trash by creating its .trashinfo exclusively;
    // that file is the lock, which is what makes concurrent trashing safe.
    static bool reserveName(const QString &trashDir, const QString &baseName,
                            const QString &recordedPath, QString *chosenName,
                            QString *infoPath, QString *error);
};
