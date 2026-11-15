import QtQuick
import Win95Ui

// A title bar for the Appearance preview, in the colors being edited.
Rectangle {
    id: title
    property bool active: true
    property string text: ""
    property var colors: Wayland95Settings.colors

    x: 3
    y: 3
    width: parent.width - 6
    height: 16
    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0; color: title.active ? title.colors.activeTitle : title.colors.inactiveTitle }
        GradientStop { position: 1; color: title.active ? title.colors.activeTitle2 : title.colors.inactiveTitle2 }
    }

    Text {
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        text: title.text
        color: title.active ? title.colors.activeTitleText : title.colors.inactiveTitleText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Repeater {
            model: ["‒", "□", "✕"]
            PreviewBevel {
                required property string modelData
                width: 14
                height: 12
                face: title.colors.face
                Text {
                    anchors.centerIn: parent
                    text: parent.modelData
                    font.pixelSize: 8
                    color: title.colors.windowText
                }
            }
        }
    }
}
