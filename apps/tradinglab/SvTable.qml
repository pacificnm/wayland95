import QtQuick
import Win95Ui

// A table in a sunken white well that grows with its rows (for tables inside a scrolling screen; ListWell is the one
// that scrolls by itself). Raised column headers, per-cell colours, links and shading, and a second grey line per
// cell when a column has `sub`. Wider than the space it has, it scrolls sideways.
//
//   SvTable {
//       model: rows (a JS array)
//       columns: [{ title: "Symbol", w: 80 (or flex: true, min: 120), right?: bool,
//                   text: function (item, index) { ... },  sub?: function (item) { ... },
//                   color?: function (item) -> color, bold?: bool | function (item),
//                   link?: bool | function (item) -> bool,  fill?: function (item) -> color or "",
//                   sort?: "key" (the header is clickable and shows sortKey/sortDir),
//                   bar?: function (item) -> -1..1 (a bar instead of text; diverging?: bool grows from the middle),
//                   barColor?: function (item) -> color }]
//       rowFill: function (item) -> color or ""    // whole-row shading
//       onCellClicked: (row, item, col) => ...      // any click; link cells look clickable
//   }
Bevel {
    id: table
    property var model: []
    property var columns: []
    property int rowHeight: hasSub ? 30 : 18
    property string emptyText: ""
    property var rowFill: null
    property string sortKey: ""
    property int sortDir: -1
    property bool showHeader: true
    signal cellClicked(int row, var item, int col)
    signal headerClicked(int col)

    readonly property bool hasSub: columns.some(function (c) { return !!c.sub; })
    readonly property real fixedWidth: columns.reduce(function (s, c) { return s + (c.flex ? 0 : c.w); }, 0)
    readonly property real flexMin: columns.reduce(function (s, c) { return s + (c.flex ? (c.min || 80) : 0); }, 0)
    readonly property int flexCount: columns.filter(function (c) { return c.flex; }).length
    readonly property real innerWidth: Math.max(width - 4, fixedWidth + flexMin)
    function colWidth(c) {
        if (!c.flex) return c.w;
        return (c.min || 80) + Math.max(0, innerWidth - fixedWidth - flexMin) / Math.max(1, flexCount);
    }
    function val(v, item, fallback) { return typeof v === "function" ? v(item) : (v === undefined ? fallback : v); }
    readonly property int rows: model ? model.length : 0

    style: "sunken"
    face: Theme.window
    height: 4 + (showHeader ? 18 : 0) + (rows > 0 ? rows * rowHeight : (emptyText ? 28 : 0)) + (hbar.visible ? 16 : 0)

    Flickable {
        id: flick
        x: 2
        y: 2
        width: parent.width - 4
        height: parent.height - 4 - (hbar.visible ? 16 : 0)
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: table.innerWidth
        contentHeight: height
        Column {
            width: table.innerWidth
            Row {
                visible: table.showHeader
                height: 18
                Repeater {
                    model: table.columns
                    delegate: Item {
                        id: head
                        required property var modelData
                        required property int index
                        width: table.colWidth(modelData)
                        height: 18
                        Bevel { anchors.fill: parent; style: headMouse.pressed ? "pressed" : "raised" }
                        Text {
                            x: 4
                            width: parent.width - 8
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: head.modelData.right ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: head.modelData.title + (head.modelData.sort && head.modelData.sort === table.sortKey ? (table.sortDir === 1 ? " ▲" : " ▼") : "")
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            id: headMouse
                            anchors.fill: parent
                            enabled: !!head.modelData.sort
                            onClicked: table.headerClicked(head.index)
                        }
                        ToolTipArea { text: head.modelData.tip || "" ; visible: !!head.modelData.tip }
                    }
                }
            }
            Repeater {
                model: table.model
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: table.innerWidth
                    height: table.rowHeight
                    readonly property string fill: table.rowFill ? (table.rowFill(modelData) || "") : ""
                    Rectangle { anchors.fill: parent; color: row.fill !== "" ? row.fill : "transparent" }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e4e4e4" }
                    Row {
                        Repeater {
                            model: table.columns
                            delegate: Item {
                                id: cell
                                required property var modelData
                                required property int index
                                readonly property bool isLink: !!table.val(modelData.link, row.modelData, false)
                                readonly property string cellFill: modelData.fill ? (modelData.fill(row.modelData) || "") : ""
                                width: table.colWidth(modelData)
                                height: table.rowHeight
                                Rectangle { anchors.fill: parent; anchors.bottomMargin: 1; visible: cell.cellFill !== ""; color: cell.cellFill || "transparent" }
                                Item {
                                    visible: !!cell.modelData.bar
                                    x: 4
                                    y: (parent.height - 8) / 2
                                    width: parent.width - 8
                                    height: 8
                                    readonly property real v: cell.modelData.bar ? Math.max(-1, Math.min(1, Number(cell.modelData.bar(row.modelData)) || 0)) : 0
                                    Rectangle { visible: !!cell.modelData.diverging; x: parent.width / 2; width: 1; height: parent.height; color: Theme.shadow }
                                    Rectangle {
                                        x: cell.modelData.diverging ? (parent.v >= 0 ? parent.width / 2 : parent.width / 2 * (1 + parent.v)) : 0
                                        width: cell.modelData.diverging ? Math.abs(parent.v) * parent.width / 2 : Math.abs(parent.v) * parent.width
                                        height: parent.height
                                        color: cell.modelData.barColor ? cell.modelData.barColor(row.modelData) : Theme.selection
                                    }
                                }
                                Text {
                                    id: main
                                    visible: !cell.modelData.bar
                                    x: 4
                                    y: cell.modelData.sub ? 2 : 0
                                    width: parent.width - 8
                                    height: cell.modelData.sub ? 14 : parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: cell.modelData.right ? Text.AlignRight : Text.AlignLeft
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    text: { if (!cell.modelData.text) return ""; const t = cell.modelData.text(row.modelData, row.index); return t === undefined || t === null ? "" : String(t).replace(/\n/g, " "); }
                                    color: cell.isLink ? "#0000c0" : cell.modelData.color ? cell.modelData.color(row.modelData) : Theme.text
                                    font.underline: cell.isLink && cellMouse.containsMouse
                                    font.bold: !!table.val(cell.modelData.bold, row.modelData, false)
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                                Text {
                                    visible: !!cell.modelData.sub
                                    x: 4
                                    y: 15
                                    width: parent.width - 8
                                    height: 13
                                    horizontalAlignment: cell.modelData.right ? Text.AlignRight : Text.AlignLeft
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    text: cell.modelData.sub ? String(cell.modelData.sub(row.modelData) ?? "") : ""
                                    color: Theme.shadow
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize - 1
                                }
                                MouseArea {
                                    id: cellMouse
                                    anchors.fill: parent
                                    hoverEnabled: cell.isLink
                                    cursorShape: cell.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: table.cellClicked(row.index, row.modelData, cell.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Text {
        visible: table.rows === 0 && table.emptyText !== ""
        x: 8
        y: (table.showHeader ? 20 : 2) + 7
        width: parent.width - 16
        elide: Text.ElideRight
        textFormat: Text.PlainText
        text: table.emptyText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    ScrollBar95 {
        id: hbar
        orientation: Qt.Horizontal
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        flickable: flick
        stepSize: 40
    }
}
