import QtQuick
import Omafile

// The one modal surface in the app, used for the new-folder name, the permanent-delete
// confirmation and the conflict question (§8). Keyboard-first: every choice has a letter,
// Escape always cancels, and there is no mouse-only path through any of it.
FocusScope {
    id: overlay

    required property var app

    // "text" shows a single input; "choice" shows a row of labelled keys.
    property string mode: "text"
    property string label: ""
    property string initialText: ""
    property var choices: []          // [{ key: "r", label: "Replace", value: 0 }, ...]
    property bool offerApplyToAll: false
    property bool applyToAll: false
    // Masks the field and skips the "looks like a filename" validation.
    property bool secret: false

    signal accepted(string text)
    signal chose(int value, bool all)
    signal cancelled()

    anchors.fill: parent
    visible: false
    // Without this the scope never becomes the window's focused child, so its TextInput
    // can hold focus locally and still never see a key event.
    focus: visible

    function open() {
        visible = true
        applyToAll = false
        if (mode === "text")
            field.text = initialText

        // Focus is taken on the next turn of the event loop, not inline: an item that
        // only just became visible cannot hold active focus yet, and grabbing it too
        // early leaves the field looking focused (selectAll still renders a selection)
        // while every keystroke goes to the window behind it.
        Qt.callLater(grabFocus)
    }

    function grabFocus() {
        if (!visible)
            return
        if (mode === "text") {
            field.forceActiveFocus()
            field.selectAll()
        } else {
            keys.forceActiveFocus()
        }
    }

    function close() {
        visible = false
        app.refocus()
    }

    // Swallows clicks and dims what is behind it, so the overlay reads as modal.
    Rectangle {
        anchors.fill: parent
        color: Theme.bg
        opacity: 0.82
        MouseArea {
            anchors.fill: parent
            onClicked: {} // absorb
        }
    }

    Rectangle {
        id: panel

        anchors.centerIn: parent
        width: Math.min(parent.width - app.pad * 4, 620)
        height: content.height + app.pad * 2
        radius: 6
        color: app.panelBg
        border.width: 1
        border.color: Theme.muted

        Column {
            id: content

            anchors.centerIn: parent
            width: parent.width - app.pad * 2
            spacing: 12

            Text {
                width: parent.width
                text: overlay.label
                wrapMode: Text.WordWrap
                color: Theme.fgBright
                font.family: app.monoFamily
                font.pixelSize: app.fontSize
            }

            TextInput {
                id: field

                width: parent.width
                visible: overlay.mode === "text"
                focus: overlay.mode === "text"
                color: acceptable ? Theme.fgBright : Theme.error
                selectionColor: app.selectionBg
                selectedTextColor: app.selectionText
                font.family: app.monoFamily
                font.pixelSize: app.fontSize

                echoMode: overlay.secret ? TextInput.Password : TextInput.Normal
                // A password may contain anything, including a slash.
                readonly property bool acceptable: overlay.secret
                                                   ? text.length > 0
                                                   : text.length > 0 && !text.includes("/")

                onAccepted: {
                    if (!acceptable)
                        return
                    overlay.accepted(text)
                    overlay.close()
                }
                Keys.onEscapePressed: {
                    overlay.cancelled()
                    overlay.close()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.bottom
                    anchors.topMargin: 4
                    height: 1
                    color: parent.acceptable ? Theme.accent : Theme.error
                }
            }

            Item {
                id: keys

                width: parent.width
                height: overlay.mode === "choice" ? choiceRow.height : 0
                visible: overlay.mode === "choice"
                focus: overlay.mode === "choice"

                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Escape) {
                        overlay.cancelled()
                        overlay.close()
                        event.accepted = true
                        return
                    }
                    if (overlay.offerApplyToAll && event.text.toLowerCase() === "a") {
                        overlay.applyToAll = !overlay.applyToAll
                        event.accepted = true
                        return
                    }
                    for (let i = 0; i < overlay.choices.length; ++i) {
                        if (event.text.toLowerCase() === overlay.choices[i].key) {
                            overlay.chose(overlay.choices[i].value, overlay.applyToAll)
                            overlay.close()
                            event.accepted = true
                            return
                        }
                    }
                }

                Row {
                    id: choiceRow

                    spacing: 18

                    Repeater {
                        model: overlay.choices

                        Text {
                            required property var modelData

                            text: "[" + modelData.key + "] " + modelData.label
                            color: Theme.fg
                            font.family: app.monoFamily
                            font.pixelSize: app.fontSize

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    overlay.chose(modelData.value, overlay.applyToAll)
                                    overlay.close()
                                }
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: overlay.offerApplyToAll
                text: (overlay.applyToAll ? "[a] ✓ " : "[a]   ") + "apply to all remaining"
                color: overlay.applyToAll ? Theme.accent : Theme.dim
                font.family: app.monoFamily
                font.pixelSize: app.fontSize - 1
            }

            Text {
                width: parent.width
                text: "Escape to cancel"
                color: Theme.dim
                font.family: app.monoFamily
                font.pixelSize: app.fontSize - 2
            }
        }
    }
}
