import QtQuick
import Win95Ui

// A Calculator key: a raised Win95 button with its label in the key's
// colour (blue for digits, red for operators and memory, dark blue for
// functions). `key` is what it presses (CalcEngine.press); `flash()` shows
// it pushed in, when the keyboard presses it.
Item {
    id: button

    property string text: ""
    property string key: text
    property color ink: "#0000c0"
    property bool enabled: true
    property bool flashing: false
    signal clicked(string key)

    width: 36
    height: 29

    function flash() {
        flashing = true;
        flashTimer.restart();
    }
    Timer { id: flashTimer; interval: 120; onTriggered: button.flashing = false }

    readonly property bool down: (mouse.pressed && mouse.containsMouse) || flashing

    Bevel {
        anchors.fill: parent
        style: button.down ? "pressed" : "raised"
    }
    Text {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        text: button.text
        textFormat: Text.StyledText
        color: button.enabled ? button.ink : Theme.disabledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize + 1
    }
    // (disabled: the label etched, as Win95 does)
    Text {
        visible: !button.enabled
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: 1
        anchors.verticalCenterOffset: 1
        z: -1
        text: button.text
        textFormat: Text.StyledText
        color: Theme.highlight
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize + 1
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabled
        onClicked: button.clicked(button.key)
    }
}
