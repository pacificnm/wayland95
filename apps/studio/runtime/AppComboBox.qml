import QtQuick
import Win95Ui

// ComboBox: a drop-down list. With a DataField its list comes from the
// field: a Choice field's choices, a Lookup's records (the field then holds
// the chosen record's id) or Yes/No; any other field holds the item's text.
AppElement {
    id: combo

    property var items: []
    property int currentIndex: -1
    signal changed()

    defaultBack: Theme.window
    activeFocusOnTab: !designMode
    resetProperties: ["currentIndex"]

    readonly property string kind: !bound ? "" : fieldInfo.type === "lookup" ? "lookup"
                                 : fieldInfo.type === "choice" ? "choice"
                                 : fieldInfo.type === "boolean" ? "boolean" : "text"
    // a lookup's [{ id, label }]; read again when that table changes
    property var lookups: []
    function readLookups() {
        lookups = kind === "lookup" && form.db ? form.db.lookupChoices(fieldInfo.lookupTable) : [];
    }
    onKindChanged: readLookups()
    Connections {
        target: combo.kind === "lookup" ? combo.form.db : null
        function onChanged(table) {
            if (table === combo.fieldInfo.lookupTable) {
                combo.readLookups();
                combo.showField();
            }
        }
    }

    readonly property var list: kind === "lookup" ? lookups.map(function (c) { return c.label; })
                              : kind === "choice" ? (fieldInfo.choices || [])
                              : kind === "boolean" ? ["Yes", "No"] : (items || [])
    readonly property string text: currentIndex >= 0 && currentIndex < list.length ? String(list[currentIndex]) : ""
    // what the field gets for the current item
    readonly property var value: currentIndex < 0 ? null
                               : kind === "lookup" ? (lookups[currentIndex] ? lookups[currentIndex].id : null)
                               : kind === "boolean" ? currentIndex === 0 : text

    function showField() {
        if (!bound)
            return;
        var v = fieldValue, index = -1;
        if (v !== undefined && v !== null) {
            if (kind === "lookup") {
                for (var i = 0; i < lookups.length; ++i) {
                    if (String(lookups[i].id) === String(v))
                        index = i;
                }
            } else if (kind === "boolean") {
                index = v ? 0 : 1;
            } else {
                index = list.indexOf(String(v));
            }
        }
        currentIndex = index;
    }
    onFieldValueChanged: showField()
    onListChanged: showField()

    function choose(i) {
        if (i < 0 || i >= list.length || i === currentIndex)
            return;
        currentIndex = i;
        store(value);
        changed();
    }

    function openList() {
        if (!form || !form.popupMenu || list.length === 0)
            return;
        var entries = [];
        for (var i = 0; i < list.length; ++i) {
            entries.push({
                text: String(list[i]).replace(/&/g, "&&"),
                checked: i === currentIndex,
                action: (function (index) { return function () { combo.choose(index); combo.forceActiveFocus(); }; })(i),
            });
        }
        var p = combo.mapToItem(form.popupMenu, 0, combo.height);
        form.popupMenu.popup(p.x, p.y, entries);
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: combo.fillColor
    }

    Rectangle {
        x: 3
        y: 3
        width: parent.width - arrow.width - 7
        height: parent.height - 6
        color: combo.activeFocus ? Theme.selection : "transparent"
        Text {
            x: 2
            width: parent.width - 4
            anchors.verticalCenter: parent.verticalCenter
            text: combo.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: combo.activeFocus ? Theme.selectionText : combo.textColor
            font.family: combo.family
            font.pixelSize: combo.pixelSize
            font.bold: combo.fontBold
            font.italic: combo.fontItalic
            font.underline: combo.fontUnderline
        }
    }

    Item {
        id: arrow
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: parent.height - 4
        Bevel { anchors.fill: parent; style: mouse.pressed ? "thinSunken" : "raised" }
        Text {
            anchors.centerIn: parent
            text: "▼"
            font.pixelSize: 7
            color: combo.enabled ? Theme.text : Theme.disabledText
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: !combo.designMode
        onPressed: {
            combo.forceActiveFocus();
            combo.openList();
        }
    }

    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Down && (event.modifiers & Qt.AltModifier)) || event.key === Qt.Key_F4)
            openList();
        else if (event.key === Qt.Key_Down)
            choose(Math.min(list.length - 1, currentIndex + 1));
        else if (event.key === Qt.Key_Up)
            choose(Math.max(0, currentIndex - 1));
        else
            return;
        event.accepted = true;
    }
}
