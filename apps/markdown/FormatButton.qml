import QtQuick
import Win95Ui

// A small toolbar button (WordPad's): one of Wayland 95's icons (`icon`,
// Icon95's) or a styled letter (`letter`, with bold/italic/underline/
// strike), flat until the mouse is over it, pushed in while `checked`.
// `tip` shows in the status bar.
Item {
    id: button

    property string icon: ""
    property string letter: ""
    property bool letterBold: false
    property bool letterItalic: false
    property bool letterUnderline: false
    property bool letterStrike: false
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

    Item {
        width: 16
        height: 16
        x: (parent.width - width) / 2 + (button.down ? 1 : 0)
        y: (parent.height - height) / 2 + (button.down ? 1 : 0)

        Text {
            visible: button.letter.length > 0
            anchors.centerIn: parent
            text: button.letter
            color: button.enabled ? "#000000" : Theme.disabledText
            font.family: "Liberation Serif"
            font.pixelSize: 15
            font.bold: button.letterBold
            font.italic: button.letterItalic
            font.underline: button.letterUnderline
            font.strikeout: button.letterStrike
        }

        Icon95 {
            visible: button.icon.length > 0
            anchors.fill: parent
            name: button.icon
            enabled: button.enabled
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
