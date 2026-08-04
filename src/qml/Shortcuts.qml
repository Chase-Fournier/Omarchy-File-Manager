import QtQml
import QtQuick
import Omafile

// The one place shortcuts are defined (§5). The bindings below and the Ctrl+? overlay
// that M5 adds both read this same table, so the documentation cannot drift from the
// behavior — the overlay will be generated from `table`, never hand-written.
//
// Bare letter keys are deliberately absent: they type into the filter, which Main.qml
// handles in Keys.onPressed because a Shortcut would swallow them.
Item {
    id: root

    // The window, which owns the verbs. Keeping actions as one-line calls means this
    // file stays a table rather than becoming a second controller.
    required property var app

    readonly property var table: [
        { keys: ["Down", "Ctrl+J"],   label: "Move down",            action: () => app.moveBy(1) },
        { keys: ["Up", "Ctrl+K"],     label: "Move up",              action: () => app.moveBy(-1) },
        { keys: ["PgDown"],           label: "Page down",            action: () => app.moveBy(app.pageStep) },
        { keys: ["PgUp"],             label: "Page up",              action: () => app.moveBy(-app.pageStep) },
        { keys: ["Home"],             label: "First item",           action: () => app.moveToEdge(false) },
        { keys: ["End"],              label: "Last item",            action: () => app.moveToEdge(true) },

        { keys: ["Right", "Return", "Enter"], label: "Open",         action: () => app.activateCurrent() },
        { keys: ["Shift+Return", "Shift+Enter"], label: "Open in new window", action: () => app.activateCurrentInNewWindow() },
        { keys: ["Left"],             label: "Parent directory",     action: () => app.goUp() },

        { keys: ["Space"],            label: "Add to selection",     action: () => app.toggleSelection() },
        { keys: ["Shift+Down"],       label: "Extend selection down", action: () => app.extendSelection(1) },
        { keys: ["Shift+Up"],         label: "Extend selection up",  action: () => app.extendSelection(-1) },
        { keys: ["Ctrl+A"],           label: "Select all",           action: () => Dir.selectAll() },

        // Super+C/X/V is the Omarchy convention Nautilus cannot do (§1); Ctrl is kept
        // alongside it for muscle memory, exactly as §5 specifies.
        { keys: ["Meta+C", "Ctrl+C"], label: "Copy",                 action: () => app.copy() },
        { keys: ["Meta+X", "Ctrl+X"], label: "Cut",                  action: () => app.cut() },
        { keys: ["Meta+V", "Ctrl+V"], label: "Paste",                action: () => app.paste() },
        { keys: ["Ctrl+Shift+C"],     label: "Copy absolute path",   action: () => app.copyPath() },

        { keys: ["F2", "Ctrl+R"],     label: "Rename",               action: () => app.beginRename() },
        { keys: ["Delete"],           label: "Move to trash",        action: () => app.trash() },
        { keys: ["Shift+Delete"],     label: "Delete permanently",   action: () => app.confirmDelete() },
        { keys: ["Ctrl+Z"],           label: "Undo",                 action: () => Ops.undo() },
        { keys: ["Ctrl+Shift+N"],     label: "New folder",           action: () => app.promptNewFolder() },

        { keys: ["Ctrl+T"],           label: "Terminal here",        action: () => app.openTerminal() },
        { keys: ["Ctrl+L"],           label: "Edit path",            action: () => app.beginPathEdit() },
        { keys: ["Ctrl+H"],           label: "Toggle hidden files",  action: () => app.toggleHidden() },
        { keys: ["Ctrl+N"],           label: "New window",           action: () => app.newWindow() },
        { keys: ["F5"],               label: "Refresh",              action: () => app.refresh() },

        // Sorting has no assignment in §5, so it uses digits rather than squatting on a
        // key reserved for a later milestone. Repeating the current mode reverses it.
        { keys: ["Ctrl+1"],           label: "Sort by name",         action: () => app.setSort(DirectoryModel.SortName) },
        { keys: ["Ctrl+2"],           label: "Sort by size",         action: () => app.setSort(DirectoryModel.SortSize) },
        { keys: ["Ctrl+3"],           label: "Sort by time",         action: () => app.setSort(DirectoryModel.SortTime) },
    ]

    Instantiator {
        model: root.table
        delegate: Shortcut {
            required property var modelData
            sequences: modelData.keys
            // Anything modal owns the keyboard while it is up.
            enabled: !root.app.editingPath && !root.app.overlayActive
                     && root.app.renamingRow < 0
            onActivated: modelData.action()
        }
    }
}
