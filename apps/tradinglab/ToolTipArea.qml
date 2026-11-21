import QtQuick
import Win95Ui

// A Win95 tooltip for whatever it is put in: hover a moment and the text shows in a yellow box below the pointer.
MouseArea {
    id: area
    property string text: ""
    anchors.fill: parent
    hoverEnabled: true
    acceptedButtons: Qt.NoButton
    Timer { id: wait; interval: 600; onTriggered: tip.visible = area.containsMouse && area.text !== "" }
    onContainsMouseChanged: { if (containsMouse) wait.restart(); else tip.visible = false; }
    Rectangle {
        id: tip
        visible: false
        z: 1000
        x: area.mouseX + 4
        y: area.height + 2
        width: tipText.implicitWidth + 8
        height: tipText.implicitHeight + 4
        color: "#ffffe1"
        border.color: "black"
        Text { id: tipText; x: 4; y: 2; text: area.text; textFormat: Text.PlainText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
    }
}
