import QtQuick
import Omafile

// Right-click menus, for people who want them. Built out of plain QtQuick rather than
// QtQuick.Controls: importing Controls loads its style plugin at startup, and §12's
// budget has no room to spare for a feature that is only reached on demand.
//
// Keyboard-navigable as well as clickable, because the rest of the app is.
FocusScope {
    id: menu

    required property var app

    // [{ label, action, enabled, separator }] — a separator entry needs nothing else.
    property var items: []

    anchors.fill: parent
    visible: false
    focus: visible

    property int hovered: -1
    property real originX: 0
    property real originY: 0

    function openAt(x, y, entries) {
        items = entries
        hovered = firstEnabled(0, 1)
        originX = x
        originY = y
        visible = true
        Qt.callLater(function () { menu.forceActiveFocus() })
    }

    function close() {
        visible = false
        app.refocus()
    }

    // Separators and disabled rows are skipped when moving with the keyboard.
    function firstEnabled(from, step) {
        for (let i = from; i >= 0 && i < items.length; i += step) {
            const item = items[i]
            if (!item.separator && item.enabled !== false)
                return i
        }
        return -1
    }

    function trigger(index) {
        if (index < 0 || index >= items.length)
            return
        const item = items[index]
        if (item.separator || item.enabled === false)
            return
        close()
        item.action()
    }

    Keys.onPressed: function (event) {
        if (event.key === Qt.Key_Escape) {
            close()
        } else if (event.key === Qt.Key_Down) {
            const next = firstEnabled(hovered + 1, 1)
            hovered = next >= 0 ? next : firstEnabled(0, 1)
        } else if (event.key === Qt.Key_Up) {
            const previous = firstEnabled(hovered - 1, -1)
            hovered = previous >= 0 ? previous : firstEnabled(items.length - 1, -1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            trigger(hovered)
        } else {
            close()
            return
        }
        event.accepted = true
    }

    // Clicking anywhere else dismisses — and the click still does whatever it came to do.
    // A menu that only eats the click costs two clicks for every one you meant, and the
    // second one is the price of a stray right-click, which should be free.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function (event) {
            menu.close()
            // Declining the press hands it on to whatever is underneath: a row selects, a
            // crumb navigates, and another right-click opens the menu there instead.
            event.accepted = false
        }
    }

    Rectangle {
        id: panel

        // Kept inside the window: a menu opened near the right or bottom edge flips back
        // rather than hanging off where it cannot be clicked. A binding rather than a
        // calculation in openAt() because the panel has not been laid out yet at that
        // point — its height would still read as empty and the clamp would do nothing.
        x: Math.max(0, Math.min(menu.originX, menu.width - width - 4))
        y: Math.max(0, Math.min(menu.originY, menu.height - height - 4))
        // TextMetrics measures with `width`; it has no implicitWidth, and reaching for one
        // yields undefined, which propagates to a NaN width and an invisible menu.
        width: Math.max(180, longest.width + app.pad * 2)
        height: column.implicitHeight + 10
        radius: 6
        color: app.panelBg
        border.width: 1
        border.color: Theme.muted

        // The panel's own padding and separators are still the menu. A click there should
        // do nothing at all rather than fall through to the list behind it, which is what
        // the pass-through above would otherwise give it.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
        }

        // Measures the widest label without laying it out, so the panel is exactly as
        // wide as it needs to be.
        TextMetrics {
            id: longest

            font.family: app.monoFamily
            font.pixelSize: app.fontSize - 1
            text: {
                let widest = ""
                for (let i = 0; i < menu.items.length; ++i) {
                    const label = menu.items[i].label || ""
                    if (label.length > widest.length)
                        widest = label
                }
                return widest
            }
        }

        Column {
            id: column

            y: 5
            width: parent.width

            Repeater {
                model: menu.items

                Item {
                    required property int index
                    required property var modelData

                    width: column.width
                    height: modelData.separator ? 7 : app.rowHeight - 6

                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - 16
                        height: 1
                        visible: modelData.separator === true
                        color: Theme.muted
                        opacity: 0.5
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 4
                        anchors.rightMargin: 4
                        radius: 4
                        visible: !modelData.separator && menu.hovered === index
                        color: app.selectionBg
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: app.pad
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !modelData.separator
                        elide: Text.ElideRight
                        text: modelData.label || ""
                        color: modelData.enabled === false ? Theme.dim
                             : menu.hovered === index ? app.selectionText
                             : Theme.fg
                        font.family: app.monoFamily
                        font.pixelSize: app.fontSize - 1
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: !modelData.separator && modelData.enabled !== false
                        hoverEnabled: true
                        onEntered: menu.hovered = index
                        onClicked: menu.trigger(index)
                    }
                }
            }
        }
    }
}
