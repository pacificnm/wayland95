import QtQuick
import Win95Ui

// A scrolling page of sections (upstream's .pane-body.pad.screen): children go into a Column with a margin; a
// ScrollBar95 appears when they do not fit. `contentWidth` is the width the children should use.
Item {
    id: page
    default property alias content: column.data
    property real margin: 8
    property real spacing: 8
    readonly property real contentWidth: flick.width - 2 * margin
    readonly property alias flickable: flick
    clip: true

    Flickable {
        id: flick
        width: parent.width - (bar.visible ? bar.width : 0)
        height: parent.height
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * page.margin
        boundsBehavior: Flickable.StopAtBounds
        interactive: false
        Column {
            id: column
            x: page.margin
            y: page.margin
            width: page.contentWidth
            spacing: page.spacing
        }
        WheelHandler {
            onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 60)
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        height: parent.height
        flickable: flick
        stepSize: 20
    }
}
