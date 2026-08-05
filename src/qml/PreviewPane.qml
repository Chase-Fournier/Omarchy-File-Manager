import QtQuick
import Omafile

// §11: the right 40% of the window, off by default. Images scaled to fit, text as its
// first 200 lines in monospace with no syntax highlighting — dead simple is the point.
Item {
    id: pane

    required property var app

    // Bumped on every decode so the image is re-fetched rather than served from Qt's
    // pixmap cache under a name it has already seen.
    property int revision: 0

    Connections {
        target: Preview
        function onImageReady() { pane.revision++ }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.muted
        opacity: 0.4
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: app.pad
        anchors.bottomMargin: app.rowHeight

        Image {
            anchors.fill: parent
            visible: Preview.kind === Preview.Image
            source: Preview.kind === Preview.Image
                    ? "image://preview/" + pane.revision : ""
            fillMode: Image.PreserveAspectFit
            // The image is already scaled to fit by the decoder; asking for it again at
            // display size would only cost another copy.
            cache: false
            asynchronous: true
            smooth: true
        }

        Text {
            anchors.fill: parent
            visible: Preview.kind === Preview.Text
            text: Preview.text
            color: Theme.fg
            font.family: app.monoFamily
            font.pixelSize: app.fontSize - 2
            wrapMode: Text.NoWrap
            clip: true
            textFormat: Text.PlainText
        }

        Text {
            anchors.centerIn: parent
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: Preview.kind === Preview.Other || Preview.kind === Preview.Empty
            text: Preview.loading ? "…"
                 : Preview.kind === Preview.Other ? "no preview"
                 : ""
            color: Theme.dim
            font.family: app.monoFamily
            font.pixelSize: app.fontSize - 1
        }
    }

    // Size and dimensions, on the same line as the status bar so the pane itself stays
    // uncluttered.
    Text {
        anchors.left: parent.left
        anchors.leftMargin: app.pad
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        elide: Text.ElideRight
        text: Preview.detail
        color: Theme.dim
        font.family: app.monoFamily
        font.pixelSize: app.fontSize - 2
    }
}
