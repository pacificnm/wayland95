import QtQuick
import Win95Ui

// The header of a market-wide screen (upstream "pane-title" and its "subtabs"): the screen's name, optional tabs
// (`tabs`: [{id, text}], `current`, tabClicked(id)) and room on the right for the screen's own controls (`tools`).
Bevel {
    id: bar
    property string title: ""
    property var tabs: []
    property string current: ""
    default property alias tools: toolRow.data
    signal tabClicked(string id)
    style: "raised"
    height: 30

    Text {
        id: label
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: bar.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize + 1
        font.bold: true
        color: Theme.text
    }
    Row {
        x: label.x + label.width + 14
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Repeater {
            model: bar.tabs
            delegate: ToolButton95 {
                required property var modelData
                height: 22
                text: modelData.text
                checked: bar.current === modelData.id
                onClicked: bar.tabClicked(modelData.id)
            }
        }
    }
    Row {
        id: toolRow
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
}
