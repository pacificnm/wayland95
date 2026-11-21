import QtQuick
import Win95Ui

// A page that scrolls as a whole (the reader, a help page, Settings): a Column of whatever is put in it, in a
// sunken white well (`well: true`) or on the dialog face, with a ScrollBar95 and the wheel. Children get
// `availableWidth` to size themselves.
Item {
    id: page
    default property alias content: col.data
    property bool well: true
    property real padding: 10
    property real spacing: 8
    readonly property real availableWidth: flick.width - padding * 2
    readonly property alias flickable: flick
    readonly property alias column: col

    function scrollTo(y) { flick.contentY = Math.max(0, Math.min(Math.max(0, flick.contentHeight - flick.height), y)); }
    function scrollToItem(item) { scrollTo(item.mapToItem(col, 0, 0).y + padding - 4); }

    Bevel {
        anchors.fill: parent
        visible: page.well
        style: "sunken"
        face: Theme.window
    }
    Flickable {
        id: flick
        x: page.well ? 2 : 0
        y: page.well ? 2 : 0
        width: parent.width - x * 2 - (bar.visible ? bar.width : 0)
        height: parent.height - y * 2
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: col.implicitHeight + page.padding * 2
        Column {
            id: col
            x: page.padding
            y: page.padding
            width: page.availableWidth
            spacing: page.spacing
        }
        WheelHandler {
            onWheel: (event) => page.scrollTo(flick.contentY - event.angleDelta.y / 120 * 48)
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        anchors.rightMargin: page.well ? 2 : 0
        y: flick.y
        height: flick.height
        flickable: flick
        stepSize: 16
    }
}
