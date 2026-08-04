import QtQuick
import Omafile

// One window, one location (§1). No toolbar, no menu bar: a breadcrumb, a list, a
// status bar, and the keyboard.
Window {
    id: root

    width: 1200
    height: 800
    visible: true
    color: Theme.bg
    title: Dir.displayPath.length > 0 ? Dir.displayPath : "omafile"

    // Ships with Omarchy, matches the terminal, and carries the file-type glyphs.
    readonly property string monoFamily: "CaskaydiaMono Nerd Font"
    readonly property int pad: 20
    readonly property int rowHeight: 32
    readonly property int fontSize: 14

    property bool editingPath: false
    property int renamingRow: -1
    readonly property bool overlayActive: overlay.visible || Ops.conflictActive
    readonly property int pageStep: Math.max(1, Math.floor(list.height / rowHeight) - 1)

    function luma(c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b }

    // Themes disagree about what `selection` means. Quattro ships a subtle row tint
    // (#292e42 on tokyo-night); Omarchy 3.x derived it from selection_background, which
    // on the same theme is the accent blue — a full-bleed row of that reads as a
    // highlighter stripe, not as emphasis. Loud selections get blended back toward the
    // background; subtle ones are used as the theme intended.
    readonly property real selectionDistance: Math.abs(luma(Theme.selection) - luma(Theme.bg))
    readonly property real selectionMix: selectionDistance > 0.25 ? 0.35 : 1.0
    readonly property color selectionBg: Qt.rgba(
        Theme.bg.r + (Theme.selection.r - Theme.bg.r) * selectionMix,
        Theme.bg.g + (Theme.selection.g - Theme.bg.g) * selectionMix,
        Theme.bg.b + (Theme.selection.b - Theme.bg.b) * selectionMix, 1.0)
    readonly property color selectionText: luma(selectionBg) > 0.55 ? Theme.bg : Theme.fgBright

    // Same partial-theme problem as the selection: several themes resolve
    // lighter_background straight back to background, which would leave a modal panel
    // invisible against its own dimmed backdrop. Lift it toward the foreground instead.
    readonly property color panelBg: Math.abs(luma(Theme.bgLight) - luma(Theme.bg)) > 0.02
        ? Theme.bgLight
        : Qt.rgba(Theme.bg.r + (Theme.fg.r - Theme.bg.r) * 0.10,
                  Theme.bg.g + (Theme.fg.g - Theme.bg.g) * 0.10,
                  Theme.bg.b + (Theme.fg.b - Theme.bg.b) * 0.10, 1.0)

    // ── Verbs. Shortcuts.qml binds keys to these; nothing else drives the model. ──

    function moveBy(delta) { Dir.moveCurrent(delta) }
    function selectAll() { Dir.selectAll() }
    function moveToEdge(last) { Dir.setCurrentToEdge(last) }
    function activateCurrent() { Dir.activate(Dir.currentIndex) }
    function activateCurrentInNewWindow() { Dir.activateInNewWindow(Dir.currentIndex) }
    function goUp() { Dir.goParent() }
    function toggleHidden() { Dir.showHidden = !Dir.showHidden }
    function refresh() { Dir.refresh() }
    function newWindow() { Dir.openNewWindowHere() }
    function toggleSelection() { Dir.toggleSelection(Dir.currentIndex) }
    function extendSelection(delta) { Dir.extendSelection(delta) }
    function openTerminal() { Ops.openTerminal(Dir.path) }

    function copy() { Ops.copyToClipboard(Dir.actionPaths()) }
    function cut() { Ops.cut(Dir.actionPaths()) }
    function paste() { Ops.paste(Dir.path) }
    function copyPath() { Ops.copyPathToClipboard(Dir.actionPaths()) }
    function trash() { Ops.trash(Dir.actionPaths()) }

    // What a drag carries: the whole selection when the dragged row is part of it,
    // otherwise just that row (§7).
    function dragPaths(index, name) {
        if (Dir.selectionCount > 0 && Dir.actionNames().indexOf(name) >= 0)
            return Dir.actionPaths()
        return [Dir.rowPath(index)]
    }

    // ── Dragging out (§7) ────────────────────────────────────────────
    // The drag source and its image live here, at window level, not on the row. A
    // delegate can be destroyed while the drag is still running — the drag spins a
    // nested event loop, an internal drop refreshes the model, and the ListView then
    // frees the very item the loop returns into. That crashed the app on every internal
    // drag.
    property url dragImage
    property string dragLabel: ""
    property string dragGlyph: ""
    property int dragCount: 1

    // Called on press, because grabToImage needs a frame to complete and the drag itself
    // begins as soon as the move threshold is crossed.
    function prepareDrag(index, name, glyph) {
        dragLabel = name
        dragGlyph = glyph
        dragCount = (Dir.selectionCount > 0 && Dir.actionNames().indexOf(name) >= 0)
                    ? Dir.selectionCount : 1
        dragImage = ""
        // One turn later, and with no explicit size: the badge's width comes from a Text
        // that has not been laid out yet at this point, so grabbing inline captures a
        // stale, near-empty rectangle.
        Qt.callLater(function () {
            dragBadge.grabToImage(function (result) { root.dragImage = result.url },
                                  Qt.size(dragBadge.width, dragBadge.height))
        })
    }

    // Requested by a row, run by the window. The drag spins a nested event loop, and an
    // internal drop refreshes the model — so if this ran inside the row's own signal
    // handler, the ListView would destroy that row mid-handler and Qt aborts the process
    // ("Object destroyed while one of its QML signal handlers is in progress").
    property int pendingDragIndex: -1
    property string pendingDragName: ""

    function requestDrag(index, name) {
        pendingDragIndex = index
        pendingDragName = name
        Qt.callLater(runPendingDrag)
    }

    function runPendingDrag() {
        if (pendingDragIndex < 0)
            return
        const paths = dragPaths(pendingDragIndex, pendingDragName)
        pendingDragIndex = -1
        if (paths.length === 0)
            return

        dragProxy.Drag.mimeData = {
            "text/uri-list": Ops.uriList(paths),
            "text/plain": paths.join("\n")
        }
        if (dragImage != "")
            dragProxy.Drag.imageSource = dragImage

        // With Drag.Automatic this single assignment *performs* the drag: it blocks until
        // the drop resolves and resets itself to false afterwards. Calling startDrag() as
        // well only earns a "drag must be active" warning, because by then it is false
        // again.
        dragProxy.Drag.active = true
    }

    function refocus() {
        if (!editingPath && renamingRow < 0)
            keyboard.forceActiveFocus()
    }

    // Asking for the sort already in effect reverses it, which is the only way to get
    // largest-first or newest-first without spending another binding on it.
    function setSort(mode) {
        if (Dir.sortMode === mode)
            Dir.sortReversed = !Dir.sortReversed
        else
            Dir.sortMode = mode
    }

    // Wraps the matched run in the accent color. Escaped by hand because the text is a
    // filename and StyledText would otherwise treat "&" or "<" as markup.
    function highlight(name, start, length, color) {
        function escapeMarkup(text) {
            return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        }
        return escapeMarkup(name.substring(0, start))
             + '<font color="' + color + '"><b>'
             + escapeMarkup(name.substr(start, length))
             + '</b></font>'
             + escapeMarkup(name.substring(start + length))
    }

    function beginPathEdit() {
        pathField.text = Dir.path
        editingPath = true
        pathField.forceActiveFocus()
        pathField.selectAll()
    }

    function endPathEdit(commit) {
        if (commit && pathField.text.length > 0)
            Dir.navigate(pathField.text)
        editingPath = false
        keyboard.forceActiveFocus()
    }

    // ── Rename (§9) ──────────────────────────────────────────────────

    function beginRename() {
        if (Dir.currentIndex >= 0)
            renamingRow = Dir.currentIndex
    }

    function cancelRename() {
        renamingRow = -1
        keyboard.forceActiveFocus()
    }

    function commitRename(index, name) {
        Ops.rename(Dir.rowPath(index), name)
        cancelRename()
    }

    // ── Overlays ─────────────────────────────────────────────────────

    function promptNewFolder() {
        overlay.mode = "text"
        overlay.label = "New folder"
        overlay.initialText = "untitled"
        overlay.offerApplyToAll = false
        overlay.pending = "newFolder"
        overlay.open()
    }

    function confirmDelete() {
        const paths = Dir.actionPaths()
        if (paths.length === 0)
            return
        overlay.mode = "choice"
        overlay.label = "Delete " + paths.length + " item" + (paths.length === 1 ? "" : "s")
                      + " permanently? This cannot be undone."
        overlay.choices = [{ key: "d", label: "Delete", value: 1 },
                           { key: "c", label: "Cancel", value: 0 }]
        overlay.offerApplyToAll = false
        overlay.pending = "delete"
        overlay.open()
    }

    // Escape unwinds one layer at a time: filter, then selection, then the window (§5).
    // Not named `escape`: that is a JavaScript global and QML rejects it as a method.
    function escapePressed() {
        if (Ops.busy)
            Ops.cancel()
        else if (Dir.filter.length > 0)
            Dir.filter = ""
        else if (Dir.selectionCount > 0)
            Dir.clearSelection()
        else if (Dir.currentIndex >= 0)
            Dir.currentIndex = -1
        else
            root.close()
    }

    // §8: an operation in flight blocks window close rather than being detached.
    onClosing: function (close) {
        if (Ops.busy) {
            close.accepted = false
            Ops.cancel()
        }
    }

    Item {
        id: dragProxy

        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
    }

    // What the cursor carries. Composed off-screen and grabbed, rather than grabbing the
    // row itself: a row is the full width of the window, which made the drag image an
    // enormous bar spanning the screen.
    Item {
        id: dragBadge

        x: -10000
        // Sized from TextMetrics rather than from the Row's laid-out width: layout has
        // not run when the drag is prepared, so reading badgeContent.width there yields
        // an empty rectangle and the cursor carries a 24 px sliver.
        width: badgeMetrics.width + root.fontSize * 1.6 + 8 + 24
        height: root.rowHeight

        TextMetrics {
            id: badgeMetrics

            font.family: root.monoFamily
            font.pixelSize: root.fontSize
            text: root.dragCount > 1 ? root.dragCount + " items" : root.dragLabel
        }

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: root.panelBg
            border.width: 1
            border.color: Theme.accent
        }

        Row {
            id: badgeContent

            anchors.centerIn: parent
            spacing: 8

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.dragGlyph
                color: Theme.dim
                font.family: root.monoFamily
                font.pixelSize: root.fontSize
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: badgeMetrics.text
                color: Theme.fgBright
                font.family: root.monoFamily
                font.pixelSize: root.fontSize
            }
        }
    }

    Shortcuts {
        app: root
    }

    Connections {
        target: Ops
        // Refresh is cheap and the watcher may not have fired yet; selecting what was
        // just created is what makes "new folder then type a name" flow.
        function onCompleted(selectNames) {
            Dir.refresh()
            // Whatever the operation produced ends up selected and under the cursor, so a
            // drop or a paste can be acted on immediately without hunting for it.
            if (selectNames.length > 0)
                Dir.selectNames(selectNames)
        }
    }

    // Bare letters type into the filter, so key handling cannot live in a Shortcut —
    // one would swallow every letter before the view ever saw it (§5).
    Item {
        id: keyboard

        anchors.fill: parent
        focus: true

        Keys.onPressed: function (event) {
            if (root.editingPath || root.overlayActive || root.renamingRow >= 0)
                return

            if (event.key === Qt.Key_Escape) {
                root.escapePressed()
                event.accepted = true
                return
            }

            if (event.key === Qt.Key_Backspace) {
                // Backspace edits the filter while one is being typed and only walks up
                // when there is nothing left to delete — otherwise typing a filter and
                // correcting a typo would throw you into the parent directory.
                if (Dir.filter.length > 0)
                    Dir.filter = Dir.filter.slice(0, -1)
                else
                    root.goUp()
                event.accepted = true
                return
            }

            // Space is bound as a shortcut (add to selection), so it must not also type.
            const plainText = event.text.length > 0
                && event.text.charCodeAt(0) > 0x20
                && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            if (plainText) {
                Dir.filter += event.text
                event.accepted = true
            }
        }

        Item {
            anchors.fill: parent
            anchors.margins: root.pad

            // ── Breadcrumb ────────────────────────────────────────────────
            Item {
                id: header

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: root.rowHeight

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.editingPath
                    spacing: 0

                    Repeater {
                        model: Dir.segments

                        Row {
                            required property int index
                            required property string modelData
                            spacing: 0

                            Text {
                                text: " / "
                                visible: index > 0
                                color: Theme.dim
                                font.family: root.monoFamily
                                font.pixelSize: root.fontSize
                            }

                            Text {
                                text: modelData
                                color: index === Dir.segments.length - 1 ? Theme.fgBright : Theme.dim
                                font.family: root.monoFamily
                                font.pixelSize: root.fontSize

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Dir.navigateToSegment(index)
                                }
                            }
                        }
                    }
                }

                // Ctrl+L turns the breadcrumb into the same field it describes (§5).
                TextInput {
                    id: pathField

                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    visible: root.editingPath
                    color: Theme.fgBright
                    selectionColor: root.selectionBg
                    selectedTextColor: root.selectionText
                    font.family: root.monoFamily
                    font.pixelSize: root.fontSize

                    onAccepted: root.endPathEdit(true)
                    Keys.onEscapePressed: root.endPathEdit(false)
                }
            }

            // ── Filter line: absent entirely until something is typed ─────
            Text {
                id: filterLine

                anchors.top: header.bottom
                anchors.left: parent.left
                height: visible ? root.rowHeight : 0
                visible: Dir.filter.length > 0
                verticalAlignment: Text.AlignVCenter
                text: "  " + Dir.filter
                color: Theme.accent
                font.family: root.monoFamily
                font.pixelSize: root.fontSize
            }

            // ── List ──────────────────────────────────────────────────────
            ListView {
                id: list

                anchors.top: filterLine.bottom
                anchors.topMargin: root.pad / 2
                anchors.bottom: statusBar.top
                anchors.bottomMargin: root.pad / 2
                anchors.left: parent.left
                anchors.right: parent.right

                clip: true
                // Selection is driven by the model, not by the view's own key handling.
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: Dir.currentIndex
                cacheBuffer: root.rowHeight * 20

                // Only visible rows get stat'd (§12); this is what reports the window.
                function reportVisible() {
                    if (height <= 0)
                        return
                    const first = Math.floor(contentY / root.rowHeight)
                    const last = Math.ceil((contentY + height) / root.rowHeight)
                    Dir.ensureStats(first, last)
                }

                onContentYChanged: reportVisible()
                onHeightChanged: reportVisible()

                Connections {
                    target: Dir
                    function onCountsChanged() { list.reportVisible() }
                    function onCurrentIndexChanged() {
                        if (Dir.currentIndex >= 0)
                            list.positionViewAtIndex(Dir.currentIndex, ListView.Contain)
                    }
                }

                model: Dir

                delegate: EntryRow {
                    app: root
                    width: ListView.view.width
                }

            }

            // Empty and error states share the middle of the list area.
            Text {
                anchors.centerIn: list
                width: list.width - root.pad * 2
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: Dir.count === 0 && !Dir.loading
                text: Dir.error.length > 0 ? Dir.error
                    : Dir.filter.length > 0 ? "no match"
                    : "empty"
                color: Dir.error.length > 0 ? Theme.error : Theme.dim
                font.family: root.monoFamily
                font.pixelSize: root.fontSize
            }

            // ── Drop target (§7) ──────────────────────────────────────
            DropArea {
                id: dropArea

                // Anchored to the list, not parented inside it: a ListView is a Flickable,
                // so a declared child lands in the scrolling contentItem — which is sized
                // to the content and moves as you scroll, leaving the drop target
                // somewhere other than where the list appears.
                anchors.fill: list

                // The folder row currently under the cursor, or -1 for "this directory".
                property int targetRow: -1

                function rowAt(y) {
                    const index = Math.floor((list.contentY + y) / root.rowHeight)
                    if (index < 0 || index >= Dir.count)
                        return -1
                    return Dir.rowIsDir(index) ? index : -1
                }

                onPositionChanged: function (drag) {
                    const row = rowAt(drag.y)
                    if (row !== targetRow) {
                        targetRow = row
                        // Spring-load: hovering a folder for a moment navigates into
                        // it, the one genuinely great Finder behavior (§7).
                        springLoad.restart()
                    }
                }
                onExited: {
                    targetRow = -1
                    springLoad.stop()
                }

                onDropped: function (drop) {
                    springLoad.stop()

                    // Three ways in, because sources differ in what they actually
                    // publish: Qt-parsed urls, a raw uri-list Qt did not parse, or bare
                    // text. Falling through silently is how a drop "lights up" and then
                    // does nothing at all.
                    let uris = []
                    if (drop.hasUrls && drop.urls.length > 0) {
                        uris = drop.urls.map(u => u.toString())
                    } else if (drop.formats.indexOf("text/uri-list") >= 0) {
                        uris = drop.getDataAsString("text/uri-list")
                                   .split(/[\r\n]+/)
                                   .filter(line => line.length > 0 && line[0] !== "#")
                    } else if (drop.hasText) {
                        uris = drop.text.split(/[\r\n]+/).filter(line => line.length > 0)
                    }

                    // Ctrl forces copy, Shift forces move; otherwise move within a
                    // filesystem and copy across one (§7).
                    let action = 0
                    if (drop.keyboardModifiers & Qt.ControlModifier)
                        action = 1
                    else if (drop.keyboardModifiers & Qt.ShiftModifier)
                        action = 2

                    const destination = targetRow >= 0 ? Dir.rowPath(targetRow) : Dir.path
                    Ops.dropUris(uris, destination, action)
                    drop.acceptProposedAction()
                    targetRow = -1
                }

                Timer {
                    id: springLoad

                    interval: 300
                    onTriggered: {
                        if (dropArea.targetRow >= 0 && dropArea.containsDrag)
                            Dir.activate(dropArea.targetRow)
                    }
                }

                // Highlights the folder a drop would land in.
                Rectangle {
                    visible: dropArea.containsDrag
                    x: 0
                    width: dropArea.width
                    y: dropArea.targetRow >= 0
                       ? dropArea.targetRow * root.rowHeight - list.contentY
                       : 0
                    height: dropArea.targetRow >= 0 ? root.rowHeight : dropArea.height
                    radius: 4
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.accent
                    opacity: 0.8
                }
            }

            // ── Status bar ────────────────────────────────────────────────
            Item {
                id: statusBar

                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: root.rowHeight

                // A thin progress line, never a dialog (§8).
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    width: parent.width * Ops.progress
                    height: 2
                    visible: Ops.busy
                    color: Theme.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: sortText.left
                    anchors.rightMargin: 16
                    elide: Text.ElideRight
                    color: Ops.status.length > 0 ? Theme.fg : Theme.dim
                    font.family: root.monoFamily
                    font.pixelSize: root.fontSize - 1
                    text: {
                        if (Ops.busy)
                            return Ops.progressName.length > 0
                                 ? "… " + Ops.progressName : "working…"
                        if (Ops.status.length > 0)
                            return Ops.status

                        let parts = [Dir.count + (Dir.count === 1 ? " item" : " items")]
                        if (Dir.filter.length > 0)
                            parts.push("of " + Dir.totalCount)
                        if (Dir.selectionCount > 0)
                            parts.push(Dir.selectionCount + " selected")
                        else if (Dir.currentName.length > 0)
                            parts.push(Dir.currentName)
                        if (Dir.currentSizeText.length > 0 && Dir.selectionCount === 0)
                            parts.push(Dir.currentSizeText)
                        return parts.join(" · ")
                    }
                }

                Text {
                    id: sortText

                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    color: Theme.dim
                    font.family: root.monoFamily
                    font.pixelSize: root.fontSize - 1
                    text: {
                        const sort = Dir.sortMode === DirectoryModel.SortName ? "name"
                                   : Dir.sortMode === DirectoryModel.SortSize ? "size" : "time"
                        return (Dir.showHidden ? " · " : "")
                             + sort + (Dir.sortReversed ? " ▴" : " ▾")
                             + "    Ctrl+?"
                    }
                }
            }
        }
    }

    // ── Modal surfaces ───────────────────────────────────────────────
    Overlay {
        id: overlay

        app: root
        // Which caller opened it, so one overlay can serve several questions.
        property string pending: ""

        onAccepted: function (text) {
            if (pending === "newFolder")
                Ops.newFolder(Dir.path, text)
        }
        onChose: function (value) {
            if (pending === "delete" && value === 1)
                Ops.deletePermanently(Dir.actionPaths())
        }
        onCancelled: {}
    }

    // The conflict question is driven by the worker, not by a verb, so it lives outside
    // the shared overlay's caller bookkeeping (§8).
    Overlay {
        id: conflictOverlay

        app: root
        mode: "choice"
        offerApplyToAll: true
        label: "\"" + Ops.conflictName + "\" already exists.\nSuggested: "
               + Ops.conflictSuggestion
        choices: [{ key: "r", label: "Replace", value: 0 },
                  { key: "s", label: "Skip", value: 1 },
                  { key: "n", label: "Rename", value: 2 },
                  { key: "c", label: "Cancel", value: 3 }]

        onChose: function (value, all) { Ops.resolveConflict(value, all) }
        onCancelled: Ops.resolveConflict(3, false)

        Connections {
            target: Ops
            function onConflictChanged() {
                if (Ops.conflictActive)
                    conflictOverlay.open()
            }
        }
    }
}
