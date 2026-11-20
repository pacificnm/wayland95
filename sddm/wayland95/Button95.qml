import QtQuick

// A Win95 push button. `isDefault` draws the black frame of the button
// Enter activates; disabled ones get the etched gray label.
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
        border.color: W95.dark
        visible: button.isDefault || button.activeFocus
    }
    Bevel {
        anchors.fill: parent
        anchors.margins: (button.isDefault || button.activeFocus) ? 1 : 0
        style: button.down ? "thinSunken" : "raised"
    }

    Text {
        visible: !button.enabled
        x: label.x + 1; y: label.y + 1
        text: label.text
        textFormat: Text.StyledText
        color: W95.highlight
        font: label.font
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        text: button.text
        textFormat: Text.StyledText
        color: button.enabled ? W95.text : W95.disabledText
        font.family: W95.fontFamily
        font.pixelSize: W95.fontSize
    }

    // dotted focus rectangle
    Rectangle {
        visible: button.activeFocus
        anchors.fill: parent
        anchors.margins: 4
        color: "transparent"
        border.color: W95.dark
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
