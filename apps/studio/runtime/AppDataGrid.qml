import QtQuick
import Win95Ui

// DataGrid: a table's records in rows. DataTable is the table (blank: the
// form's); Columns the fields to show (blank: all of them). With
// FollowForm, choosing a row of the form's own table shows that record in
// the form's bound elements, and the grid follows the form's record.
//
// In code: grid.currentId, grid.currentRow (the record), grid.rows,
// grid.refresh(), grid.search = "text".
AppElement {
    id: grid

    property string table: ""
    property var columns: []
    property string search: ""
    property string orderBy: ""
    property bool followForm: true
    signal rowSelected()
    signal rowActivated()

    // the Studio's canvas has no database: it hands over the headings
    property var designColumns: []

    defaultBack: Theme.window
    activeFocusOnTab: !designMode

    readonly property string source: table !== "" ? table : (form ? form.table : "")
    readonly property var db: form && !designMode ? form.db : null
    readonly property var info: db && source !== "" ? db.tableInfo(source) : ({})
    readonly property var shownFields: {
        if (designMode)
            return designColumns.map(function (c) { return { name: c, label: c, type: "text" }; });
        var all = info.fields || [], out = [];
        if (!columns || columns.length === 0)
            return all;
        for (var i = 0; i < columns.length; ++i) {
            var name = String(columns[i]).trim();
            if (name === "id" || name === "created_on" || name === "modified_on") {
                out.push({ name: name, label: name, type: name === "id" ? "integer" : "datetime" });
                continue;
            }
            for (var j = 0; j < all.length; ++j) {
                if (all[j].name === name)
                    out.push(all[j]);
            }
        }
        return out;
    }
    readonly property real columnWidth: shownFields.length === 0 ? 0
        : Math.max(72, Math.floor((width - 4 - (vbar.needed ? vbar.width : 0)) / shownFields.length))
    readonly property real rowsWidth: columnWidth * shownFields.length
    readonly property int rowHeight: pixelSize + 6

    property var rows: []
    property int currentIndex: -1
    readonly property var currentRow: currentIndex >= 0 && currentIndex < rows.length ? rows[currentIndex] : null
    readonly property var currentId: currentRow ? currentRow.id : null

    function refresh() {
        if (!db || source === "") {
            rows = [];
            currentIndex = -1;
            return;
        }
        var keep = currentId;
        rows = db.select(source, { search: search, orderBy: orderBy });
        currentIndex = indexOfId(keep);
        follow();
    }
    function indexOfId(id) {
        if (id === null || id === undefined)
            return -1;
        for (var i = 0; i < rows.length; ++i) {
            if (String(rows[i].id) === String(id))
                return i;
        }
        return -1;
    }
    readonly property bool following: followForm && form !== null && !designMode && source !== "" && source === form.table
    // the form's record is the row that's marked
    function follow() {
        if (!following)
            return;
        var i = indexOfId(form.recordId);
        if (i !== currentIndex) {
            currentIndex = i;
            if (i >= 0)
                view.positionViewAtIndex(i, ListView.Contain);
        }
    }
    function select(i, activate) {
        if (i < 0 || i >= rows.length)
            return;
        currentIndex = i;
        view.positionViewAtIndex(i, ListView.Contain);
        if (following && String(form.recordId) !== String(rows[i].id))
            form.loadRecord(rows[i].id);
        rowSelected();
        if (activate)
            rowActivated();
    }

    onSourceChanged: refresh()
    onSearchChanged: refresh()
    onOrderByChanged: refresh()
    onDbChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: grid.db
        function onChanged(table) {
            if (table === grid.source)
                grid.refresh();
        }
    }
    Connections {
        target: grid.following ? grid.form : null
        function onCurrent() { grid.follow(); }
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: grid.fillColor
    }

    Item {
        id: inside
        x: 2
        y: 2
        width: parent.width - 4 - (vbar.needed ? vbar.width : 0)
        height: parent.height - 4 - (hbar.needed ? hbar.height : 0)
        clip: true

        // the headings, moving sideways with the rows
        Row {
            id: header
            x: -view.contentX
            height: grid.rowHeight + 2
            z: 2
            Repeater {
                model: grid.shownFields
                delegate: Item {
                    id: heading
                    required property var modelData
                    width: grid.columnWidth
                    height: header.height
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 5
                        width: parent.width - 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: heading.modelData.label || heading.modelData.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: grid.family
                        font.pixelSize: grid.pixelSize
                    }
                }
            }
        }

        ListView {
            id: view
            y: header.height
            width: parent.width
            height: parent.height - header.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalAndVerticalFlick
            contentWidth: Math.max(width, grid.rowsWidth)
            interactive: !grid.designMode
            model: grid.rows
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: index === grid.currentIndex
                width: view.contentWidth
                height: grid.rowHeight
                color: current ? Theme.selection : "transparent"
                Row {
                    Repeater {
                        model: grid.shownFields
                        delegate: Item {
                            id: cell
                            required property var modelData
                            width: grid.columnWidth
                            height: grid.rowHeight
                            Text {
                                x: 5
                                width: parent.width - 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: grid.display(row.modelData[cell.modelData.name], cell.modelData)
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                maximumLineCount: 1
                                horizontalAlignment: cell.modelData.type === "integer" || cell.modelData.type === "decimal"
                                                     || cell.modelData.type === "float" || cell.modelData.type === "currency"
                                                     ? Text.AlignRight : Text.AlignLeft
                                color: row.current ? Theme.selectionText : grid.textColor
                                font.family: grid.family
                                font.pixelSize: grid.pixelSize
                                font.bold: grid.fontBold
                                font.italic: grid.fontItalic
                                font.underline: grid.fontUnderline
                            }
                            // the grid's lines
                            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.face }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.face }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !grid.designMode
                    onPressed: {
                        grid.forceActiveFocus();
                        grid.select(row.index, false);
                    }
                    onDoubleClicked: grid.select(row.index, true)
                }
            }
        }
    }
    ScrollBar95 {
        id: vbar
        flickable: view
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2 + header.height
        height: inside.height - header.height
    }
    ScrollBar95 {
        id: hbar
        flickable: view
        orientation: Qt.Horizontal
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: inside.width
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Down)
            select(Math.min(rows.length - 1, currentIndex + 1), false);
        else if (event.key === Qt.Key_Up)
            select(Math.max(0, currentIndex - 1), false);
        else if (event.key === Qt.Key_Home)
            select(0, false);
        else if (event.key === Qt.Key_End)
            select(rows.length - 1, false);
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            select(currentIndex, true);
        else
            return;
        event.accepted = true;
    }
}
