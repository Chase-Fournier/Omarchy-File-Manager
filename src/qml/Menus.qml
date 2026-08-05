import QtQuick
import Omafile

// What each right-click menu contains.
//
// Split out of Main.qml because it is the one part of the window that is pure data: no
// layout, no geometry, no focus. Every entry is `{ label, action, enabled, separator }`
// and every action is an arrow function onto a verb the window already implements, so
// there is exactly one implementation of each operation to keep correct.
//
// Most of those verbs are also bound to a key. Not all: "New file", the two config
// entries, "Connect in a terminal" and "Remove bookmark" live only here, because none of
// them has a natural shortcut and inventing one to preserve a symmetry nobody asked for
// would cost a key that a more common verb should have.
QtObject {
    id: menus

    // The window. Every action here is one of its verbs.
    required property var app

    function forRow(index, x, y) {
        if (index !== Dir.currentIndex) {
            Dir.clearSelection()
            Dir.currentIndex = index
        }
        const isDir = Dir.rowIsDir(index)
        const remote = Ops.isRemote(Dir.rowPath(index))

        return [
            { label: "Open", action: () => Dir.activate(index) },
            { label: "Open in new window", action: () => Dir.activateInNewWindow(index) },
            { label: "Open with…", enabled: !isDir, action: () => app.openWith() },
            { separator: true },
            { label: "Cut", action: () => app.cut() },
            { label: "Copy", action: () => app.copy() },
            { label: "Paste", enabled: isDir && Ops.canPaste,
              action: () => Ops.paste(Dir.rowPath(index)) },
            { label: "Copy path", action: () => app.copyPath() },
            { separator: true },
            { label: "Rename", action: () => app.beginRename() },
            { label: "Bulk rename in $EDITOR", action: () => app.bulkRename() },
            { separator: true },
            // §10.6: no dependable trash on a network mount, so do not offer it there.
            { label: "Move to trash", enabled: !remote, action: () => app.trash() },
            { label: "Delete permanently", action: () => app.confirmDelete() },
            { separator: true },
            { label: "Terminal here",
              action: () => Ops.openTerminal(isDir ? Dir.rowPath(index) : Dir.path) },
        ]
    }

    function forBlankSpace() {
        return [
            { label: "New file", action: () => app.promptNewFile() },
            { label: "New folder", action: () => app.promptNewFolder() },
            { separator: true },
            { label: "Paste", enabled: Ops.canPaste, action: () => app.paste() },
            { label: "Select all", action: () => Dir.selectAll() },
            { separator: true },
            { label: Dir.showHidden ? "Hide hidden files" : "Show hidden files",
              action: () => app.toggleHidden() },
            { label: "Terminal here", action: () => app.openTerminal() },
            { label: "Refresh", action: () => app.refresh() },
        ]
    }

    // Right-clicking a place. The "config" entries open the file that actually governs
    // that place, rather than omafile inventing a settings screen for it (§1).
    function forPlace(index, kind, name, target, mounted, ejectable) {
        const home = Dir.homePath
        let entries = [
            { label: "Open", action: () => Places.activate(index) },
            { label: "Open in new window", enabled: kind !== 2 && kind !== 3,
              action: () => Ops.openInNewWindow(target) },
            { separator: true },
        ]

        // 2 = SshHost, 3 = RcloneRemote, 4 = Volume, 1 = Bookmark (Place::Kind).
        if (kind === 2) {
            entries.push({ label: "Edit ~/.ssh/config",
                           action: () => Ops.openAtLine(home + "/.ssh/config", 1) })
            entries.push({ label: "Connect in a terminal",
                           action: () => Places.connectInTerminal(target) })
        } else if (kind === 3) {
            entries.push({ label: "Run rclone config",
                           action: () => Ops.runInTerminal("rclone config") })
        } else if (kind === 1) {
            entries.push({ label: "Remove bookmark",
                           action: () => Places.removeBookmark(target) })
        }

        if (mounted || ejectable)
            entries.push({ label: "Eject / unmount", action: () => Places.eject(index) })

        entries.push({ separator: true })
        entries.push({ label: "Edit omafile config",
                       action: () => Ops.openAtLine(Settings.configPath, 1) })

        return entries
    }
}
