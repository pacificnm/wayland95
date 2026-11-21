import QtQuick
import Win95Ui

// A figure in a sunken white box (upstream's .card / .card-x stat cards): a caption, the number in large type and an
// optional line under it.
Bevel {
    id: card
    property string label: ""
    property string value: ""
    property color valueColor: Theme.text
    property string sub: ""
    property color subColor: Theme.shadow
    style: "sunken"
    face: Theme.window
    height: sub !== "" ? 62 : 48

    Column {
        x: 8
        y: 6
        width: parent.width - 16
        spacing: 2
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.label
            textFormat: Text.PlainText
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.value
            textFormat: Text.PlainText
            color: card.valueColor
            font.family: Theme.fontFamily
            font.pixelSize: 17
            font.bold: true
        }
        Text {
            visible: card.sub !== ""
            width: parent.width
            elide: Text.ElideRight
            text: card.sub
            textFormat: Text.PlainText
            color: card.subColor
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }
}
