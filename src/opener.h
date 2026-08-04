#pragma once

#include <QString>

// Hands files off to the rest of the desktop. Static, so there is nothing to construct
// at startup — §12's "defer Opener until first use" comes for free.
//
// The "open with" list of parsed .desktop handlers arrives in M5; v1 delegates to
// xdg-open, which is already the user's configured choice.
namespace Opener {

// xdg-open, detached: omafile must not become the parent of a long-lived viewer.
void open(const QString &path);

// A second window is a second process (§3): no daemon, no IPC, no shared state.
void openInNewWindow(const QString &location);

} // namespace Opener
