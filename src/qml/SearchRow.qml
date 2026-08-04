import QtQuick
import Omafile

// A search result. Shows the path relative to the search root, with the matched
// characters bolded, and for content search the line number and matching line.
Item {
    id: row

    required property var app
    required property int index
    required property string path
    required property string display
    required property var matchPositions
    required property int line
    required property string preview

    readonly property bool current: index === Find.currentIndex
    readonly property bool contentMode: Find.mode === 1

    height: contentMode ? app.rowHeight * 1.5 : app.rowHeight

    Rectangle {
        anchors.fill: parent
        anchors.rightMargin: 4
        color: row.current ? app.selectionBg : "transparent"
        radius: 4
    }

    MouseArea {
        anchors.fill: parent
        onClicked: Find.currentIndex = row.index
        onDoubleClicked: app.activateSearchResult(row.index)
    }

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Row {
            spacing: 8
            width: parent.width

            Text {
                id: pathText

                width: row.contentMode ? implicitWidth : parent.width - lineText.width - 8
                elide: Text.ElideLeft
                color: row.current ? app.selectionText : Theme.fg
                font.family: app.monoFamily
                font.pixelSize: app.fontSize

                textFormat: row.matchPositions.length > 0 ? Text.StyledText : Text.PlainText
                text: row.matchPositions.length > 0
                      ? app.highlight(row.display, row.matchPositions, Theme.accent)
                      : row.display
            }

            Text {
                id: lineText

                anchors.verticalCenter: parent.verticalCenter
                visible: row.line > 0
                text: ":" + row.line
                color: Theme.dim
                font.family: app.monoFamily
                font.pixelSize: app.fontSize - 1
            }
        }

        // The matching line itself, which is the whole point of a content search.
        Text {
            width: parent.width
            visible: row.contentMode && row.preview.length > 0
            text: row.preview
            elide: Text.ElideRight
            color: Theme.dim
            font.family: app.monoFamily
            font.pixelSize: app.fontSize - 2
        }
    }
}
