import QtQuick

// Text that turns into the Win95 "etched" gray-with-white-shadow look when
// disabled.
Item {
    id: root

    property string text: ""
    property bool enabled: true
    property color color: Theme.text
    property bool bold: false
    property int pixelSize: Theme.fontSize

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        visible: !root.enabled
        x: 1; y: 1
        text: root.text
        color: Theme.highlight
        font: label.font
    }
    Text {
        id: label
        text: root.text
        color: root.enabled ? root.color : Theme.disabledText
        font.family: Theme.fontFamily
        font.pixelSize: root.pixelSize
        font.bold: root.bold
    }
}
