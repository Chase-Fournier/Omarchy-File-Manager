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

    // ── Verbs. Shortcuts.qml binds keys to these; nothing else drives the model. ──

    function moveBy(delta) { Dir.moveCurrent(delta) }
    function moveToEdge(last) { Dir.setCurrentToEdge(last) }
    function activateCurrent() { Dir.activate(Dir.currentIndex) }
    function activateCurrentInNewWindow() { Dir.activateInNewWindow(Dir.currentIndex) }
    function goUp() { Dir.goParent() }
    function toggleHidden() { Dir.showHidden = !Dir.showHidden }
    function refresh() { Dir.refresh() }
    function newWindow() { Dir.openNewWindowHere() }

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

    // Escape unwinds one layer at a time: filter, then selection, then the window (§5).
    // Not named `escape`: that is a JavaScript global and QML rejects it as a method.
    function escapePressed() {
        if (Dir.filter.length > 0)
            Dir.filter = ""
        else if (Dir.currentIndex >= 0)
            Dir.currentIndex = -1
        else
            root.close()
    }

    Shortcuts {
        app: root
    }

    // Bare letters type into the filter, so key handling cannot live in a Shortcut —
    // one would swallow every letter before the view ever saw it (§5).
    Item {
        id: keyboard

        anchors.fill: parent
        focus: true

        Keys.onPressed: function (event) {
            if (root.editingPath)
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

            const plainText = event.text.length > 0
                && event.text.charCodeAt(0) >= 0x20
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

                delegate: Item {
                    id: row

                    required property int index
                    required property string name
                    required property string glyph
                    required property bool isDir
                    required property bool isHidden
                    required property bool isBroken
                    required property string sizeText
                    required property string timeText
                    required property int matchStart
                    required property int matchLength

                    readonly property bool current: index === Dir.currentIndex

                    width: ListView.view.width
                    height: root.rowHeight

                    // Full-width tinted row, no borders between rows (§4).
                    Rectangle {
                        anchors.fill: parent
                        color: row.current ? root.selectionBg : "transparent"
                        radius: 4
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: Dir.currentIndex = row.index
                        onDoubleClicked: Dir.activate(row.index)
                    }

                    Text {
                        id: glyphText

                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.fontSize * 1.6
                        text: row.glyph
                        color: row.current ? root.selectionText : Theme.dim
                        opacity: row.isHidden ? 0.55 : 1.0
                        font.family: root.monoFamily
                        font.pixelSize: root.fontSize
                    }

                    Text {
                        id: nameText

                        anchors.left: glyphText.right
                        anchors.leftMargin: 8
                        anchors.right: sizeCol.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideMiddle
                        opacity: row.isHidden ? 0.65 : 1.0
                        color: row.isBroken ? Theme.error
                             : row.current ? root.selectionText
                             : Theme.fg
                        font.family: root.monoFamily
                        font.pixelSize: root.fontSize

                        // Plain text unless there is a match to highlight — StyledText
                        // parses markup per row, and an unfiltered list can be 100k rows.
                        textFormat: row.matchStart >= 0 ? Text.StyledText : Text.PlainText
                        text: row.matchStart >= 0
                              ? root.highlight(row.name, row.matchStart, row.matchLength,
                                               Theme.accent)
                              : row.name
                    }

                    Text {
                        id: sizeCol

                        anchors.right: timeCol.left
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        width: 70
                        text: row.sizeText
                        color: row.current ? root.selectionText : Theme.dim
                        opacity: row.current ? 0.8 : 1.0
                        font.family: root.monoFamily
                        font.pixelSize: root.fontSize - 1
                    }

                    Text {
                        id: timeCol

                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        width: 40
                        text: row.timeText
                        color: row.current ? root.selectionText : Theme.dim
                        opacity: row.current ? 0.8 : 1.0
                        font.family: root.monoFamily
                        font.pixelSize: root.fontSize - 1
                    }
                }

                // Empty and error states share the middle of the list area.
                Text {
                    anchors.centerIn: parent
                    width: parent.width - root.pad * 2
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
            }

            // ── Status bar ────────────────────────────────────────────────
            Item {
                id: statusBar

                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                height: root.rowHeight

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    color: Theme.dim
                    font.family: root.monoFamily
                    font.pixelSize: root.fontSize - 1
                    text: {
                        let parts = [Dir.count + (Dir.count === 1 ? " item" : " items")]
                        if (Dir.filter.length > 0)
                            parts.push("of " + Dir.totalCount)
                        if (Dir.currentName.length > 0)
                            parts.push(Dir.currentName)
                        if (Dir.currentSizeText.length > 0)
                            parts.push(Dir.currentSizeText)
                        return parts.join(" · ")
                    }
                }

                Text {
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
}
