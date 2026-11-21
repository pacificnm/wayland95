import QtQuick
import Win95Ui

// The title strip over a screen (upstream .pane-title): the screen's name on the left, its buttons on the right
// (put them in it).
Bevel {
    id: bar
    property string title: ""
    property string crumb: ""
    default property alias tools: toolRow.data
    style: "raised"
    height: 30
    Text {
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        text: bar.title
        textFormat: Text.PlainText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize + 1
        font.bold: true
        color: Theme.text
    }
    Row {
        id: toolRow
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            visible: bar.crumb !== ""
            anchors.verticalCenter: parent.verticalCenter
            text: bar.crumb
            textFormat: Text.PlainText
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }
}
