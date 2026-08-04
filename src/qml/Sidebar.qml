import QtQuick
import Omafile

// Hidden by default and toggled with Ctrl+B (§4). §16.4 asks whether it should exist at
// all given Ctrl+F and bookmarks-by-search; it is built here so that question can be
// answered by deleting it, not by imagining it.
Item {
    id: sidebar

    required property var app

    // Places that cannot be opened are still listed, with the reason — §10.1's
    // "degrade honestly" rather than hiding a host because sshfs is missing.
    ListView {
        id: list

        anchors.fill: parent
        anchors.topMargin: 4
        clip: true
        model: Places
        boundsBehavior: Flickable.StopAtBounds

        delegate: Item {
            id: place

            required property int index
            required property string name
            required property string glyph
            required property string note
            required property bool available
            required property bool mounted
            required property bool ejectable

            width: ListView.view.width
            height: app.rowHeight
            opacity: available ? 1.0 : 0.5

            Rectangle {
                anchors.fill: parent
                anchors.rightMargin: 8
                radius: 4
                color: hover.hovered ? app.selectionBg : "transparent"
                opacity: 0.6
            }

            HoverHandler { id: hover }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                onClicked: function (event) {
                    if (event.button === Qt.MiddleButton && place.ejectable)
                        Places.eject(place.index)
                    else
                        Places.activate(place.index)
                }
            }

            Text {
                id: placeGlyph

                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                // Collapses when there is no glyph, rather than reserving a column that
                // reads as a stripe of dead padding down the left edge.
                width: text.length > 0 ? app.fontSize * 1.6 : 0
                text: place.glyph
                color: place.mounted ? Theme.accent : Theme.dim
                font.family: app.monoFamily
                font.pixelSize: app.fontSize
            }

            Text {
                anchors.left: placeGlyph.right
                anchors.leftMargin: placeGlyph.width > 0 ? 8 : 0
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: place.note.length > 0 ? place.name + "  — " + place.note : place.name
                color: place.available ? Theme.fg : Theme.dim
                font.family: app.monoFamily
                font.pixelSize: app.fontSize - 1
            }
        }
    }

    // A quiet divider rather than a panel: the sidebar is a list, like everything else.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.muted
        opacity: 0.4
    }
}
