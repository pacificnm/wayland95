import QtQuick
import Win95Ui

// Search every installed library (or one): titles as you type, then, on
// Enter, the text of every article through the ZIMs' own word indexes,
// with the matching passage shown.
Page95 {
    id: page
    objectName: "searchPage"
    property string initialQuery: ""
    property var libraryId: -1
    property var suggestions: []
    property bool submitted: false
    property int current: -1
    title: "Search"
    actions: [
        { text: "&Search the Text", enabled: field.text.trim() !== "", action: submit },
        { text: "&Open Article", enabled: !!currentEntry, action: function () { open(currentEntry); } },
    ]

    // read again when libraries are added or removed
    readonly property var scopes: { LibraryManager.installed.count; return [{ libraryId: -1, title: "All libraries" }].concat(LibraryManager.libraryList()); }
    property int scopeIndex: 0
    readonly property var scopeId: scopes[Math.min(scopeIndex, scopes.length - 1)].libraryId
    readonly property var entries: submitted ? Search.results : suggestions
    readonly property var currentEntry: current >= 0 && current < entries.length ? entries[current] : null

    function run(query, lib) {
        field.text = query;
        setScope(lib);
        if (query.trim() !== "")
            submit();
    }
    function setScope(lib) {
        for (var i = 0; i < scopes.length; ++i)
            if (scopes[i].libraryId === lib)
                scopeIndex = i;
    }
    function submit() {
        if (field.text.trim() === "")
            return;
        submitted = true;
        current = -1;
        Search.search(field.text, scopeId);
    }
    function open(e) {
        if (e)
            window.openArticle(e.libraryId, e.path);
    }
    Component.onCompleted: {
        setScope(libraryId);
        field.text = initialQuery;
        if (initialQuery !== "")
            submit();
        field.input.forceActiveFocus();
    }

    Row {
        id: top
        spacing: 6
        height: 24
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Look for:" }
        TextField95 {
            id: field
            width: page.width - 360
            height: 22
            onTextChanged: {
                page.submitted = false;
                page.current = -1;
                page.suggestions = text.trim() !== "" ? Search.suggest(text, page.scopeId) : [];
            }
            onAccepted: page.submit()
            input.Keys.onDownPressed: { results.forceActiveFocus(); page.current = Math.max(page.current, 0); }
        }
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "In:" }
        ComboBox95 {
            width: 170
            height: 22
            popupMenu: popup
            model: page.scopes.map(function (s) { return Util.plain(s.title); })
            currentIndex: page.scopeIndex
            onActivated: (i) => { page.scopeIndex = i; if (page.submitted) page.submit(); }
        }
        Button95 { width: 80; height: 22; text: "Search"; isDefault: true; enabled: field.text.trim() !== ""; onClicked: page.submit() }
    }

    Bevel {
        id: results
        y: top.height + 6
        width: parent.width
        height: parent.height - y - foot.height - 4
        style: "sunken"
        face: Theme.window
        activeFocusOnTab: true
        onActiveFocusChanged: if (activeFocus) list.forceActiveFocus()
        ListView {
            id: list
            x: 2
            y: 2
            width: parent.width - 4 - (bar.visible ? bar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: page.entries
            currentIndex: page.current
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Down && page.current < count - 1) page.current++;
                else if (event.key === Qt.Key_Up && page.current > 0) page.current--;
                else if (event.key === Qt.Key_Up && page.current === 0) field.input.forceActiveFocus();
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) page.open(page.currentEntry);
                else return;
                event.accepted = true;
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool selected: index === page.current
                width: list.width
                height: col.implicitHeight + 8
                Rectangle { anchors.fill: parent; color: row.selected ? Theme.selection : "transparent" }
                Loader {
                    anchors.fill: parent
                    active: row.selected && list.activeFocus
                    sourceComponent: FocusRect95 { color: "#ffff80" }
                }
                Column {
                    id: col
                    x: 6
                    y: 4
                    width: parent.width - 12
                    spacing: 1
                    Item {
                        width: parent.width
                        height: titleText.implicitHeight
                        AthenaeumIcon { name: "article"; y: -1 }
                        Text {
                            id: titleText
                            x: 22
                            width: parent.width - x - libText.implicitWidth - 8
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            text: row.modelData.title
                            color: row.selected ? Theme.selectionText : "#000080"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize + 1
                            font.bold: true
                        }
                        Text {
                            id: libText
                            anchors.right: parent.right
                            visible: page.scopeId === -1
                            textFormat: Text.PlainText
                            text: row.modelData.libraryTitle || ""
                            color: row.selected ? Theme.selectionText : Theme.shadow
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize - 1
                        }
                    }
                    Text {
                        x: 22
                        width: parent.width - 22
                        visible: page.submitted && (row.modelData.section || "") !== ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        text: row.modelData.section || ""
                        color: row.selected ? Theme.selectionText : "#006000"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize - 1
                    }
                    Text {
                        x: 22
                        width: parent.width - 22
                        visible: page.submitted && text !== ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        text: row.modelData.snippet || ""
                        color: row.selected ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: { list.forceActiveFocus(); page.current = row.index; }
                    onDoubleClicked: page.open(row.modelData)
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    list.contentY = Math.max(0, Math.min(list.contentHeight - list.height, list.contentY - event.angleDelta.y / 120 * 54));
                }
            }
        }
        ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: list
            stepSize: 18
        }
        Label95 {
            visible: !Search.running && list.count === 0
            anchors.centerIn: parent
            width: parent.width - 60
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            dim: true
            text: LibraryManager.installed.count === 0 ? "No libraries installed. Add a library in the Library Manager to search it."
                : page.submitted ? "Nothing found.\n\nTry other words. Search finds articles by their words, so a question worded differently from the article can miss."
                : field.text === "" ? "Type a title or some words, then press Enter to search the text of every article."
                : "No article titles start like that. Press Enter to search the text of articles."
        }
        Label95 {
            visible: Search.running
            anchors.centerIn: parent
            text: "Searching..."
        }
    }
    Label95 {
        id: foot
        anchors.bottom: parent.bottom
        width: parent.width
        height: 18
        verticalAlignment: Text.AlignVCenter
        dim: true
        text: page.submitted && !Search.running ? (list.count === 1 ? "1 article" : list.count + " articles") + " · " + Search.elapsedMs + " ms"
            : !page.submitted && list.count > 0 ? "Titles as you type: double-click one to read it, or press Enter to search the text." : ""
    }
}
