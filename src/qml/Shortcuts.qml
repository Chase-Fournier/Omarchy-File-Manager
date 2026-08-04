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
            enabled: !root.app.editingPath
            onActivated: modelData.action()
        }
    }
}
