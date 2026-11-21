import QtQuick
import Win95Ui

// The title bar over a whole-app screen (upstream's .pane-title): the screen's name on the left, its own buttons on the
// right (put them in as children). Used by the Account, Active Trades, Watchlists, Portfolio and Calculator screens.
Bevel {
    id: bar
    property string title: ""
    default property alias tools: toolRow.data
    style: "raised"
    height: 30

    Text {
        x: 8
        anchors.verticalCenter: parent.verticalCenter
        text: bar.title
        textFormat: Text.PlainText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: 13
        font.bold: true
    }
    Row {
        id: toolRow
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
}
