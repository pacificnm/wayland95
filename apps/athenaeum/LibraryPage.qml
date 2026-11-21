import QtQuick
import Win95Ui

// A library's home: find an article by title, A to Z, a random article,
// recently read, and Wikipedia's own overview articles ("Outline of ...",
// "List of ...", "Timeline of ..."), all read straight from the ZIM.
Page95 {
    id: page
    required property var libraryId
    property var info: LibraryBrowser.libraryInfo(libraryId)
    property var overviews: LibraryBrowser.overviews(libraryId)
    readonly property bool available: info.available === true
    property var recent: LibraryBrowser.recent(libraryId)
    property var suggestions: []
    title: info.title !== undefined ? Util.pageTitle(info.title) : "Removed library"
    actions: [
        { text: "&A to Z", enabled: available, action: aToZ },
        { text: "&Random Article", shortcut: "Ctrl+R", enabled: available, action: randomArticle },
        { text: "&Search the Text...", enabled: available, action: function () { window.openSearch(field.text, page.libraryId); } },
    ]
    onShown: recent = LibraryBrowser.recent(libraryId)

    function reload() {
        info = LibraryBrowser.libraryInfo(libraryId);
        overviews = available ? LibraryBrowser.overviews(libraryId) : [];
    }
    Connections {
        target: LibraryManager.installed
        function onModelReset() { page.reload(); }
        function onRowsRemoved() { page.reload(); }
        function onDataChanged() { page.reload(); }
    }
    function aToZ() { window.openTitles(libraryId, info.title + ": A to Z", "A", false); }
    function randomArticle() {
        var a = LibraryBrowser.randomArticle(libraryId);
        if (a.path)
            window.openArticle(libraryId, a.path);
        else
            window.notify("This library has no articles to pick from.");
    }
    Shortcut { sequence: "Ctrl+R"; enabled: page.visible && page.available && !window.modalOpen; onActivated: page.randomArticle() }

    // the library
    Image {
        id: libIcon
        x: 6
        y: 6
        width: 48
        height: 48
        source: page.info.icon || ""
        fillMode: Image.PreserveAspectFit
        visible: status === Image.Ready
    }
    AthenaeumIcon { x: 6; y: 6; size: 48; name: "book"; visible: !libIcon.visible }
    Heading95 { id: head; x: 64; y: 4; width: parent.width - 70; level: 1; text: page.info.title || "" }
    Label95 {
        id: desc
        x: 64
        y: head.y + head.height + 2
        width: parent.width - 70
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: page.info.description || ""
    }
    Label95 {
        id: meta
        x: 64
        y: desc.y + desc.height + 2
        width: parent.width - 70
        dim: true
        text: Util.metaLine(page.info.flavour, page.info.size, page.info.articleCount, page.info.date)
    }
    Label95 {
        id: warning
        x: 6
        y: Math.max(meta.y + meta.height, 56) + 4
        width: parent.width - 12
        wrapMode: Text.Wrap
        color: "#800000"
        visible: text !== ""
        height: visible ? implicitHeight : 0
        text: page.info.libraryId === undefined ? "This library has been removed."
            : !page.available ? "This library's file is missing. Add it again in the Library Manager."
            : page.info.hasFulltext === false ? "This library has no full-text index, so articles can only be found by title." : ""
    }

    // find by title
    Row {
        id: findRow
        x: 6
        y: warning.y + warning.height + 8
        spacing: 6
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Find an article:" }
        TextField95 {
            id: field
            width: page.width - 330
            height: 22
            enabled: page.available
            focus: true
            onTextChanged: page.suggestions = text.trim() !== "" ? LibraryBrowser.suggest(page.libraryId, text) : []
            onAccepted: {
                if (titleWell.currentItem && titleWell.list.activeFocus)
                    window.openArticle(page.libraryId, titleWell.currentItem.path);
                else if (text.trim() !== "")
                    window.openSearch(text, page.libraryId);
            }
            input.Keys.onDownPressed: { titleWell.forceActiveFocus(); if (titleWell.currentIndex < 0) titleWell.currentIndex = 0; }
        }
        Button95 { width: 70; height: 22; text: "Search"; enabled: page.available && field.text.trim() !== ""; onClicked: window.openSearch(field.text, page.libraryId) }
        Button95 { width: 60; height: 22; text: "A to Z"; enabled: page.available; onClicked: page.aToZ() }
        Button95 { width: 70; height: 22; text: "Random"; enabled: page.available; onClicked: page.randomArticle() }
    }

    // titles as you type, or the library's lists of articles
    ListWell {
        id: titleWell
        visible: field.text.trim() !== ""
        x: 0
        y: findRow.y + 30
        width: parent.width
        height: parent.height - y
        model: page.suggestions
        emptyText: "No article titles start like that. Press Enter to search the text of this library."
        columns: [{ title: "Titles starting “" + field.text.trim() + "”", flex: true,
                    icon: function () { return "article"; }, text: function (a) { return a.title; } }]
        onActivated: (i, a) => window.openArticle(page.libraryId, a.path)
    }
    Bevel {
        visible: !titleWell.visible
        y: titleWell.y
        width: parent.width
        height: titleWell.height
        style: "sunken"
        face: Theme.window
        Flickable {
            id: homeFlick
            x: 2
            y: 2
            width: parent.width - 4 - (homeBar.visible ? homeBar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            contentHeight: home.implicitHeight + 16
            Column {
                id: home
                x: 10
                y: 8
                width: parent.width - 20
                spacing: 6
                Heading95 { level: 3; text: "Recently read"; visible: page.recent.length > 0 }
                ChipFlow {
                    width: parent.width
                    visible: page.recent.length > 0
                    entries: page.recent
                    onPicked: (e) => window.openArticle(page.libraryId, e.path)
                }
                Repeater {
                    model: page.overviews
                    Column {
                        id: overview
                        required property var modelData
                        width: home.width
                        spacing: 4
                        Item { width: 1; height: 4 }
                        Heading95 { level: 3; text: overview.modelData.label }
                        ChipFlow {
                            width: parent.width
                            entries: overview.modelData.entries
                            more: overview.modelData.more
                            onPicked: (e) => window.openArticle(page.libraryId, e.path)
                            onShowAll: window.openTitles(page.libraryId, overview.modelData.label, overview.modelData.prefix, true)
                        }
                    }
                }
                Label95 {
                    visible: page.overviews.length === 0 && page.recent.length === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    dim: true
                    text: page.available ? "Start with A to Z, a random article, or a title above." : ""
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    homeFlick.contentY = Math.max(0, Math.min(homeFlick.contentHeight - homeFlick.height, homeFlick.contentY - event.angleDelta.y / 120 * 48));
                }
            }
        }
        ScrollBar95 {
            id: homeBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: homeFlick
        }
    }
}
