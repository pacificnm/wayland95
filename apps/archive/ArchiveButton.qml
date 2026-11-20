import QtQuick
import Win95Ui

// A big toolbar button: a 32x32 ArchiveIcon over its label, flat until the
// mouse is over it. `tip` shows in the status bar while it is.
Item {
    id: button

    property string icon: ""
    property string text: ""
    property string tip: ""
    property bool enabled: true
    readonly property bool hovered: mouse.containsMouse
    signal clicked()

    width: Math.max(56, label.implicitWidth + 12)
    height: 54

    readonly property bool down: mouse.pressed && mouse.containsMouse

    Bevel {
        anchors.fill: parent
        visible: button.down || (mouse.containsMouse && button.enabled)
        style: button.down ? "pressed" : "raised"
    }

    ArchiveIcon {
        x: (parent.width - width) / 2 + (button.down ? 1 : 0)
        y: 4 + (button.down ? 1 : 0)
        name: button.icon
        enabled: button.enabled
    }

    EtchedText {
        id: label
        x: (parent.width - width) / 2 + (button.down ? 1 : 0)
        y: 38 + (button.down ? 1 : 0)
        text: button.text
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
