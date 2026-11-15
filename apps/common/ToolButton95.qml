import QtQuick

// A Win95 toolbar button. Shows `glyph` (a large symbol) above or instead
// of `text`; `checked` keeps it pushed in, for toggle buttons like view
// modes.
Item {
    id: button

    property string text: ""
    property string glyph: ""
    property bool enabled: true
    property bool checked: false
    signal clicked()

    implicitWidth: Math.max(content.implicitWidth + 12, 24)
    implicitHeight: 22

    readonly property bool down: (mouse.pressed && mouse.containsMouse) || checked

    Bevel {
        anchors.fill: parent
        style: button.down ? "pressed" : "raised"
        // checked-but-not-pressed buttons get the dithered look; a lighter
        // face is a close, cheap stand-in
        face: button.checked && !mouse.pressed ? Theme.light : Theme.face
    }

    Row {
        id: content
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: button.down ? 1 : 0
        anchors.verticalCenterOffset: button.down ? 1 : 0
        spacing: 4

        EtchedText {
            visible: button.glyph.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: button.glyph
            enabled: button.enabled
            pixelSize: 14
            bold: true
        }
        EtchedText {
            visible: button.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: button.text
            enabled: button.enabled
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: button.enabled
        onClicked: button.clicked()
    }
}
