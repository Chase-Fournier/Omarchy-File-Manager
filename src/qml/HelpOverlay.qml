import QtQuick
import Omafile

// Ctrl+? — generated from Shortcuts.qml's own table, never hand-written (§5). The whole
// point is that the documentation cannot drift from the bindings: adding a shortcut adds
// a line here, and removing one removes it.
FocusScope {
    id: help

    required property var app
    required property var table

    anchors.fill: parent
    visible: false
    focus: visible

    function open() {
        visible = true
        Qt.callLater(function () { help.forceActiveFocus() })
    }

    function close() {
        visible = false
        app.refocus()
    }

    Keys.onPressed: function (event) {
        // Any key closes it: it is a reminder, not a mode to get stuck in.
        help.close()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        opacity: 0.97
        MouseArea {
            anchors.fill: parent
            onClicked: help.close()
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - app.pad * 4, 1000)
        spacing: app.pad

        Text {
            text: "Shortcuts"
            color: Theme.fgBright
            font.family: app.monoFamily
            font.pixelSize: app.fontSize + 3
        }

        // Two columns, because the table is longer than a window is tall.
        Row {
            width: parent.width
            spacing: app.pad * 2

            Repeater {
                model: 2

                Column {
                    required property int index

                    width: (parent.width - app.pad * 2) / 2
                    spacing: 3

                    Repeater {
                        // Split down the middle rather than alternating, so the reading
                        // order down each column matches the table.
                        model: {
                            const half = Math.ceil(help.table.length / 2)
                            return help.table.slice(index * half, (index + 1) * half)
                        }

                        Item {
                            required property var modelData

                            width: parent.width
                            height: app.fontSize + 8

                            Text {
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                width: 190
                                text: modelData.keys.join("  ")
                                elide: Text.ElideRight
                                color: Theme.accent
                                font.family: app.monoFamily
                                font.pixelSize: app.fontSize - 1
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 198
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                elide: Text.ElideRight
                                color: Theme.fg
                                font.family: app.monoFamily
                                font.pixelSize: app.fontSize - 1
                            }
                        }
                    }
                }
            }
        }

        Text {
            // The one binding that cannot be in the table: bare letters are handled in
            // Keys.onPressed, because a Shortcut would swallow every keystroke.
            text: "any letter   filter this directory        ·        any key closes this"
            color: Theme.dim
            font.family: app.monoFamily
            font.pixelSize: app.fontSize - 2
        }
    }
}
