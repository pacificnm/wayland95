import QtQuick
import Win95Ui

// Article titles as a row of small buttons that wraps (recently read, a
// library's overviews), and "Show All" when there are more.
Flow {
    id: flow
    property var entries: []
    property bool more: false
    signal picked(var entry)
    signal showAll()
    spacing: 4
    Repeater {
        model: flow.entries
        Button95 {
            required property var modelData
            width: Math.min(flow.width, Math.max(60, label.implicitWidth + 16))
            height: 22
            text: ""
            Text {
                id: label
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 10)
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: parent.modelData.title
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            onClicked: flow.picked(modelData)
        }
    }
    Button95 {
        visible: flow.more
        width: 80
        height: 22
        text: "Show All..."
        onClicked: flow.showAll()
    }
}
