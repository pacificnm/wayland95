import QtQuick
import Win95Ui

// Text to read in a sunken white well that scrolls: plain, or Markdown
// (model output goes through Util.markdown first). Links are reported.
Bevel {
    id: view
    property string text: ""
    property int textFormat: Text.PlainText
    property bool selectable: true
    property alias flickable: flick
    property alias edit: body
    property real padding: 6
    signal linkActivated(string link)

    style: "sunken"
    face: Theme.window

    Flickable {
        id: flick
        x: 2
        y: 2
        width: parent.width - 4 - (bar.visible ? bar.width : 0)
        height: parent.height - 4
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: body.implicitHeight
        TextEdit {
            id: body
            width: flick.width
            padding: view.padding
            readOnly: true
            selectByMouse: view.selectable
            persistentSelection: true
            wrapMode: TextEdit.Wrap
            textFormat: view.textFormat
            text: view.text
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize + 1
            onLinkActivated: (link) => view.linkActivated(link)
            HoverHandler {
                cursorShape: body.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
        }
        WheelHandler {
            onWheel: (event) => {
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY - event.angleDelta.y / 120 * 48));
            }
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
        flickable: flick
        stepSize: 16
    }
}
