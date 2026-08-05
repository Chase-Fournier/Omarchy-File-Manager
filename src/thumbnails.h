#pragma once

#include <QImage>
#include <QString>

// The freedesktop thumbnail spec (§11). The point of following it exactly is that omafile
// shares a cache with every other application on the desktop rather than building a
// private one: a thumbnail Nautilus or the file chooser already made is reused, and one
// omafile makes is reused by them.
//
//   $XDG_CACHE_HOME/thumbnails/{normal,large}/<md5 of the file:// URI>.png
//
// Validity is decided by the `Thumb::MTime` tag against the source file's mtime, so an
// edited file never shows a stale picture of itself.
namespace Thumbnails {

enum Size { Normal = 128, Large = 256 };

// Where this file's thumbnail lives, whether or not it exists yet.
QString cachePathFor(const QString &path, Size size = Normal);

// A cached thumbnail, or a null image when there is none or it is out of date.
QImage load(const QString &path, Size size = Normal);

// Writes a thumbnail with the tags the spec requires. Callers pass an already-scaled
// image; nothing here decodes.
bool store(const QString &path, const QImage &image, Size size = Normal);

// Scales for storage, preserving aspect ratio and never enlarging a small image.
QImage scaleForCache(const QImage &image, Size size = Normal);

} // namespace Thumbnails
