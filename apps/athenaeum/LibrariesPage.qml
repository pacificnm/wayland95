import QtQuick
import Win95Ui

// The installed libraries (ZIM files), like a folder of them: double-click
// one to open its home page.
Page95 {
    id: page
    title: "Libraries"
    actions: [
        { text: "&Open", enabled: !!well.currentItem && !well.currentItem.missing, action: openCurrent },
        { separator: true },
        { text: "&Get More Libraries...", action: function () { window.showLibraryManager(1); } },
        { text: "&Add a ZIM File...", action: window.addZimFile },
    ]
    function openCurrent() {
        if (well.currentItem)
            window.openLibrary(well.currentItem.libraryId);
    }

    Label95 {
        id: intro
        x: 4
        y: 2
        width: parent.width - 8
        height: 20
        verticalAlignment: Text.AlignVCenter
        text: LibraryManager.installed.count > 0 ? "Double-click a library to open it."
                                                 : ""
    }
    ListWell {
        id: well
        y: 24
        width: parent.width
        height: parent.height - y - buttons.height - 6
        focus: true
        model: LibraryManager.installed
        rowHeight: 20
        emptyText: "No libraries yet.\n\nDownload a Wikipedia library from the Library Manager to start reading and asking questions, or add a ZIM file you already have."
        columns: [
            { title: "Library", flex: true, min: 180, icon: function (l) { return "book"; }, text: function (l) { return l.title; } },
            { title: "Contents", w: 150, text: function (l) { return Util.flavourLabel(l.flavour); } },
            { title: "Articles", w: 80, right: true, text: function (l) { return l.articleCount > 0 ? Number(l.articleCount).toLocaleString(Qt.locale(), "f", 0) : ""; } },
            { title: "Size", w: 72, right: true, text: function (l) { return Util.byteSize(l.size); } },
            { title: "Date", w: 80, text: function (l) { return l.date; } },
            { title: "Status", w: 120, text: function (l) {
                return l.missing ? "File missing" : l.updateVersion !== "" ? "Update: " + l.updateVersion : !l.hasFulltext ? "Titles only" : "Ready"; } },
        ]
        rowColor: function (l) { return l.missing ? Theme.shadow : Theme.text; }
        onActivated: (i, l) => { if (!l.missing) window.openLibrary(l.libraryId); }
        onMenuRequested: (i, l, x, y) => {
            var p = mapToItem(popup, x, y);
            popup.popup(p.x, p.y, [
                { text: "&Open", enabled: !l.missing, action: function () { window.openLibrary(l.libraryId); } },
                { text: "&Search This Library...", enabled: !l.missing, action: function () { window.openSearch("", l.libraryId); } },
                { separator: true },
                { text: "&Update to " + l.updateVersion, enabled: l.updateVersion !== "", action: function () { LibraryManager.updateLibrary(l.libraryId); } },
                { text: "&Details...", action: function () { window.showLibraryManager(0); } },
            ]);
        }
    }
    Label95 {
        x: 4
        anchors.top: well.bottom
        anchors.topMargin: 6
        width: parent.width - buttons.width - 12
        height: 23
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        dim: true
        text: well.currentItem ? (well.currentItem.description || "") : ""
    }
    Row {
        id: buttons
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 6
        height: 23
        Button95 { width: 100; text: "Open"; enabled: !!well.currentItem && !well.currentItem.missing; onClicked: page.openCurrent() }
        Button95 { width: 150; text: "Get More Libraries..."; onClicked: window.showLibraryManager(1) }
    }
    Component.onCompleted: if (LibraryManager.installed.count > 0) well.currentIndex = 0
}
