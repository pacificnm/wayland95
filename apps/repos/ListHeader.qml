import QtQuick
import Win95Ui

// One column header over a list: a raised button with its title.
Item {
    id: header

    property string text: ""
    property bool alignRight: false
    signal clicked()

    height: 18
    readonly property bool down: mouse.pressed && mouse.containsMouse

    Bevel { anchors.fill: parent; style: header.down ? "thinSunken" : "raised" }
    Text {
        x: header.alignRight ? parent.width - width - 6 : 6
        width: Math.min(implicitWidth, parent.width - 12)
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        elide: Text.ElideRight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: header.clicked()
    }
}
