import QtQuick
import Omafile

// One row: glyph, name, size, time. Also the drag source (§7) and the inline rename
// field (§9), both of which need to live on the row itself.
Item {
    id: row

    required property var app
    required property int index
    required property string name
    required property string glyph
    required property bool isDir
    required property bool isHidden
    required property bool isBroken
    required property bool isSelected
    required property string sizeText
    required property string timeText
    required property int matchStart
    required property int matchLength

    readonly property bool current: index === Dir.currentIndex
    readonly property bool renaming: app.renamingRow === index
    // The cursor row and explicitly selected rows are tinted the same; the cursor also
    // gets the brighter text, which is what distinguishes them.
    readonly property bool tinted: current || isSelected

    height: app.rowHeight

    Rectangle {
        anchors.fill: parent
        color: row.tinted ? app.selectionBg : "transparent"
        opacity: row.current ? 1.0 : 0.55
        radius: 4
    }

    // A thin accent bar marks explicit selection, so it reads differently from the
    // cursor merely resting on a row.
    Rectangle {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: 2
        height: parent.height - 8
        radius: 1
        visible: row.isSelected
        color: Theme.accent
    }

    MouseArea {
        id: mouse

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: function (event) {
            if (event.modifiers & Qt.ControlModifier)
                Dir.toggleSelection(row.index)
            else
                Dir.clearSelection()
            Dir.currentIndex = row.index
        }
        onDoubleClicked: Dir.activate(row.index)
    }

    // ── Drag out (§7) ────────────────────────────────────────────────
    // text/uri-list is what Chrome, Slack, GIMP and every GTK app read; text/plain is
    // for terminals and text fields. A multi-select drag carries every selected path.
    Item {
        id: dragSource

        anchors.fill: parent

        Drag.active: dragHandler.active
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.mimeData: {
            const paths = app.dragPaths(row.index)
            return {
                "text/uri-list": paths.map(p => "file://" + encodeURI(p)).join("\r\n"),
                "text/plain": paths.join("\n")
            }
        }

        DragHandler {
            id: dragHandler

            // Well above the click slop, so click-to-select never turns into a drag.
            dragThreshold: 8
            onActiveChanged: {
                if (active)
                    dragSource.Drag.start()
                else
                    dragSource.Drag.drop()
            }
        }
    }

    Text {
        id: glyphText

        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: app.fontSize * 1.6
        text: row.glyph
        color: row.current ? app.selectionText : Theme.dim
        opacity: row.isHidden ? 0.55 : 1.0
        font.family: app.monoFamily
        font.pixelSize: app.fontSize
    }

    Text {
        id: nameText

        anchors.left: glyphText.right
        anchors.leftMargin: 8
        anchors.right: sizeCol.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        visible: !row.renaming
        elide: Text.ElideMiddle
        opacity: row.isHidden ? 0.65 : 1.0
        color: row.isBroken ? Theme.error
             : row.current ? app.selectionText
             : Theme.fg
        font.family: app.monoFamily
        font.pixelSize: app.fontSize

        // Plain text unless there is a match to highlight — StyledText parses markup per
        // row, and an unfiltered list can be 100k rows.
        textFormat: row.matchStart >= 0 ? Text.StyledText : Text.PlainText
        text: row.matchStart >= 0
              ? app.highlight(row.name, row.matchStart, row.matchLength, Theme.accent)
              : row.name
    }

    // ── Inline rename (§9) ───────────────────────────────────────────
    TextInput {
        id: renameField

        anchors.left: glyphText.right
        anchors.leftMargin: 8
        anchors.right: sizeCol.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        visible: row.renaming
        color: acceptable ? Theme.fgBright : Theme.error
        // Not the row tint: on the row being renamed those are the same color, which
        // makes an active edit field indistinguishable from ordinary text.
        selectionColor: Theme.accent
        selectedTextColor: Theme.bg
        font.family: app.monoFamily
        font.pixelSize: app.fontSize

        // Validation tints the field rather than opening a dialog (§9).
        readonly property bool acceptable: text.length > 0 && !text.includes("/")

        onVisibleChanged: {
            if (!visible)
                return
            text = row.name
            forceActiveFocus()
            // Select the basename only, leaving the extension out — renaming a file
            // almost never means renaming its type.
            const dot = row.name.lastIndexOf(".")
            if (dot > 0)
                select(0, dot)
            else
                selectAll()
        }

        onAccepted: {
            if (acceptable)
                app.commitRename(row.index, text)
            else
                app.cancelRename()
        }
        Keys.onEscapePressed: app.cancelRename()

        // The one unambiguous signal that this row is being edited.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.bottom
            anchors.topMargin: 2
            height: 1
            color: parent.acceptable ? Theme.accent : Theme.error
        }
    }

    Text {
        id: sizeCol

        anchors.right: timeCol.left
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
        width: 70
        text: row.sizeText
        color: row.current ? app.selectionText : Theme.dim
        opacity: row.current ? 0.8 : 1.0
        font.family: app.monoFamily
        font.pixelSize: app.fontSize - 1
    }

    Text {
        id: timeCol

        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: Text.AlignRight
        width: 40
        text: row.timeText
        color: row.current ? app.selectionText : Theme.dim
        opacity: row.current ? 0.8 : 1.0
        font.family: app.monoFamily
        font.pixelSize: app.fontSize - 1
    }
}
