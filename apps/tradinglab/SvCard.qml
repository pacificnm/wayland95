import QtQuick
import Win95Ui

// A titled section of a screen (upstream "card-x"): a Win95 group box whose height follows its contents, with an
// optional grey note after the title (upstream's card-x-head muted text). Children stack in a column.
Item {
    id: card
    property string title: ""
    property string sub: ""
    default property alias body: col.data
    property alias spacing: col.spacing
    implicitHeight: col.implicitHeight + 30
    height: implicitHeight

    Rectangle {
        x: 1; y: 7
        width: parent.width - 1; height: parent.height - 7
        color: "transparent"
        border.color: Theme.highlight
    }
    Rectangle {
        x: 0; y: 6
        width: parent.width - 1; height: parent.height - 7
        color: "transparent"
        border.color: Theme.shadow
    }
    Rectangle {
        visible: card.title.length > 0
        x: 7
        width: Math.min(caption.implicitWidth, card.width - 20) + 4
        height: caption.implicitHeight
        color: Theme.face
        Text {
            id: caption
            x: 2
            width: Math.min(implicitWidth, card.width - 20)
            elide: Text.ElideRight
            textFormat: Text.StyledText
            text: "<b>" + card.title + "</b>" + (card.sub ? "  <font color=\"" + Theme.shadow + "\">" + card.sub + "</font>" : "")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }
    Column {
        id: col
        x: 10
        y: 22
        width: parent.width - 20
        spacing: 6
    }
}
