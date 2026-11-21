import QtQuick
import Win95Ui

// A toolbar button with one of Athenaeum's 16x16 icons and, if given, a
// label beside it. `flat`: raised only under the mouse, as Win95's
// toolbars drew theirs.
Item {
    id: button
    property string icon: ""
    property string text: ""
    property bool enabled: true
    property bool checked: false
    property bool flat: false
    signal clicked()

    implicitWidth: row.implicitWidth + 10
    implicitHeight: 24
    readonly property bool down: (mouse.pressed && mouse.containsMouse) || checked

    Bevel {
        anchors.fill: parent
        visible: !button.flat || mouse.containsMouse || button.down
        style: button.down ? (button.flat ? "thinSunken" : "pressed") : (button.flat ? "thinRaised" : "raised")
        face: button.checked && !mouse.pressed ? Theme.light : Theme.face
    }
    Row {
        id: row
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        spacing: 4
        Icon95 {
            visible: button.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            name: button.icon
            enabled: button.enabled
        }
        EtchedText {
            visible: button.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            enabled: button.enabled
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: button.flat
        enabled: button.enabled
        onClicked: button.clicked()
    }
}
