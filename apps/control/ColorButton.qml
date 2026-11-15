import QtQuick
import Win95Ui

// A labelled Win95 color button: a swatch and a ▼. The owner opens its
// color picker in response to `clicked(button)`.
Column {
    id: cb
    property string label
    property color color
    signal clicked(Item button)

    spacing: 1

    Text {
        text: cb.label
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize - 1
    }
    ToolButton95 {
        id: button
        width: 44
        height: 21
        Rectangle {
            x: 5; y: 5
            width: 22; height: 11
            color: cb.color
            border.color: Theme.dark
        }
        Text {
            x: 31
            anchors.verticalCenter: parent.verticalCenter
            text: "▼"
            font.pixelSize: 7
        }
        onClicked: cb.clicked(button)
    }
}
