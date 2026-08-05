import QtQuick
import Omafile

// Where you are, and the several ways to leave: click a crumb to go there, drag onto one
// to move something there, or Ctrl+L to type a path into the same strip.
Item {
    id: breadcrumb

    // The window: its metrics, its drop handling, and its blank-space menu.
    required property var app

    // Ctrl+L turns this into a text field. Owned by the window, because Escape and the
    // keyboard focus chain are its business, not the breadcrumb's.
    property bool editing: false

    height: app.rowHeight

    // Driven from the window rather than reached into: `pathField` used to be poked at
    // from four places in Main.qml, which is exactly the coupling this file exists to end.
    function beginEditing(path) {
        field.text = path
        editing = true
        field.forceActiveFocus()
        field.selectAll()
    }

    function editedPath() {
        return field.text
    }

    Row {
        id: crumbs

        anchors.verticalCenter: parent.verticalCenter
        visible: !breadcrumb.editing
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
                    font.family: app.monoFamily
                    font.pixelSize: app.fontSize
                }

                Text {
                    id: crumb

                    text: modelData
                    color: index === Dir.segments.length - 1 ? Theme.fgBright : Theme.dim
                    font.family: app.monoFamily
                    font.pixelSize: app.fontSize

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Dir.navigateToSegment(index)
                    }

                    // The way back out. Spring-loading takes you *into* a folder mid-drag
                    // and there was nothing that took you out again, nor any way to reach
                    // a directory above the one in view — so the breadcrumb accepts a drag
                    // too: hover to go there, drop to drop there.
                    DropArea {
                        id: crumbDrop

                        // Wider than the text so a crumb is not a pixel-perfect target
                        // while something is held under the cursor — "~" is eight pixels
                        // of glyph. The horizontal reach covers the " / " between crumbs
                        // as well, so there is no dead gap to drop into by mistake.
                        anchors.fill: parent
                        anchors.leftMargin: -6
                        anchors.rightMargin: -6
                        anchors.topMargin: -4
                        anchors.bottomMargin: -4

                        onEntered: crumbSpring.restart()
                        onExited: crumbSpring.stop()
                        onDropped: function (drop) {
                            crumbSpring.stop()
                            app.acceptDrop(drop, Dir.segmentPath(index))
                        }

                        Timer {
                            id: crumbSpring

                            // Longer than the list's 300 ms: crumbs sit in a row, so
                            // several get crossed on the way to the one that was meant.
                            interval: 500
                            onTriggered: {
                                if (crumbDrop.containsDrag)
                                    Dir.navigateToSegment(index)
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: crumbDrop.containsDrag
                            radius: 4
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.accent
                            opacity: 0.8
                        }
                    }
                }
            }
        }
    }

    // The empty run to the right of the last crumb still *is* this directory, so it offers
    // the same menu the blank space below the list does — it is the nearest thing to a
    // title bar, and that is where a right-click looks for "new file" when the list is
    // full to the bottom.
    MouseArea {
        anchors.left: crumbs.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // Not while Ctrl+L has the path open for editing: the field covers this whole
        // strip and a right-click there belongs to the field.
        visible: !breadcrumb.editing
        acceptedButtons: Qt.RightButton
        onClicked: function (event) {
            const point = mapToItem(app.contentItem, event.x, event.y)
            app.menuForBlankSpace(point.x, point.y)
        }
    }

    // Ctrl+L turns the breadcrumb into the same field it describes (§5).
    TextInput {
        id: field

        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        visible: breadcrumb.editing
        color: Theme.fgBright
        selectionColor: app.selectionBg
        selectedTextColor: app.selectionText
        font.family: app.monoFamily
        font.pixelSize: app.fontSize

        onAccepted: app.endPathEdit(true)
        Keys.onEscapePressed: app.endPathEdit(false)
    }
}
