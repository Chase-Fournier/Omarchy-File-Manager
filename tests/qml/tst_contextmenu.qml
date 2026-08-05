import QtQuick
import QtTest

// ContextMenu.qml, which has the worst bug history in the project: it once built, took
// focus and rendered at zero width because TextMetrics has no `implicitWidth` and the
// resulting NaN silently collapsed the panel. Nothing caught that but a screenshot.
//
// The component is exercised on its own with a stubbed `app`, so these tests do not need
// the model, the ops thread, or a real directory.
Item {
    id: harness

    width: 600
    height: 400

    // Everything ContextMenu reads off its `app`. Values are the real ones from Main.qml
    // so the sizes these tests assert are the sizes the app produces.
    QtObject {
        id: appStub

        readonly property string monoFamily: "monospace"
        readonly property int pad: 20
        readonly property int rowHeight: 32
        readonly property int fontSize: 14
        readonly property color panelBg: "#222222"
        readonly property color selectionBg: "#4444aa"
        readonly property color selectionText: "#ffffff"

        property int refocusCount: 0
        function refocus() { refocusCount = refocusCount + 1 }
    }

    // What a click that passed through the menu would land on.
    property int backgroundClicks: 0

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: harness.backgroundClicks = harness.backgroundClicks + 1
    }

    Loader {
        id: loader

        anchors.fill: parent

        // setSource with initial properties, not `source:` plus an assignment in
        // onLoaded: `app` is a required property, and a Loader refuses to build a
        // component whose required properties are not supplied up front — it fails
        // silently, leaving item null.
        Component.onCompleted: setSource("../../src/qml/ContextMenu.qml",
                                         { "app": appStub })
    }

    TestCase {
        id: testCase

        name: "ContextMenu"
        when: windowShown
        width: 600
        height: 400

        property var menu: loader.item

        function init() {
            verify(loader.status === Loader.Ready,
                   "ContextMenu.qml failed to load: " + loader.item)
            menu = loader.item
            menu.visible = false
            menu.items = []
            harness.backgroundClicks = 0
            appStub.refocusCount = 0
        }

        function fired(counter) {
            return { count: counter }
        }

        function sampleItems(record) {
            return [
                { label: "Open", action: function () { record.opened = true } },
                { label: "Open with…", enabled: false,
                  action: function () { record.openedWith = true } },
                { separator: true },
                { label: "Move to trash", action: function () { record.trashed = true } },
            ]
        }

        // The regression test for the invisible menu. A NaN width renders at zero with no
        // warning at all, so "it opened" is not the assertion — "it has a size" is.
        function test_opensWithARealSize() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))

            verify(menu.visible, "the menu should be visible after openAt")

            const panel = findPanel()
            verify(panel !== null, "the menu has no panel child")
            verify(panel.width > 0, "panel width is " + panel.width + " — NaN collapses to 0")
            verify(panel.height > 0, "panel height is " + panel.height)

            // Wide enough for the longest label, and tall enough for three rows plus a
            // separator — not merely non-zero.
            verify(panel.width >= 180, "panel is narrower than the minimum: " + panel.width)
            verify(panel.height > appStub.rowHeight * 2,
                   "panel is too short to hold its rows: " + panel.height)
        }

        // The clamp has to be a binding on the panel: at openAt time the panel has not
        // been laid out, so a calculation there reads a stale height and does nothing.
        function test_staysInsideTheWindow() {
            const record = {}
            menu.openAt(harness.width - 5, harness.height - 5, sampleItems(record))
            wait(0) // let the binding settle against the real size

            const panel = findPanel()
            verify(panel.x + panel.width <= harness.width,
                   "panel runs off the right edge: " + (panel.x + panel.width))
            verify(panel.y + panel.height <= harness.height,
                   "panel runs off the bottom edge: " + (panel.y + panel.height))
            verify(panel.x >= 0 && panel.y >= 0, "panel was clamped off the top or left")
        }

        function test_disabledEntriesDoNothingAndSeparatorsAreNotSelectable() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))

            // Index 1 is disabled, index 2 is a separator: neither may run anything, and
            // neither may close the menu out from under the pointer.
            menu.trigger(1)
            verify(!record.openedWith, "a disabled entry ran its action")
            verify(menu.visible, "a disabled entry closed the menu")

            menu.trigger(2)
            verify(menu.visible, "a separator closed the menu")

            menu.trigger(3)
            verify(record.trashed, "an enabled entry did not run its action")
            verify(!menu.visible, "the menu stayed open after an entry ran")
        }

        // Keyboard navigation must skip both, or Down lands on a separator and Enter
        // appears to do nothing.
        function test_keyboardSkipsSeparatorsAndDisabledEntries() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))

            compare(menu.hovered, 0, "the first enabled entry should start hovered")

            keyClick(Qt.Key_Down)
            compare(menu.hovered, 3, "Down should skip the disabled entry and the separator")

            keyClick(Qt.Key_Up)
            compare(menu.hovered, 0, "Up should come back past both")

            keyClick(Qt.Key_Return)
            verify(record.opened, "Enter did not run the hovered entry")
            verify(!menu.visible)
        }

        function test_escapeClosesAndHandsFocusBack() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))
            keyClick(Qt.Key_Escape)

            verify(!menu.visible, "Escape did not close the menu")
            compare(appStub.refocusCount, 1, "closing must hand focus back to the window")
        }

        // Dismissing does the click as well as closing it: the press is declined so
        // whatever is underneath still receives it. Eating the click costs two clicks for
        // every one meant.
        function test_clickingAwayClosesAndStillDoesTheClick() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))
            verify(menu.visible)

            // Far from the panel, which sits at the top-left.
            mouseClick(harness, harness.width - 20, harness.height - 20, Qt.LeftButton)

            verify(!menu.visible, "clicking away did not close the menu")
            compare(harness.backgroundClicks, 1,
                    "the click was swallowed instead of passing through")
        }

        // A click on the panel's own padding belongs to the menu, not to the list behind.
        function test_clickingThePanelDoesNotFallThrough() {
            const record = {}
            menu.openAt(10, 10, sampleItems(record))

            const panel = findPanel()
            // The bottom edge of the panel is its padding, below the last row.
            mouseClick(panel, panel.width / 2, panel.height - 3, Qt.LeftButton)

            compare(harness.backgroundClicks, 0,
                    "a click on the panel fell through to what was behind it")
        }

        function findPanel() {
            for (let i = 0; i < menu.children.length; ++i) {
                const child = menu.children[i]
                // The panel is the only Rectangle child; the dismiss MouseArea is the other.
                if (child.hasOwnProperty("radius"))
                    return child
            }
            return null
        }
    }
}
