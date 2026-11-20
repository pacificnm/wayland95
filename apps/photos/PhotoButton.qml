import QtQuick
import Win95Ui

// A flat toolbar button with a PhotoIcon, raised while the mouse is over
// it, pushed in while `checked`. `tip` shows in the status bar.
Item {
    id: button

    property string icon: ""
    property string tip: ""
    property bool enabled: true
    property bool checked: false
    readonly property bool hovered: mouse.containsMouse
    signal clicked()

    width: 23
    height: 22

    readonly property bool down: (mouse.pressed && mouse.containsMouse) || checked

    Bevel {
        anchors.fill: parent
        visible: button.down || (mouse.containsMouse && button.enabled)
        style: button.down ? "pressed" : "raised"
        face: button.checked && !mouse.pressed ? Theme.light : Theme.face
    }

    PhotoIcon {
        x: (parent.width - width) / 2 + (button.down ? 1 : 0)
        y: (parent.height - height) / 2 + (button.down ? 1 : 0)
        name: button.icon
        enabled: button.enabled
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
