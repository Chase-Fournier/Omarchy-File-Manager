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
                                          || help.visible || contextMenu.visible
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
    function showHelp() { help.open() }

    // ── Right-click menus ────────────────────────────────────────────
    // Every entry calls a verb that already exists for a shortcut, so the menu can never
    // do something the keyboard cannot.

    function menuForRow(index, x, y) {
        if (index !== Dir.currentIndex) {
            Dir.clearSelection()
            Dir.currentIndex = index
        }
        const isDir = Dir.rowIsDir(index)
        const remote = Ops.isRemote(Dir.rowPath(index))

        contextMenu.openAt(x, y, [
            { label: "Open", action: () => Dir.activate(index) },
            { label: "Open in new window", action: () => Dir.activateInNewWindow(index) },
            { label: "Open with…", enabled: !isDir, action: () => openWith() },
            { separator: true },
            { label: "Cut", action: () => cut() },
            { label: "Copy", action: () => copy() },
            { label: "Paste", enabled: isDir && Ops.canPaste,
              action: () => Ops.paste(Dir.rowPath(index)) },
            { label: "Copy path", action: () => copyPath() },
            { separator: true },
            { label: "Rename", action: () => beginRename() },
            { label: "Bulk rename in $EDITOR", action: () => bulkRename() },
            { separator: true },
            // §10.6: no dependable trash on a network mount, so do not offer it there.
            { label: "Move to trash", enabled: !remote, action: () => trash() },
            { label: "Delete permanently", action: () => confirmDelete() },
            { separator: true },
            { label: "Terminal here",
              action: () => Ops.openTerminal(isDir ? Dir.rowPath(index) : Dir.path) },
        ])
    }

    function menuForBlankSpace(x, y) {
        contextMenu.openAt(x, y, [
            { label: "New file", action: () => promptNewFile() },
            { label: "New folder", action: () => promptNewFolder() },
            { separator: true },
            { label: "Paste", enabled: Ops.canPaste, action: () => paste() },
            { label: "Select all", action: () => Dir.selectAll() },
            { separator: true },
            { label: Dir.showHidden ? "Hide hidden files" : "Show hidden files",
              action: () => toggleHidden() },
            { label: "Terminal here", action: () => openTerminal() },
            { label: "Refresh", action: () => refresh() },
        ])
    }

    // Right-clicking a place. The "config" entries open the file that actually governs
    // that place, rather than omafile inventing a settings screen for it (§1).
    function menuForPlace(index, kind, name, target, mounted, ejectable, x, y) {
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

        contextMenu.openAt(x, y, entries)
    }

    function promptNewFile() {
        overlay.mode = "text"
        overlay.label = "New file"
        overlay.initialText = ""
        overlay.secret = false
        overlay.stacked = false
        overlay.offerApplyToAll = false
        overlay.pending = "newFile"
        overlay.open()
    }
    function togglePreview() { Settings.preview = !Settings.preview }

    // Ctrl+Enter: pick the application rather than accepting xdg-open's default (§5).
    function openWith() {
        const paths = Dir.actionPaths()
        if (paths.length === 0)
            return
        const apps = Ops.handlersFor(paths[0])
        if (apps.length === 0) {
            Ops.reportStatus("nothing is registered to open that")
            return
        }

        // Letters rather than arrow keys, like every other choice in the app.
        const letters = "asdfghjkl"
        overlay.mode = "choice"
        overlay.secret = false
        overlay.label = "Open \"" + paths[0].split("/").pop() + "\" with"
        overlay.choices = apps.slice(0, letters.length).map(function (app, i) {
            return { key: letters[i], label: app.name, value: i }
        })
        overlay.stacked = true
        overlay.offerApplyToAll = false
        overlay.pending = "openWith"
        overlay.pendingApps = apps
        overlay.pendingPath = paths[0]
        overlay.open()
    }
    function bulkRename() { Ops.bulkRename(Dir.path, Dir.actionNames()) }

    // ── Places and remote (§10) ──────────────────────────────────────
    // Both remembered across sessions; see Settings for the precedence rules.
    readonly property bool sidebarVisible: Settings.sidebar

    function toggleSidebar() { Settings.sidebar = !Settings.sidebar }

    function bookmarkHere() {
        if (Places.isBookmarked(Dir.path))
            Places.removeBookmark(Dir.path)
        else
            Places.addBookmark(Dir.path)
    }

    function promptConnect() {
        overlay.mode = "text"
        overlay.label = "Connect to  —  ssh://host/path, smb://…, davs://…, "
                      + "rclone:remote:path, or a host from ~/.ssh/config"
        overlay.initialText = ""
        overlay.offerApplyToAll = false
        overlay.secret = false
        overlay.stacked = false
        overlay.pending = "connect"
        overlay.open()
    }

    function ejectHere() {
        // Ejecting acts on the place the current location belongs to, so it works from
        // inside the mount rather than only from the sidebar.
        for (let i = 0; i < Places.count; ++i) {
            const target = Places.data(Places.index(i, 0), 259) // TargetRole
            if (target.length > 0 && Dir.path.startsWith(target)) {
                Places.eject(i)
                return
            }
        }
    }

    // §10.6: no dependable trash on a network mount, so Delete there is the permanent
    // one — asked for explicitly instead of silently doing something different.
    function trashOrConfirm() {
        const paths = Dir.actionPaths()
        if (paths.length > 0 && Ops.isRemote(paths[0]))
            confirmDelete()
        else
            trash()
    }

    // ── Find (§6 tiers 2 and 3) ──────────────────────────────────────
    readonly property bool searching: Find.active

    function beginFind() {
        if (!Find.nameSearchAvailable) {
            Ops.copyPathToClipboard([])   // no-op; status line explains instead
            return
        }
        Dir.filter = ""
        Find.begin(Dir.path, 0)
    }

    function beginContentFind() {
        if (!Find.contentSearchAvailable)
            return
        Dir.filter = ""
        Find.begin(Dir.path, 1)
    }

    function endFind() { Find.end() }

    function activateSearchResult(index) {
        const target = Find.rowPath(index)
        if (target.length === 0)
            return

        if (Find.mode === 1) {
            // A content hit opens at its line; the search stays open so the next hit is
            // one keystroke away.
            Ops.openAtLine(target, Find.rowLine(index))
            return
        }

        if (Find.rowIsDir(index)) {
            Find.end()
            Dir.navigate(target)
            return
        }

        // A file: go to where it lives and put the cursor on it, rather than opening it
        // blind — finding something is usually the prelude to acting on it.
        Find.end()
        Dir.navigate(target.substring(0, target.lastIndexOf("/")))
        Dir.selectByName(target.substring(target.lastIndexOf("/") + 1))
    }

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

    // Bolds exactly the characters the scorer matched — which are scattered, not a
    // single run, now that the filter is fuzzy. Escaped by hand because the text is a
    // filename and StyledText would otherwise treat "&" or "<" as markup.
    function highlight(name, positions, color) {
        function escapeMarkup(text) {
            return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        }

        const marked = {}
        for (let i = 0; i < positions.length; ++i)
            marked[positions[i]] = true

        // Adjacent matches are wrapped as one span rather than one per character, which
        // keeps the markup short on long names.
        let out = ""
        let run = ""
        let inRun = false
        for (let c = 0; c <= name.length; ++c) {
            const hit = c < name.length && marked[c] === true
            if (hit !== inRun || c === name.length) {
                if (run.length > 0) {
                    out += inRun
                        ? '<font color="' + color + '"><b>' + escapeMarkup(run) + '</b></font>'
                        : escapeMarkup(run)
                }
                run = ""
                inRun = hit
            }
            if (c < name.length)
                run += name[c]
        }
        return out
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
        overlay.secret = false
        overlay.stacked = false
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
        overlay.secret = false
        overlay.stacked = false
        overlay.pending = "delete"
        overlay.open()
    }

    // Escape unwinds one layer at a time: filter, then selection, then the window (§5).
    // Not named `escape`: that is a JavaScript global and QML rejects it as a method.
    function escapePressed() {
        if (Find.active)
            endFind()
        else if (Ops.busy)
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

    Binding {
        target: Preview
        property: "enabled"
        value: Settings.preview
    }

    Shortcuts {
        id: shortcuts

        app: root
    }

    Connections {
        target: Places
        function onNavigate(location) { Dir.navigate(location) }
        function onStatus(message) { Ops.reportStatus(message) }

        // Nothing automatic worked. Rather than leaving a dead end, offer the terminal:
        // ssh can ask for anything there — a passphrase, a verification code, an unknown
        // host key — and omafile picks the mount up once it appears.
        function onConnectFailed(hostAlias, message) {
            overlay.mode = "choice"
            overlay.secret = false
            overlay.label = message + "\n\nOpen a terminal and connect to \"" + hostAlias
                          + "\" by hand?"
            overlay.choices = [{ key: "t", label: "Open terminal", value: 1 },
                               { key: "c", label: "Cancel", value: 0 }]
            overlay.offerApplyToAll = false
            overlay.stacked = false
        overlay.pending = "connectTerminal"
            overlay.pendingHost = hostAlias
            overlay.open()
        }

        // §10.7: prompted per session, held in memory only, never written anywhere.
        function onPasswordRequired(prompt) {
            overlay.mode = "text"
            overlay.label = prompt
            overlay.initialText = ""
            overlay.secret = true
            overlay.offerApplyToAll = false
            overlay.stacked = false
        overlay.pending = "password"
            overlay.open()
        }
    }

    Connections {
        target: Dir
        // §6: the warm cache is invalidated by the watcher, or a search would keep
        // answering from a tree that no longer exists.
        function onCountsChanged() { Find.invalidateCache() }
        function onLocationChanged() { Find.invalidateCache() }
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

            // Ctrl+? is matched here rather than as a Shortcut sequence because "?" needs
            // Shift on most layouts, so the event actually arrives as Ctrl+Shift+Question
            // — which matches neither QKeySequence("Ctrl+?") (Ctrl only) nor
            // QKeySequence("Ctrl+Shift+/") (which wants Key_Slash). Testing the key and
            // the Control modifier works on every layout.
            if ((event.modifiers & Qt.ControlModifier)
                && (event.key === Qt.Key_Question || event.key === Qt.Key_Slash)) {
                root.showHelp()
                event.accepted = true
                return
            }

            if (event.key === Qt.Key_Backspace) {
                // Backspace edits the query while one is being typed and only walks up
                // when there is nothing left to delete — otherwise typing a filter and
                // correcting a typo would throw you into the parent directory.
                if (Find.active)
                    Find.query = Find.query.slice(0, -1)
                else if (Dir.filter.length > 0)
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
                // Same key, two destinations: the recursive query while a search is open,
                // the in-directory filter otherwise.
                if (Find.active)
                    Find.query += event.text
                else
                    Dir.filter += event.text
                event.accepted = true
            }
        }

        Sidebar {
            id: sidebar

            app: root
            visible: root.sidebarVisible
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            // The same window padding as everything else, so the sidebar's rows indent
            // exactly like the file list's rows rather than sitting half a step further
            // out.
            anchors.margins: root.pad
            width: visible ? 240 : 0
        }

        Item {
            anchors.fill: parent
            anchors.margins: root.pad
            // A full pad of breathing room between the sidebar and the list, matching the
            // gap on the window's other three sides.
            anchors.leftMargin: root.pad
                                + (sidebar.visible ? sidebar.width + root.pad : 0)

            // ── Breadcrumb ────────────────────────────────────────────────
            Item {
                id: header

                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: root.rowHeight

                Row {
                    id: crumbs

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

                // The empty run to the right of the last crumb still *is* this directory,
                // so it offers the same menu the blank space below the list does — it is
                // the nearest thing to a title bar, and that is where a right-click looks
                // for "new file" when the list is full to the bottom.
                MouseArea {
                    anchors.left: crumbs.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    // Not while Ctrl+L has the path open for editing: the field covers
                    // this whole strip and a right-click there belongs to the field.
                    visible: !root.editingPath
                    acceptedButtons: Qt.RightButton
                    onClicked: function (event) {
                        const point = mapToItem(root.contentItem, event.x, event.y)
                        root.menuForBlankSpace(point.x, point.y)
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
            // One line serves both the in-directory filter and the recursive search;
            // the glyph is what says which is running.
            Text {
                id: filterLine

                anchors.top: header.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: visible ? root.rowHeight : 0
                visible: Dir.filter.length > 0 || Find.active
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: Theme.accent
                font.family: root.monoFamily
                font.pixelSize: root.fontSize
                text: {
                    if (!Find.active)
                        return "  " + Dir.filter
                    const glyph = Find.mode === 1 ? "  " : "  "
                    return glyph + Find.query + (Find.busy ? " …" : "")
                }
            }

            // ── List ──────────────────────────────────────────────────────
            // Behind the list on purpose: a row handles its own right-click, so only
            // genuinely empty space reaches this.
            MouseArea {
                anchors.fill: list
                acceptedButtons: Qt.RightButton
                onClicked: function (event) {
                    const point = mapToItem(root.contentItem, event.x, event.y)
                    root.menuForBlankSpace(point.x, point.y)
                }
            }

            ListView {
                id: list

                anchors.top: filterLine.bottom
                anchors.topMargin: root.pad / 2
                anchors.bottom: statusBar.top
                anchors.bottomMargin: root.pad / 2
                anchors.left: parent.left
                anchors.right: previewPane.visible ? previewPane.left : parent.right
                anchors.rightMargin: previewPane.visible ? root.pad : 0

                clip: true
                // Selection is driven by the model, not by the view's own key handling.
                keyNavigationEnabled: false
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: Find.active ? Find.currentIndex : Dir.currentIndex
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

                // Belt and braces: even if something re-announces the index without it
                // having changed, the view must not jump. Scrolling is the user's.
                property int lastScrolledTo: -1

                Connections {
                    target: Dir
                    function onCountsChanged() { list.reportVisible() }
                    function onCurrentIndexChanged() {
                        if (!Find.active && Dir.currentIndex >= 0
                            && Dir.currentIndex !== list.lastScrolledTo) {
                            list.lastScrolledTo = Dir.currentIndex
                            list.positionViewAtIndex(Dir.currentIndex, ListView.Contain)
                        }
                        Preview.show(Dir.currentIndex >= 0 ? Dir.rowPath(Dir.currentIndex) : "")
                    }
                }

                Connections {
                    target: Find
                    function onCurrentIndexChanged() {
                        if (Find.active && Find.currentIndex >= 0)
                            list.positionViewAtIndex(Find.currentIndex, ListView.Contain)
                    }
                }

                model: Find.active ? Find : Dir
                delegate: Find.active ? searchDelegate : entryDelegate

                Component {
                    id: entryDelegate

                    EntryRow {
                        app: root
                        width: ListView.view.width
                    }
                }

                Component {
                    id: searchDelegate

                    SearchRow {
                        app: root
                        width: ListView.view.width
                    }
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

            PreviewPane {
                id: previewPane

                app: root
                visible: Preview.enabled
                anchors.top: filterLine.bottom
                anchors.topMargin: root.pad / 2
                anchors.bottom: statusBar.top
                anchors.bottomMargin: root.pad / 2
                anchors.right: parent.right
                // §11: the right 40% of the window.
                width: visible ? parent.width * 0.4 : 0
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
                        if (Find.active) {
                            let found = [Find.count + (Find.count === 1 ? " result" : " results")]
                            if (Find.scanned > 0)
                                found.push("of " + Find.scanned + " scanned")
                            return found.join(" · ")
                        }

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
    // Declared after the list on purpose: QML paints in declaration order, so anything
    // that covers the content has to come after it.
    ContextMenu {
        id: contextMenu

        app: root
    }

    HelpOverlay {
        id: help

        app: root
        // Straight from the binding table, so the two can never disagree.
        table: shortcuts.table
    }

    Overlay {
        id: overlay

        app: root
        // Which caller opened it, so one overlay can serve several questions.
        property string pending: ""
        property string pendingHost: ""
        property var pendingApps: []
        property string pendingPath: ""

        onAccepted: function (text) {
            if (pending === "newFolder")
                Ops.newFolder(Dir.path, text)
            else if (pending === "newFile")
                Ops.newFile(Dir.path, text)
            else if (pending === "connect")
                Places.connectTo(text)
            else if (pending === "password")
                Places.providePassword(text)
        }
        onChose: function (value) {
            if (pending === "delete" && value === 1)
                Ops.deletePermanently(Dir.actionPaths())
            else if (pending === "connectTerminal" && value === 1)
                Places.connectInTerminal(pendingHost)
            else if (pending === "openWith" && value >= 0 && value < pendingApps.length)
                Ops.openWith(pendingApps[value].desktopFile, pendingPath)
        }
        onCancelled: {
            if (pending === "password")
                Places.cancelPassword()
        }
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
