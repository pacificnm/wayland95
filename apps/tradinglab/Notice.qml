import QtQuick
import Win95Ui

// A message across the top of a page or panel (Ollama is not running, a
// file is missing...), with an icon and buttons: [{ text, action }]
Rectangle {
    id: notice
    property string kind: "info"     // info | warning | error
    property string text: ""
    property var buttons: []
    visible: text !== ""
    height: visible ? Math.max(28, body.implicitHeight + 10) : 0
    color: kind === "error" ? "#ffe8e8" : kind === "warning" ? "#ffffe1" : "#e8f0ff"
    border.color: Theme.shadow
    Icon95 { x: 6; y: 6; name: notice.kind }
    Text {
        id: body
        x: 28
        y: 5
        width: parent.width - x - buttonRow.width - 12
        wrapMode: Text.Wrap
        textFormat: Text.StyledText // the program's own words (with <b>), outside text goes in through Util.plain
        text: notice.text
        color: "#000000"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Row {
        id: buttonRow
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Repeater {
            model: notice.buttons
            Button95 {
                required property var modelData
                width: Math.max(70, implicitWidth)
                height: 21
                text: modelData.text
                onClicked: modelData.action()
            }
        }
    }
}
