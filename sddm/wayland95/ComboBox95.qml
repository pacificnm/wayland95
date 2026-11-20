import QtQuick

// A Win95 drop-down list: sunken white box with the current choice and a ▼
// button; the list opens in `popup` (a ListPopup95).
FocusScope {
    id: combo

    property var model: []          // strings
    property int currentIndex: 0
    property ListPopup95 popup
    signal activated(int index)

    implicitWidth: 160
    implicitHeight: 21
    activeFocusOnTab: true

    readonly property string currentText: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : ""

    function choose(i) {
        if (i < 0 || i >= model.length)
            return;
        currentIndex = i;
        activated(i);
    }
    function openList() {
        if (popup)
            popup.open(combo, model, currentIndex, function (i) { combo.choose(i); combo.forceActiveFocus(); });
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: W95.window
    }
    Rectangle {
        x: 3
        y: 3
        width: parent.width - button.width - 7
        height: parent.height - 6
        color: combo.activeFocus ? W95.selection : "transparent"
        Text {
            x: 2
            width: parent.width - 4
            anchors.verticalCenter: parent.verticalCenter
            text: combo.currentText
            elide: Text.ElideRight
            color: combo.activeFocus ? W95.selectionText : W95.text
            font.family: W95.fontFamily
            font.pixelSize: W95.fontSize
        }
    }
    Item {
        id: button
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: parent.height - 4
        Bevel { anchors.fill: parent; style: mouse.pressed ? "thinSunken" : "raised" }
        Text {
            anchors.centerIn: parent
            text: "▼"
            font.pixelSize: 7
            color: W95.text
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        onPressed: {
            combo.forceActiveFocus();
            combo.openList();
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)))
            openList();
        else if (event.key === Qt.Key_Down)
            choose(Math.min(model.length - 1, currentIndex + 1));
        else if (event.key === Qt.Key_Up)
            choose(Math.max(0, currentIndex - 1));
        else
            return;
        event.accepted = true;
    }
}
