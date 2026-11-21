import QtQuick
import Win95Ui

// Label / value pairs (upstream ScreenFrame's KV): the label grey on the left, the value bold on the right, in one or
// more columns. `entries` is an object {label: value} (insertion order is kept) or an array of [label, value].
Grid {
    id: kv
    property var entries: ({})
    property int cols: 1
    property var valueColor: null      // function (label, value) -> color, or null
    readonly property var pairs: Array.isArray(entries) ? entries : Object.keys(entries || {}).map(function (k) { return [k, entries[k]]; })
    columns: cols
    columnSpacing: 16
    rowSpacing: 2
    Repeater {
        model: kv.pairs
        delegate: Item {
            required property var modelData
            width: (kv.width - (kv.cols - 1) * kv.columnSpacing) / kv.cols
            height: 17
            Label95 {
                id: key
                width: Math.min(implicitWidth, parent.width * 0.6)
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                dim: true
                text: parent.modelData[0]
            }
            Label95 {
                anchors.left: key.right
                anchors.leftMargin: 8
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                bold: true
                text: String(parent.modelData[1] ?? "—")
                color: kv.valueColor ? kv.valueColor(parent.modelData[0], parent.modelData[1]) : Theme.text
            }
        }
    }
}
