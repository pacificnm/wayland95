import QtQuick
import Win95Ui

// A list in a sunken white well with rows of any height (the strategy list, the journal list): give it a model and
// a delegate; it adds the scroll bar and the wheel. `emptyText` shows when there are no rows.
Bevel {
    id: rows
    property alias model: list.model
    property alias delegate: list.delegate
    property alias list: list
    property string emptyText: ""
    style: "sunken"
    face: Theme.window
    ListView {
        id: list
        x: 2
        y: 2
        width: parent.width - 4 - (bar.visible ? bar.width : 0)
        height: parent.height - 4
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        WheelHandler {
            onWheel: (event) => {
                list.contentY = Math.max(list.originY, Math.min(list.originY + list.contentHeight - list.height, list.contentY - event.angleDelta.y / 120 * 54));
            }
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
        flickable: list
        stepSize: 18
    }
    Text {
        visible: list.count === 0 && rows.emptyText !== ""
        x: 8
        y: 8
        width: parent.width - 16
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: rows.emptyText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
}
