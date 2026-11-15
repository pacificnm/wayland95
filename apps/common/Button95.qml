import QtQuick

// A Win95 dialog push button (OK / Cancel / Yes / No). `isDefault` draws
// the extra black frame of the button Enter activates.
FocusScope {
    id: button

    property string text: ""
    property bool isDefault: false
    signal clicked()

    implicitWidth: Math.max(75, label.implicitWidth + 20)
    implicitHeight: 23
    activeFocusOnTab: true

    readonly property bool down: (mouse.pressed && mouse.containsMouse) || spaceDown
    property bool spaceDown: false

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.dark
        border.width: 1
        visible: button.isDefault || button.activeFocus
    }

    Bevel {
        anchors.fill: parent
        anchors.margins: (button.isDefault || button.activeFocus) ? 1 : 0
        style: button.down ? "thinSunken" : "raised"
    }

    EtchedText {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        text: button.text
        enabled: button.enabled
    }

    // dotted focus rectangle
    Rectangle {
        visible: button.activeFocus
        anchors.fill: parent
        anchors.margins: 4
        color: "transparent"
        border.color: Theme.dark
        border.width: 1
        opacity: 0.6
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: button.clicked()
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            button.spaceDown = true;
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            button.clicked();
            event.accepted = true;
        }
    }
    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat && button.spaceDown) {
            button.spaceDown = false;
            button.clicked();
            event.accepted = true;
        }
    }
}
