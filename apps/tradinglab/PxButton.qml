import QtQuick
import Win95Ui

// A small Win95 push button for toolbars, table rows and toggle groups (Button95 is the 75x23 dialog button):
// raised, sunken while pressed or `checked`; `flat` buttons are raised only under the mouse, like Win95 toolbars.
// `icon` is an Icon95 name shown before the text.
Item {
    id: button
    property string text: ""
    property string icon: ""
    property bool checked: false
    property bool flat: false
    property bool bold: false
    property color textColor: Theme.text
    signal clicked()
    signal doubleClicked()

    implicitWidth: row.implicitWidth + 14
    implicitHeight: 22
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
            bold: button.bold
            color: button.textColor
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
        onDoubleClicked: button.doubleClicked()
    }
}
