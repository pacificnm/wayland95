import QtQuick
import Win95Ui

// Every saved chat, newest first, with its sources and citations: reopen,
// rename, add to a project or delete.
Page95 {
    id: page
    title: "Chats"
    actions: [
        { text: "&Open", enabled: !!well.currentItem, action: function () { open(well.currentItem); } },
        { text: "&Rename...", enabled: !!well.currentItem, action: function () { rename(well.currentItem); } },
        { text: "Add to &Project...", enabled: !!well.currentItem && well.currentItem.projectId <= 0, action: function () { addToProject(well.currentItem); } },
        { text: "&Delete", shortcut: "Del", enabled: !!well.currentItem, action: function () { remove(well.currentItem); } },
    ]
    onShown: chats.refresh()
    onVisibleChanged: if (visible) chats.refresh()

    ChatList { id: chats }

    function open(c) { if (c) window.openChat(c.chatId, c.articleLibraryId, c.articlePath, c.projectId, true); }
    function rename(c) {
        var id = c.chatId;
        window.ask("Rename Chat", "New name:", c.title, function (t) { if (t.trim() !== "") chats.rename(id, t.trim()); });
    }
    function addToProject(c) {
        var id = c.chatId;
        window.pickProject("Add Chat to Project", "", false, [], function (projectId, projectTitle) {
            if (projectList.attachChat(projectId, id)) {
                chats.refresh();
                window.notify("Added to “" + projectTitle + "”");
            }
        });
    }
    function remove(c) {
        var id = c.chatId;
        window.confirm("Delete Chat", "Delete “" + Util.plain(c.title) + "” and its answers? This cannot be undone.", function () { chats.remove(id); });
    }
    Shortcut { sequence: "Delete"; enabled: page.visible && !window.modalOpen && !!well.currentItem; onActivated: page.remove(well.currentItem) }

    ListWell {
        id: well
        width: parent.width
        height: parent.height - 30
        focus: true
        model: chats
        emptyText: "No chats yet.\n\nQuestions you ask are saved here with their answers and sources."
        columns: [
            { title: "Chat", flex: true, min: 180, icon: function () { return "chat"; }, text: function (c) { return c.title; } },
            { title: "About", w: 200, text: function (c) {
                return c.projectId > 0 ? "Project: " + c.projectTitle : c.articleLibraryId > 0 ? "Article: " + c.articleTitle : "Your libraries"; } },
            { title: "Questions", w: 70, right: true, text: function (c) { return c.questions; } },
            { title: "Last Asked", w: 130, text: function (c) { return Qt.formatDateTime(c.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat)); } },
        ]
        onActivated: (i, c) => page.open(c)
        onMenuRequested: (i, c, x, y) => {
            var p = mapToItem(popup, x, y);
            popup.popup(p.x, p.y, [
                { text: "&Open", action: function () { page.open(c); } },
                { text: "&Rename...", action: function () { page.rename(c); } },
                { text: "Add to &Project...", enabled: c.projectId <= 0, action: function () { page.addToProject(c); } },
                { separator: true },
                { text: "&Delete", action: function () { page.remove(c); } },
            ]);
        }
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Button95 { width: 80; text: "Open"; enabled: !!well.currentItem; onClicked: page.open(well.currentItem) }
        Button95 { width: 80; text: "Rename..."; enabled: !!well.currentItem; onClicked: page.rename(well.currentItem) }
        Button95 { width: 80; text: "Delete"; enabled: !!well.currentItem; onClicked: page.remove(well.currentItem) }
        Button95 { width: 90; text: "New Question"; onClicked: window.show("ask") }
    }
}
