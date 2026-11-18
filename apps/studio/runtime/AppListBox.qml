import QtQuick
import Win95Ui

// ListBox: a list to pick one item from. With a DataField it works as a
// ComboBox does (a Choice's choices, a Lookup's records, or the text).
AppElement {
    id: box

    property var items: []
    property int currentIndex: -1
    signal changed()
    signal doubleClicked()

    defaultBack: Theme.window
    activeFocusOnTab: !designMode
    resetProperties: ["currentIndex"]

    readonly property string kind: !bound ? "" : fieldInfo.type === "lookup" ? "lookup"
                                 : fieldInfo.type === "choice" ? "choice" : "text"
    property var lookups: []
    function readLookups() {
        lookups = kind === "lookup" && form.db ? form.db.lookupChoices(fieldInfo.lookupTable) : [];
    }
    onKindChanged: readLookups()
    Connections {
        target: box.kind === "lookup" ? box.form.db : null
        function onChanged(table) {
            if (table === box.fieldInfo.lookupTable) {
                box.readLookups();
                box.showField();
            }
        }
    }

    readonly property var list: kind === "lookup" ? lookups.map(function (c) { return c.label; })
                              : kind === "choice" ? (fieldInfo.choices || []) : (items || [])
    readonly property string text: currentIndex >= 0 && currentIndex < list.length ? String(list[currentIndex]) : ""
    readonly property var value: currentIndex < 0 ? null
                               : kind === "lookup" ? (lookups[currentIndex] ? lookups[currentIndex].id : null) : text

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
        view.positionViewAtIndex(i, ListView.Contain);
        store(value);
        changed();
    }

    Bevel {
        anchors.fill: parent
        style: "sunken"
        face: box.fillColor
    }

    ListView {
        id: view
        x: 2
        y: 2
        width: parent.width - 4 - (bar.needed ? bar.width : 0)
        height: parent.height - 4
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: !box.designMode
        model: box.list
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool current: index === box.currentIndex
            width: view.width
            height: box.pixelSize + 5
            color: current ? Theme.selection : "transparent"
            Text {
                x: 3
                width: parent.width - 6
                anchors.verticalCenter: parent.verticalCenter
                text: String(row.modelData)
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: row.current ? Theme.selectionText : box.textColor
                font.family: box.family
                font.pixelSize: box.pixelSize
                font.bold: box.fontBold
                font.italic: box.fontItalic
                font.underline: box.fontUnderline
            }
            MouseArea {
                anchors.fill: parent
                enabled: !box.designMode
                onPressed: {
                    box.forceActiveFocus();
                    box.choose(row.index);
                }
                onDoubleClicked: box.doubleClicked()
            }
        }
    }
    ScrollBar95 {
        id: bar
        flickable: view
        anchors.right: parent.right
        anchors.rightMargin: 2
        y: 2
        height: parent.height - 4
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Down)
            choose(Math.min(list.length - 1, currentIndex + 1));
        else if (event.key === Qt.Key_Up)
            choose(Math.max(0, currentIndex - 1));
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            doubleClicked();
        else
            return;
        event.accepted = true;
    }
}
