import QtQuick
import Win95Ui

// The Properties window's grid: a name and a value on each row, edited in
// place. The Studio uses it for an element's or form's properties, a
// field's properties in the Data Designer and a record in the Data view.
//
// rows: [{ name, label, type, value, values?, hint?, error?, readOnly?,
//          placeholder? (in grey while it's blank), editPlaceholder? (an
//          edit of it blank starts from the placeholder) }]
// type: string, int, real, bool, color, font, enum (values), choice (values
//       as [{ value, label }]), list (strings, one per line), text (several
//       lines), image (a file), media (a sound or video file), icon (one of
//       Wayland 95's: the Icon chooser), menu and toolbar (the Menu Editor),
//       graph (the Graph Editor)
// `tools` supplies the dialogs: pickColor(value, cb), editText(title, text,
// cb), pickFromList(title, items, current, cb), pickPicture(cb), pickIcon(name, cb),
// editItems(kind, items, cb), editGraph(settings, cb).
FocusScope {
    id: grid

    property var rows: []
    property Menu95 popupMenu
    property var tools
    property int current: -1
    property bool showHint: true
    signal edited(string name, var value)

    readonly property var currentRow: current >= 0 && current < rows.length ? rows[current] : null
    // the current row is always in sight (another field or element picked: back to the top)
    onCurrentChanged: {
        if (current >= 0)
            view.positionViewAtIndex(current, ListView.Contain);
    }
    readonly property int rowHeight: 18
    readonly property real nameWidth: Math.round((width - 4) * 0.44)

    function isText(type) { return type === "string" || type === "int" || type === "real" || type === "color" || type === "media"; }
    function hasList(type) { return type === "bool" || type === "enum" || type === "choice"; }
    function hasDialog(type) {
        return type === "color" || type === "font" || type === "list" || type === "text" || type === "image" || type === "media" || type === "icon"
            || type === "menu" || type === "toolbar" || type === "graph";
    }

    function shown(row) {
        var v = row.value;
        switch (row.type) {
        case "bool":
            return v ? "True" : "False";
        case "list":
            return (v || []).join(", ");
        case "menu":
        case "toolbar":
            // the menus' or buttons' captions
            return (v || []).filter(function (it) { return !it.separator; })
                            .map(function (it) { return String(it.text || it.name || "").replace(/&(.)/g, "$1"); }).join(", ");
        case "text":
            return String(v === undefined || v === null ? "" : v).replace(/\n/g, " ↵ ");
        case "icon":
            // its name in the chooser: "Save"
            var all = StudioTools.icons();
            for (var k = 0; k < all.length; ++k) {
                if (all[k].name === v)
                    return all[k].label;
            }
            return v === undefined || v === null ? "" : String(v);
        case "graph":
            // its type, and what it reads: "Pie: Books by Genre"
            v = v || {};
            return (v.type || "Column") + (v.source === "code" ? ": from code"
                   : v.table ? ": " + v.table + (v.labelField ? " by " + v.labelField : "") : "");
        case "choice":
            for (var i = 0; i < (row.values || []).length; ++i) {
                if (row.values[i].value === v)
                    return row.values[i].label;
            }
            return v === undefined || v === null ? "" : String(v);
        default:
            return v === undefined || v === null ? "" : String(v);
        }
    }

    // a typed value into the row's type; undefined if it can't be
    function parsed(row, text) {
        if (row.type === "int") {
            if (text.trim() === "" && row.blank)
                return "";
            return /^\s*-?\d+\s*$/.test(text) ? parseInt(text, 10) : undefined;
        }
        if (row.type === "real") {
            if (text.trim() === "" && row.blank)
                return "";
            var n = Number(text);
            return text.trim() !== "" && isFinite(n) ? n : undefined;
        }
        if (row.type === "color") {
            var c = text.trim();
            return c === "" || StudioTools.isColor(c) ? c : undefined;
        }
        return text;
    }

    function commit(index, value) {
        var row = rows[index];
        if (!row || row.readOnly || value === undefined)
            return;
        if (JSON.stringify(value) === JSON.stringify(row.value))
            return;
        edited(row.name, value);
    }

    // the ▼ or … at the end of the row
    function openEditor(index, cell) {
        var row = rows[index];
        if (!row || row.readOnly)
            return;
        if (hasList(row.type)) {
            var entries = [], options = row.type === "bool"
                ? [{ value: true, label: "True" }, { value: false, label: "False" }]
                : row.type === "enum" ? (row.values || []).map(function (v) { return { value: v, label: v === "" ? "(none)" : v }; })
                : (row.values || []);
            if (options.length > 22 && tools) {
                tools.pickFromList(row.label, options, row.value, function (value) { grid.commit(index, value); });
                return;
            }
            for (var i = 0; i < options.length; ++i) {
                entries.push({
                    text: String(options[i].label).replace(/&/g, "&&"),
                    checked: options[i].value === row.value,
                    action: (function (value) { return function () { grid.forceActiveFocus(); grid.commit(index, value); }; })(options[i].value),
                });
            }
            if (entries.length === 0 || !popupMenu)
                return;
            var p = cell.mapToItem(popupMenu, 0, cell.height);
            popupMenu.popup(p.x, p.y, entries);
        } else if (!tools) {
            return;
        } else if (row.type === "color") {
            tools.pickColor(row.value, function (value) { grid.commit(index, value); });
        } else if (row.type === "font") {
            var fonts = [{ value: "", label: "(default)" }].concat(
                StudioTools.fontFamilies().map(function (f) { return { value: f, label: f }; }));
            tools.pickFromList(row.label, fonts, row.value, function (value) { grid.commit(index, value); });
        } else if (row.type === "list") {
            tools.editText(row.label + " (one on each line)", (row.value || []).join("\n"), function (text) {
                var items = text.split("\n").map(function (s) { return s.trim(); }).filter(function (s) { return s !== ""; });
                grid.commit(index, items);
            });
        } else if (row.type === "text") {
            tools.editText(row.label, String(row.value || ""), function (text) { grid.commit(index, text); });
        } else if (row.type === "image") {
            tools.pickPicture(function (path) { grid.commit(index, path); });
        } else if (row.type === "icon") {
            tools.pickIcon(row.value, function (name) { grid.commit(index, name); });
        } else if (row.type === "media") {
            tools.pickMedia(function (path) { grid.commit(index, path); });
        } else if (row.type === "menu" || row.type === "toolbar") {
            tools.editItems(row.type, row.value || [], function (items) { grid.commit(index, items); });
        } else if (row.type === "graph") {
            tools.editGraph(row.value || {}, function (settings) { grid.commit(index, settings); });
        }
    }

    Bevel {
        anchors.fill: parent
        anchors.bottomMargin: grid.showHint ? hint.height + 2 : 0
        style: "sunken"
        face: Theme.window
    }

    ListView {
        id: view
        x: 2
        y: 2
        width: parent.width - 4 - (bar.needed ? bar.width : 0)
        height: parent.height - 4 - (grid.showHint ? hint.height + 2 : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: grid.rows.length
        // (the arrows are the grid's: it moves `current`, whoever has the keyboard inside it)
        keyNavigationEnabled: false
        delegate: Item {
            id: rowItem
            required property int index
            readonly property var row: grid.rows[index] || ({})
            readonly property bool isCurrent: index === grid.current
            readonly property alias cell: valueCell
            function edit() {
                if (row.readOnly)
                    return;
                if (row.type === "bool")
                    grid.commit(index, !row.value);
                else if (grid.isText(row.type))
                    editor.begin();
                else
                    grid.openEditor(index, valueCell);
            }
            width: view.width
            height: grid.rowHeight

            Rectangle {
                width: grid.nameWidth
                height: parent.height - 1
                color: rowItem.isCurrent ? Theme.selection : "transparent"
                Text {
                    x: 3
                    width: parent.width - 6
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.row.label || rowItem.row.name || ""
                    elide: Text.ElideRight
                    color: rowItem.isCurrent ? Theme.selectionText : rowItem.row.error ? "#c00000" : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            // the grid's lines
            Rectangle { x: grid.nameWidth; width: 1; height: parent.height; color: Theme.face }
            Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: Theme.face }

            Item {
                id: valueCell
                x: grid.nameWidth + 1
                width: parent.width - x
                height: parent.height - 1

                Rectangle {
                    id: swatch
                    visible: rowItem.row.type === "color" && !editor.visible && rowItem.row.value !== ""
                    x: 3
                    width: 14
                    height: 10
                    anchors.verticalCenter: parent.verticalCenter
                    color: rowItem.row.value ? rowItem.row.value : "transparent"
                    border.color: Theme.text
                    border.width: 1
                }
                Icon95 {
                    id: iconShown
                    visible: rowItem.row.type === "icon" && !editor.visible && known
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    name: rowItem.row.type === "icon" ? rowItem.row.value || "" : ""
                }
                Text {
                    visible: !editor.visible
                    x: swatch.visible ? 21 : iconShown.visible ? 22 : 3
                    width: parent.width - x - (button.visible ? button.width + 2 : 2)
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        var s = grid.shown(rowItem.row);
                        return s === "" && rowItem.row.placeholder ? rowItem.row.placeholder : s;
                    }
                    elide: Text.ElideRight
                    color: rowItem.row.readOnly || (grid.shown(rowItem.row) === "" && rowItem.row.placeholder) ? Theme.shadow : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: {
                        grid.current = rowItem.index;
                        grid.forceActiveFocus();
                        if (rowItem.row.readOnly)
                            return;
                        if (grid.isText(rowItem.row.type))
                            editor.begin();
                    }
                    onDoubleClicked: {
                        if (rowItem.row.readOnly)
                            return;
                        if (rowItem.row.type === "bool")
                            grid.commit(rowItem.index, !rowItem.row.value);
                        else if (!grid.isText(rowItem.row.type))
                            grid.openEditor(rowItem.index, valueCell);
                    }
                }

                TextInput {
                    id: editor
                    visible: false
                    x: 3
                    width: parent.width - 6 - (button.visible ? button.width : 0)
                    height: parent.height
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    selectByMouse: true
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.selectionText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize

                    function begin() {
                        text = grid.shown(rowItem.row);
                        // (a row that starts from what it says in grey)
                        if (text === "" && rowItem.row.editPlaceholder && rowItem.row.placeholder)
                            text = rowItem.row.placeholder;
                        visible = true;
                        forceActiveFocus();
                        selectAll();
                    }
                    // the edit ends when the keyboard leaves (a click elsewhere)
                    function end(keep) {
                        if (!visible)
                            return;
                        visible = false;
                        if (keep)
                            grid.commit(rowItem.index, grid.parsed(rowItem.row, text));
                    }
                    // Enter or Escape: the keyboard to the grid, then the value
                    // (last: it can put up a message box, which wants the
                    // keyboard, and a change to the rows makes this row anew)
                    function finish(keep) {
                        if (!visible)
                            return;
                        var g = grid, index = rowItem.index, value = keep ? grid.parsed(rowItem.row, text) : undefined;
                        visible = false;
                        focus = false;
                        g.forceActiveFocus();
                        if (keep)
                            g.commit(index, value);
                    }
                    onAccepted: finish(true)
                    Keys.onEscapePressed: finish(false)
                    onActiveFocusChanged: {
                        if (!activeFocus)
                            end(true);
                    }
                }

                Item {
                    id: button
                    visible: rowItem.isCurrent && !rowItem.row.readOnly
                             && (grid.hasList(rowItem.row.type) || grid.hasDialog(rowItem.row.type))
                    anchors.right: parent.right
                    width: 16
                    height: parent.height
                    Bevel { anchors.fill: parent; style: buttonMouse.pressed ? "thinSunken" : "raised" }
                    Text {
                        anchors.centerIn: parent
                        text: grid.hasList(rowItem.row.type) ? "▼" : "…"
                        font.pixelSize: grid.hasList(rowItem.row.type) ? 7 : 10
                        font.bold: true
                        color: Theme.text
                    }
                    MouseArea {
                        id: buttonMouse
                        anchors.fill: parent
                        onClicked: {
                            editor.end(false);
                            grid.openEditor(rowItem.index, valueCell);
                        }
                    }
                }
            }

            MouseArea {
                width: grid.nameWidth
                height: parent.height
                onPressed: {
                    grid.current = rowItem.index;
                    grid.forceActiveFocus();
                }
                onDoubleClicked: {
                    if (rowItem.row.readOnly)
                        return;
                    if (rowItem.row.type === "bool")
                        grid.commit(rowItem.index, !rowItem.row.value);
                    else if (grid.isText(rowItem.row.type))
                        editor.begin();
                    else
                        grid.openEditor(rowItem.index, valueCell);
                }
            }
        }
    }
    ScrollBar95 {
        id: bar
        flickable: view
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: view.height
    }

    // what the current property is for (and what's wrong with its value)
    Item {
        id: hint
        visible: grid.showHint
        anchors.bottom: parent.bottom
        width: parent.width
        height: 46
        Bevel { anchors.fill: parent; style: "thinSunken" }
        Text {
            x: 5
            y: 4
            width: parent.width - 10
            text: grid.currentRow ? (grid.currentRow.label || grid.currentRow.name) : ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
            color: Theme.text
            elide: Text.ElideRight
        }
        Text {
            x: 5
            y: 18
            width: parent.width - 10
            height: parent.height - 20
            text: grid.currentRow ? (grid.currentRow.error || grid.currentRow.hint || "") : ""
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize - 1
            color: grid.currentRow && grid.currentRow.error ? "#c00000" : Theme.text
        }
    }

    Keys.onPressed: (event) => {
        // F4 or Alt+Down: the current row's list, dialog or edit (Enter
        // stays the dialog's, as its default button)
        if (event.key === Qt.Key_F4 || (event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier))) {
            var item = view.itemAtIndex(current);
            if (item)
                item.edit();
            event.accepted = true;
            return;
        }
        if (event.key === Qt.Key_Down)
            current = Math.min(rows.length - 1, current + 1);
        else if (event.key === Qt.Key_Up)
            current = Math.max(0, current - 1);
        else
            return;
        view.positionViewAtIndex(current, ListView.Contain);
        event.accepted = true;
    }
}
