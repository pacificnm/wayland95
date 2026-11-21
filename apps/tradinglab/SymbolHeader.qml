import QtQuick
import Win95Ui

// The header every per-symbol screen shares (upstream ScreenFrame + SymbolTabs): the symbol and its last price,
// tool buttons for the six per-symbol screens (those the FMP plan includes), and room on the right for the screen's
// own controls (put them in `tools`).
Bevel {
    id: header
    property var app
    property string current: app ? app.view : ""
    property string title: ""            // e.g. "Quote Details"; defaults to the screen's name
    default property alias tools: toolRow.data
    style: "raised"
    height: 30

    Row {
        id: tabs
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Repeater {
            model: header.app ? header.app.symbolViews.filter(function (t) { return t.id === header.current || header.app.hasScreen(t.id); }) : []
            delegate: ToolButton95 {
                required property var modelData
                width: 24
                height: 22
                checked: header.current === modelData.id
                onClicked: header.app.setView(modelData.id)
                Icon95 { anchors.centerIn: parent; name: parent.modelData.icon }
                ToolTipArea { text: parent.modelData.text }
            }
        }
    }
    Text {
        id: label
        x: tabs.x + tabs.width + 10
        anchors.verticalCenter: parent.verticalCenter
        textFormat: Text.PlainText
        text: (header.app ? header.app.symbol : "") + "  " + (header.title || (header.app ? (header.app.symbolViews.find(function (t) { return t.id === header.current; }) || { text: "" }).text : ""))
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize + 1
        font.bold: true
        color: Theme.text
    }
    Text {
        x: label.x + label.width + 10
        anchors.verticalCenter: parent.verticalCenter
        visible: header.app && header.app.quote.last !== null
        textFormat: Text.PlainText
        text: header.app && header.app.quote.last !== null ? Util.usd(header.app.quote.last) + "  " + Util.signed(header.app.quote.change) + " (" + Util.pct(header.app.quote.changePct) + ")" : ""
        color: header.app ? Util.tone(header.app.quote.change) : Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Row {
        id: toolRow
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
}
