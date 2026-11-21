import QtQuick
import Win95Ui

// A filter chip (upstream .tk-chip): a small flat button that stays pushed in while it is on.
Item {
    id: chip
    property string text: ""
    property bool checked: false
    signal clicked()
    implicitWidth: label.implicitWidth + 14
    implicitHeight: 20
    Bevel {
        anchors.fill: parent
        style: chip.checked || (mouse.pressed && mouse.containsMouse) ? "thinSunken" : "thinRaised"
        face: chip.checked ? Theme.light : Theme.face
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: chip.checked ? 1 : 0
        anchors.verticalCenterOffset: chip.checked ? 1 : 0
        text: chip.text
        textFormat: Text.PlainText
        color: chip.enabled ? Theme.text : Theme.disabledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: chip.checked
    }
    MouseArea { id: mouse; anchors.fill: parent; onClicked: chip.clicked() }
}
