import QtQuick
import Win95Ui

// A to Z (one letter at a time), or every title with a prefix ("List of ").
// Titles load a hundred at a time as the list is scrolled.
Page95 {
    id: page
    required property var libraryId
    required property string label
    required property string key        // a letter, or a title prefix
    required property bool isPrefix
    property string current: key
    property int next: 0
    title: Util.pageTitle(label)

    function reload() {
        titlesModel.clear();
        next = 0;
        loadMore();
        well.currentIndex = titlesModel.count ? 0 : -1;
    }
    function loadMore() {
        if (next < 0)
            return;
        var p = LibraryBrowser.titles(libraryId, current, isPrefix, next, 100);
        for (var i = 0; i < p.entries.length; ++i)
            titlesModel.append(p.entries[i]);
        next = p.next;
    }
    Component.onCompleted: reload()
    ListModel { id: titlesModel }

    Row {
        id: letters
        visible: !page.isPrefix
        height: visible ? 24 : 0
        spacing: 1
        Repeater {
            model: ["#"].concat("ABCDEFGHIJKLMNOPQRSTUVWXYZ".split(""))
            ToolButton95 {
                required property string modelData
                width: Math.max(18, Math.floor((page.width - 27) / 27))
                height: 22
                text: modelData
                checked: page.current === modelData
                onClicked: { page.current = modelData; page.reload(); }
            }
        }
    }
    ListWell {
        id: well
        y: letters.height + (letters.visible ? 4 : 0)
        width: parent.width
        height: parent.height - y
        focus: true
        model: titlesModel
        emptyText: page.isPrefix ? "No articles" : "No articles under " + page.current
        columns: [{ title: "Article", flex: true, icon: function () { return "article"; }, text: function (a) { return a.title; } }]
        onActivated: (i, a) => window.openArticle(page.libraryId, a.path)
        onCurrentIndexChanged: if (currentIndex >= titlesModel.count - 5) page.loadMore()
    }
    Connections {
        target: well.list
        function onAtYEndChanged() { if (well.list.atYEnd) page.loadMore(); }
    }
}
