import QtQuick
import Win95Ui

// A sunken white text box of several lines that scrolls, for typing.
FocusScope {
    id: area
    property alias text: edit.text
    property alias edit: edit
    property bool readOnly: false
    property bool monospace: false
    property string placeholder: ""
    signal ctrlReturn()

    implicitWidth: 200
    implicitHeight: 60

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: area.enabled ? Theme.window : Theme.face
    }
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
        contentHeight: Math.max(height, edit.implicitHeight)
        function ensureVisible(r) {
            if (contentY >= r.y)
                contentY = r.y;
            else if (contentY + height <= r.y + r.height)
                contentY = r.y + r.height - height;
        }
        TextEdit {
            id: edit
            width: flick.width
            height: Math.max(flick.height, implicitHeight)
            padding: 3
            focus: true
            readOnly: area.readOnly
            selectByMouse: true
            persistentSelection: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            font.family: area.monospace ? Theme.monoFamily : Theme.fontFamily
            font.pixelSize: area.monospace ? 13 : Theme.fontSize + 1
            onCursorRectangleChanged: if (activeFocus) flick.ensureVisible(cursorRectangle)
            Keys.onPressed: (event) => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                    area.ctrlReturn();
                    event.accepted = true;
                }
            }
            Text {
                visible: edit.text === "" && !edit.activeFocus
                x: edit.leftPadding
                y: edit.topPadding
                text: area.placeholder
                color: Theme.shadow
                font: edit.font
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
