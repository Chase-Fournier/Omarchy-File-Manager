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
    required property var matchPositions

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
        // The ListView is a Flickable and will otherwise steal the press to scroll,
        // which both turns a file drag into a scroll and kills the drag before it starts.
        // Dragging a row means dragging the file; the wheel scrolls.
        preventStealing: true

        property point pressPoint
        property bool dragStarted: false

        onPressed: function (event) {
            pressPoint = Qt.point(event.x, event.y)
            dragStarted = false
            app.prepareDrag(row.index, row.name, row.glyph)
        }
        onReleased: dragStarted = false

        // The drag has to begin from the press that is still held, which is why it lives
        // here rather than in a DragHandler: a handler alongside this MouseArea fights it
        // for the grab and neither ends up owning the press.
        onPositionChanged: function (event) {
            if (!pressed || dragStarted)
                return
            if (Math.abs(event.x - pressPoint.x) < 8 && Math.abs(event.y - pressPoint.y) < 8)
                return
            dragStarted = true
            app.requestDrag(row.index, row.name)
        }

        onClicked: function (event) {
            if (event.modifiers & Qt.ShiftModifier) {
                Dir.selectTo(row.index)
                return
            }
            if (event.modifiers & Qt.ControlModifier) {
                Dir.toggleSelection(row.index)
                Dir.currentIndex = row.index
                return
            }
            Dir.clearSelection()
            Dir.currentIndex = row.index
        }
        onDoubleClicked: Dir.activate(row.index)
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
        textFormat: row.matchPositions.length > 0 ? Text.StyledText : Text.PlainText
        text: row.matchPositions.length > 0
              ? app.highlight(row.name, row.matchPositions, Theme.accent)
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
