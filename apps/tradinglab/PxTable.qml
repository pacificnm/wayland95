import QtQuick
import Win95Ui

// A table in a sunken white well (upstream's table.grid) whose cells can hold buttons and number boxes, which the
// text-only ListWell cannot. Rows are a JS array; the delegates are kept while the row count stays the same, so a
// refresh (new prices, a new snapshot) does not throw away a number being typed.
//
//   PxTable {
//       rows: [...]
//       columns: [{ title, w (or flex: true, min), right?: bool,
//                   text: fn(row, i), color?: fn, bold?: fn, sub?: fn (a second, grey line), tip?: fn,
//                   onClick?: fn(row, i)                      // the text is a link
//                 | button: "Close" or fn, onClick: fn(row, i), enabled?: fn, flat?: bool
//                 | num: { value: fn, commit: fn(row, i, n), prefix?, suffix?, width?, placeholder?, tip?: fn }
//                 | delegate: Component (an Item with `property var row; property int rowIndex; property var col; property bool sel`) }]
//       footer: [null | "text" | { text, color?, bold?, left?, span? }, ...]   // one per column
//       autoHeight: true          // as tall as the rows (inside a scrolling page); otherwise it scrolls itself
//       selected: fn(row, i) -> bool
//       onRowClicked: (index, row) => ...
//   }
Bevel {
    id: table

    property var rows: []
    property var columns: []
    property int rowHeight: 20
    property var footer: null
    property bool autoHeight: false
    property string emptyText: ""
    property var selected: null
    signal rowClicked(int index, var row)

    style: "sunken"
    face: Theme.window
    readonly property int headerHeight: 18
    readonly property int footerHeight: footer ? 20 : 0
    implicitHeight: 4 + headerHeight + Math.max(rows.length * rowHeight, rows.length === 0 && emptyText !== "" ? 40 : 0) + footerHeight
    height: autoHeight ? implicitHeight : 200

    readonly property real innerWidth: width - 4 - (bar.visible ? bar.width : 0)
    readonly property real fixedWidth: {
        let w = 0;
        for (let i = 0; i < columns.length; ++i)
            if (!columns[i].flex) w += columns[i].w;
        return w;
    }
    readonly property int flexCount: columns.filter(function (c) { return c.flex; }).length
    function columnWidth(c) { return c.flex ? Math.max(c.min || 60, (innerWidth - fixedWidth) / Math.max(1, flexCount)) : c.w; }
    function call(f, row, i, fallback) { return typeof f === "function" ? f(row, i) : (f === undefined ? fallback : f); }

    // ---------------------------------------------------------------- header
    Row {
        id: header
        x: 2
        y: 2
        width: table.innerWidth
        height: table.headerHeight
        clip: true
        Repeater {
            model: table.columns
            Item {
                required property var modelData
                width: table.columnWidth(modelData)
                height: table.headerHeight
                Bevel { anchors.fill: parent; style: "raised" }
                Text {
                    x: 4
                    width: parent.width - 8
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    text: parent.modelData.title || ""
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }

    // ---------------------------------------------------------------- rows
    Flickable {
        id: flick
        x: 2
        y: header.y + header.height
        width: table.innerWidth
        height: table.height - y - 2 - table.footerHeight
        clip: true
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: width
        contentHeight: body.height
        Column {
            id: body
            width: flick.width
            Repeater {
                model: table.rows.length
                delegate: Item {
                    id: rowItem
                    required property int index
                    readonly property var row: index < table.rows.length ? table.rows[index] : null
                    readonly property bool sel: row !== null && table.selected ? table.selected(row, index) === true : false
                    width: body.width
                    height: table.rowHeight
                    Rectangle {
                        anchors.fill: parent
                        color: rowItem.sel ? Theme.selection : "transparent"
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: if (rowItem.row !== null) table.rowClicked(rowItem.index, rowItem.row)
                    }
                    Row {
                        Repeater {
                            model: table.columns
                            delegate: Loader {
                                id: cellLoader
                                required property var modelData
                                width: table.columnWidth(modelData)
                                height: table.rowHeight
                                sourceComponent: modelData.delegate ? modelData.delegate : modelData.button !== undefined ? buttonCell : modelData.num ? numCell : textCell
                                onLoaded: {
                                    item.col = Qt.binding(function () { return cellLoader.modelData; });
                                    item.row = Qt.binding(function () { return rowItem.row; });
                                    item.rowIndex = Qt.binding(function () { return rowItem.index; });
                                    item.sel = Qt.binding(function () { return rowItem.sel; });
                                }
                            }
                        }
                    }
                    Rectangle { // a faint rule between rows
                        y: parent.height - 1
                        width: parent.width
                        height: 1
                        color: "#e4e4e4"
                    }
                }
            }
        }
        WheelHandler {
            onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 3 * table.rowHeight)
        }
    }
    ScrollBar95 {
        id: bar
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: flick.y
        height: flick.height
        flickable: flick
        stepSize: table.rowHeight
    }
    Text {
        visible: table.rows.length === 0 && table.emptyText !== ""
        x: 10
        y: flick.y + 10
        width: parent.width - 20
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        text: table.emptyText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ---------------------------------------------------------------- footer (totals)
    Rectangle {
        visible: table.footer !== null
        x: 2
        y: flick.y + flick.height
        width: table.innerWidth
        height: 1
        color: Theme.shadow
    }
    Row {
        visible: table.footer !== null
        x: 2
        y: flick.y + flick.height + 1
        height: table.footerHeight - 1
        Repeater {
            model: table.footer ? table.columns.length : 0
            Item {
                id: foot
                required property int index
                readonly property var spec: table.footer && index < table.footer.length ? table.footer[index] : null
                readonly property var cell: spec === null || spec === undefined ? { text: "" } : typeof spec === "string" ? { text: spec } : spec
                width: index < table.columns.length ? table.columnWidth(table.columns[index]) : 0
                height: parent.height
                // a cell can run over the next ones (`span`, like colSpan), which are then left empty
                readonly property real spanWidth: {
                    let w = 0;
                    for (let k = foot.index; k < Math.min(table.columns.length, foot.index + (foot.cell.span || 1)); ++k) w += table.columnWidth(table.columns[k]);
                    return w;
                }
                Text {
                    x: 4
                    width: foot.spanWidth - 8
                    elide: Text.ElideRight
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: foot.index < table.columns.length && table.columns[foot.index].right && !foot.cell.left ? Text.AlignRight : Text.AlignLeft
                    textFormat: Text.PlainText
                    text: foot.cell.text || ""
                    color: foot.cell.color || Theme.text
                    font.bold: foot.cell.bold === true
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
        }
    }

    // ---------------------------------------------------------------- cell kinds
    Component {
        id: textCell
        Item {
            id: tc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property bool ok: col !== null && row !== null
            readonly property string sub: ok && col.sub ? String(col.sub(row, rowIndex) ?? "") : ""
            readonly property bool link: ok && typeof col.onClick === "function"
            readonly property color tone: ok && col.color ? (col.color(row, rowIndex) || Theme.text) : Theme.text
            clip: true
            Text {
                id: main
                x: 4
                width: parent.width - 8
                y: tc.sub !== "" ? 2 : 0
                height: tc.sub !== "" ? 16 : parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: tc.ok && tc.col.right ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: { if (!tc.ok) return ""; const t = tc.col.text(tc.row, tc.rowIndex); return t === undefined || t === null ? "" : String(t); }
                color: tc.sel ? Theme.selectionText : tc.link ? "#0000c0" : tc.tone
                font.underline: tc.link
                font.bold: tc.ok && tc.col.bold ? tc.col.bold(tc.row, tc.rowIndex) === true : false
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                MouseArea {
                    visible: tc.link
                    x: tc.ok && tc.col.right ? parent.width - Math.min(parent.width, parent.contentWidth) : 0
                    width: Math.min(parent.width, parent.contentWidth)
                    height: parent.height
                    cursorShape: Qt.PointingHandCursor
                    onClicked: tc.col.onClick(tc.row, tc.rowIndex)
                }
            }
            Text {
                visible: tc.sub !== ""
                x: 4
                y: 17
                width: parent.width - 8
                horizontalAlignment: main.horizontalAlignment
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: tc.sub
                color: tc.sel ? Theme.selectionText : Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
            ToolTipArea { text: tc.ok && tc.col.tip ? String(tc.col.tip(tc.row, tc.rowIndex) ?? "") : "" }
        }
    }
    Component {
        id: buttonCell
        Item {
            id: bc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property bool ok: col !== null && row !== null
            PxButton {
                anchors.centerIn: parent
                height: Math.min(20, parent.height - 2)
                width: Math.max(implicitWidth, bc.ok && bc.col.flat ? 20 : 50)
                flat: bc.ok && bc.col.flat === true
                textColor: bc.sel && flat ? Theme.selectionText : Theme.text
                text: bc.ok ? String(table.call(bc.col.button, bc.row, bc.rowIndex, "")) : ""
                enabled: bc.ok && (bc.col.enabled ? bc.col.enabled(bc.row, bc.rowIndex) !== false : true)
                onClicked: bc.col.onClick(bc.row, bc.rowIndex)
                ToolTipArea { text: bc.ok && bc.col.tip ? String(bc.col.tip(bc.row, bc.rowIndex) ?? "") : "" }
            }
        }
    }
    Component {
        id: numCell
        Item {
            id: nc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property bool ok: col !== null && row !== null
            PxNum {
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                value: nc.ok ? nc.col.num.value(nc.row, nc.rowIndex) : null
                prefix: nc.ok ? nc.col.num.prefix || "" : ""
                suffix: nc.ok ? nc.col.num.suffix || "" : ""
                fieldWidth: nc.ok ? nc.col.num.width || 70 : 70
                placeholder: nc.ok ? nc.col.num.placeholder || "" : ""
                tip: nc.ok && nc.col.num.tip ? String(nc.col.num.tip(nc.row, nc.rowIndex) ?? "") : ""
                onCommitted: (n) => nc.col.num.commit(nc.row, nc.rowIndex, n)
            }
        }
    }
}
