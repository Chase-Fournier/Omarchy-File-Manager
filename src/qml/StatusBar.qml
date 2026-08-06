import QtQuick
import Omafile

// The one line at the bottom: what is here, what is selected, what is running, and how
// the list is sorted. No buttons — §1 has no toolbar, and everything it reports is
// reachable from the keyboard.
Item {
    id: statusBar

    // The window, for its fonts and metrics.
    required property var app

    height: app.rowHeight

    // A thin progress line, never a dialog (§8).
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        // Negative means indeterminate; the modal bar shows that, this one just waits.
        width: parent.width * Math.max(0, Ops.progress)
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
        font.family: app.monoFamily
        font.pixelSize: app.fontSize - 1
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
        font.family: app.monoFamily
        font.pixelSize: app.fontSize - 1
        text: {
            const sort = Dir.sortMode === DirectoryModel.SortName ? "name"
                       : Dir.sortMode === DirectoryModel.SortSize ? "size" : "time"
            return (Dir.showHidden ? " · " : "")
                 + sort + (Dir.sortReversed ? " ▴" : " ▾")
                 + "    Ctrl+?"
        }
    }
}
