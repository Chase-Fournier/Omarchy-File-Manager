import QtQuick
import Omafile

// The one modal surface in the app, used for the new-folder name, the permanent-delete
// confirmation and the conflict question (§8). Keyboard-first: every choice has a letter,
// Escape always cancels, and there is no mouse-only path through any of it.
FocusScope {
    id: overlay

    required property var app

    // "text" shows a single input; "choice" a row of labelled keys; "progress" a bar for
    // an operation already running, which the window cannot be used during.
    property string mode: "text"
    // 0..1 for a determinate bar. Negative means "working, length unknown".
    property real progress: 0
    // The entry currently being written, under the bar.
    property string detail: ""
    property string label: ""
    property string initialText: ""
    property var choices: []          // [{ key: "r", label: "Replace", value: 0 }, ...]
    // One choice per line. Short verb sets (Replace / Skip / Rename / Cancel) read well
    // side by side; application names do not, and used to run straight off the panel.
    property bool stacked: false
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

            // A determinate bar for an operation in flight. Modal on purpose: the window
            // cannot be used while it runs, so showing it here rather than as a hairline
            // in the status bar is what says so.
            Item {
                width: parent.width
                height: overlay.mode === "progress" ? bar.height + detailText.height + 8 : 0
                visible: overlay.mode === "progress"

                Rectangle {
                    id: bar

                    width: parent.width
                    height: 4
                    radius: 2
                    color: Theme.bg

                    // Determinate: how far along, when the total is known.
                    Rectangle {
                        // Clamped so a miscounted total cannot draw past the end of the
                        // track, and floored so the bar is visibly present at zero.
                        visible: overlay.progress >= 0
                        width: parent.width * Math.max(0, Math.min(1, overlay.progress))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent
                    }

                    // Indeterminate: a block that travels, for work whose length cannot be
                    // known without doing it twice. Extraction is the case — counting the
                    // entries first means decompressing the whole archive to throw the
                    // answer away, which on a solid format costs about as much again as
                    // the extraction itself.
                    Rectangle {
                        id: pulse

                        visible: overlay.progress < 0
                        width: parent.width * 0.25
                        height: parent.height
                        radius: parent.radius
                        color: Theme.accent

                        SequentialAnimation on x {
                            running: pulse.visible
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: bar.width - pulse.width
                                              duration: 900; easing.type: Easing.InOutQuad }
                            NumberAnimation { from: bar.width - pulse.width; to: 0
                                              duration: 900; easing.type: Easing.InOutQuad }
                        }
                    }
                }

                Text {
                    id: detailText

                    anchors.top: bar.bottom
                    anchors.topMargin: 8
                    width: parent.width
                    elide: Text.ElideMiddle
                    text: overlay.detail
                    color: Theme.dim
                    font.family: app.monoFamily
                    font.pixelSize: app.fontSize - 2
                }
            }

            Item {
                id: keys

                width: parent.width
                height: overlay.mode === "choice" ? choiceRow.height : 0
                visible: overlay.mode === "choice"
                // Also the key handler for "progress", which has no widgets of its own but
                // still has to hear Escape.
                focus: overlay.mode !== "text"

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

                // Flow, not Row: a Row cannot wrap, so a set of choices wider than the
                // panel simply ran off the edge with no way to see or reach the rest.
                Flow {
                    id: choiceRow

                    width: parent.width
                    spacing: 18

                    Repeater {
                        model: overlay.choices

                        Text {
                            required property var modelData

                            // Full width when stacked, so exactly one lands per line.
                            width: overlay.stacked ? choiceRow.width : implicitWidth
                            elide: Text.ElideRight
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
