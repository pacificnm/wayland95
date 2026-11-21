import QtQuick
import Win95Ui

// A citation (an article) or a bibliography (a project's articles) in each
// style schools ask for, to copy. The usual style (Settings) is shown
// first; another can be made the usual one from here.
//   citations: [{style, label, shortLabel, text, html}]
Dialog95 {
    id: dialog
    property var citations: []
    property string explanation: ""
    property Menu95 popupMenu
    property int index: 0
    readonly property var current: citations.length > 0 ? citations[Math.min(index, citations.length - 1)] : ({})
    title: "Cite"
    dialogWidth: 480
    dialogHeight: 300

    function open(libraryId, path, articleTitle) {
        openWith(LibraryBrowser.cite(libraryId, path, articleTitle),
                 "Check your teacher's instructions: some want the date you read it, some the address on the web. The article was read in an offline copy, which is dated.",
                 "Cite \u201c" + Util.plain(articleTitle) + "\u201d");
    }
    function openWith(list, why, heading) {
        title = heading || "Cite";
        citations = list;
        explanation = why || "";
        var i = 0;
        for (var k = 0; k < list.length; ++k)
            if (list[k].style === LibraryBrowser.citationStyle)
                i = k;
        index = i;
        tabs.currentIndex = i;
        copied.visible = false;
        show();
    }
    Keys.onEscapePressed: close()

    TabWidget95 {
        id: tabs
        x: 10
        y: 10
        width: parent.width - 20
        height: parent.height - 56
        titles: dialog.citations.map(function (c) { return c.shortLabel; })
        onCurrentIndexChanged: { dialog.index = currentIndex; copied.visible = false; }
    }
    Item {
        x: tabs.x + 8
        y: tabs.y + tabs.tabHeight + 10
        width: tabs.width - 16
        height: tabs.height - tabs.tabHeight - 18
        Label95 { id: styleLabel; width: parent.width; text: dialog.current.label || ""; dim: true }
        TextView95 {
            y: styleLabel.height + 4
            width: parent.width
            height: parent.height - y - (why.visible ? why.height + 4 : 0)
            text: dialog.current.text || ""
            edit.font.family: "Liberation Serif"
            edit.font.pixelSize: 14
        }
        Label95 {
            id: why
            anchors.bottom: parent.bottom
            width: parent.width
            visible: dialog.explanation !== ""
            small: true
            dim: true
            wrapMode: Text.Wrap
            text: dialog.explanation
        }
    }
    Row {
        x: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            text: "Copy"
            enabled: (dialog.current.text || "") !== ""
            onClicked: {
                LibraryBrowser.copyCitation(dialog.current.text, dialog.current.html);
                copied.visible = true;
            }
        }
        Button95 {
            width: 150
            text: "Use " + (dialog.current.shortLabel || "") + " by Default"
            visible: dialog.current.style !== undefined && dialog.current.style !== LibraryBrowser.citationStyle
            onClicked: LibraryBrowser.citationStyle = dialog.current.style
        }
        Label95 { id: copied; visible: false; height: 23; verticalAlignment: Text.AlignVCenter; text: "Copied, with its italics." }
    }
    Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "Close"
        isDefault: true
        onClicked: dialog.close()
    }
}
