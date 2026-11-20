import QtQuick

// The list a drop-down opens: black-edged white box under its field, the
// row under the pointer (or chosen with the arrows) highlighted. Clicking
// outside, or Esc, closes it. Fill the screen with it (it sits on top).
FocusScope {
    id: popup

    visible: false
    z: 2000

    property var items: []
    property int current: -1
    property var callback: null
    property Item owner: null
    readonly property int rowHeight: 16

    function open(anchor, list, index, cb) {
        items = list;
        current = index;
        callback = cb;
        owner = anchor;
        var p = anchor.mapToItem(popup, 0, anchor.height);
        box.width = anchor.width;
        box.x = p.x;
        // no room below: open above
        var h = list.length * rowHeight + 2;
        box.y = p.y + h > popup.height ? p.y - anchor.height - h : p.y;
        visible = true;
        forceActiveFocus();
    }
    function close() {
        visible = false;
        if (owner)
            owner.forceActiveFocus();
    }
    function pick(i) {
        visible = false;
        if (callback && i >= 0 && i < items.length)
            callback(i);
    }

    MouseArea {
        anchors.fill: parent
        onPressed: popup.close()
    }

    Rectangle {
        id: box
        height: popup.items.length * popup.rowHeight + 2
        color: W95.window
        border.color: W95.dark

        Repeater {
            model: popup.items
            delegate: Rectangle {
                required property var modelData
                required property int index
                x: 1
                y: 1 + index * popup.rowHeight
                width: box.width - 2
                height: popup.rowHeight
                color: popup.current === index ? W95.selection : "transparent"
                Text {
                    x: 3
                    width: parent.width - 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData
                    elide: Text.ElideRight
                    color: popup.current === parent.index ? W95.selectionText : W95.text
                    font.family: W95.fontFamily
                    font.pixelSize: W95.fontSize
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onContainsMouseChanged: if (containsMouse) popup.current = parent.index
                    onClicked: popup.pick(parent.index)
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        var n = items.length;
        if (event.key === Qt.Key_Down)
            current = Math.min(n - 1, current + 1);
        else if (event.key === Qt.Key_Up)
            current = Math.max(0, current - 1);
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)
            pick(current);
        else if (event.key === Qt.Key_Escape || event.key === Qt.Key_F4 || event.key === Qt.Key_Tab)
            close();
        event.accepted = true;
    }
}
