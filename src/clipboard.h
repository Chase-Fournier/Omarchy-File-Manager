#pragma once

#include <QStringList>

// The system clipboard, in the formats other file managers actually read.
//
// `text/uri-list` is what every app understands, but it carries no notion of cut versus
// copy. GNOME's `x-special/gnome-copied-files` does, and Nautilus, Thunar, Dolphin and
// the GTK file chooser all speak it — so omafile writes both and prefers the GNOME form
// when reading. This is the whole reason Super+X into another file manager works.
namespace Clipboard {

void setPaths(const QStringList &paths, bool cut);

// Returns the paths on the clipboard; `cut` is set when they were cut rather than copied.
QStringList paths(bool *cut);

bool hasPaths();

// Ctrl+Shift+C: the absolute paths as plain text, newline separated.
void setText(const QString &text);

} // namespace Clipboard
