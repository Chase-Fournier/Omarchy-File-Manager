import QtQuick
import QtTest

// Overlay.qml in its "progress" mode — the modal bar shown while an operation runs.
//
// Screenshotting this one is a race: bsdtar extracts eight thousand entries in about
// sixty milliseconds, so the window it is visible for is shorter than the shutter. The
// states are worth pinning anyway, because "determinate" and "indeterminate" are drawn by
// two different rectangles and only one of them may ever be on screen at a time.
Item {
    id: harness

    width: 700
    height: 400

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

    Loader {
        id: loader

        anchors.fill: parent
        Component.onCompleted: setSource("../../src/qml/Overlay.qml", { "app": appStub })
    }

    TestCase {
        id: testCase

        name: "OverlayProgress"
        when: windowShown
        width: 700
        height: 400

        property var overlay: null

        function init() {
            verify(loader.status === Loader.Ready, "Overlay.qml failed to load")
            overlay = loader.item
            overlay.mode = "progress"
            overlay.label = "Working…"
            overlay.detail = ""
            overlay.progress = 0
            overlay.visible = false
            appStub.refocusCount = 0
        }

        // Walks the tree for the two bar rectangles. They are siblings inside the track,
        // distinguished by which one the mode is meant to show.
        function barFills() {
            const found = []
            function walk(item) {
                for (let i = 0; i < item.children.length; ++i) {
                    const child = item.children[i]
                    // The fills are the only Rectangles with a colour and no children.
                    if (child.hasOwnProperty("radius") && child.children.length === 0)
                        found.push(child)
                    walk(child)
                }
            }
            walk(overlay)
            return found
        }

        function test_determinateFillTracksProgress() {
            overlay.progress = 0.5
            overlay.open()
            wait(0)

            const visibleFills = barFills().filter(function (r) { return r.visible && r.width > 0 })
            verify(visibleFills.length > 0, "no progress fill is drawn at all")

            // Something on screen is about half the panel's width — the fill, at 50%.
            const half = visibleFills.some(function (r) {
                return r.width > 100 && Math.abs(r.width / r.parent.width - 0.5) < 0.05
            })
            verify(half, "the determinate fill does not track progress")
        }

        // A total that was miscounted must not draw past the end of the track.
        function test_fillIsClampedToTheTrack() {
            overlay.progress = 3.0
            overlay.open()
            wait(0)

            const over = barFills().some(function (r) {
                return r.visible && r.parent && r.width > r.parent.width + 1
            })
            verify(!over, "a fill ran past the end of its track")
        }

        // Extraction cannot know its length without reading the archive twice, so it asks
        // for the travelling block instead. Exactly one of the two may be showing.
        function test_negativeProgressShowsTheIndeterminateBlock() {
            overlay.progress = -1
            overlay.open()
            wait(0)

            const fills = barFills().filter(function (r) { return r.visible && r.width > 0 })
            verify(fills.length > 0, "nothing is drawn for an indeterminate operation")

            // The travelling block is a fraction of the track, never the whole of it, so
            // it cannot be mistaken for "finished".
            const partial = fills.some(function (r) {
                return r.parent && r.width < r.parent.width * 0.9
            })
            verify(partial, "the indeterminate block fills the whole track")
        }

        function test_detailNamesWhatIsBeingWorkedOn() {
            overlay.detail = "bulk/file-30.bin"
            overlay.progress = 0.5
            overlay.open()
            wait(0)

            let seen = false
            function walk(item) {
                for (let i = 0; i < item.children.length; ++i) {
                    const child = item.children[i]
                    if (child.hasOwnProperty("text") && child.text === "bulk/file-30.bin"
                        && child.visible)
                        seen = true
                    walk(child)
                }
            }
            walk(overlay)
            verify(seen, "the entry being worked on is not shown")
        }

        // The whole point of the modal bar: Escape has to reach it, so the operation can
        // be called off without hunting for a button.
        function test_escapeAsksToCancel() {
            let cancelled = false
            overlay.cancelled.connect(function () { cancelled = true })

            overlay.progress = 0.25
            overlay.open()
            wait(50)
            keyClick(Qt.Key_Escape)

            verify(cancelled, "Escape did not cancel the operation")
        }
    }
}
