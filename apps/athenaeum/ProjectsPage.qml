import QtQuick
import Win95Ui

// The research projects: each gathers articles, passages selected in them,
// answers, chats and documents around one question.
Page95 {
    id: page
    title: "Projects"
    actions: [
        { text: "&New Project...", action: newProject },
        { text: "&Open", enabled: !!well.currentItem, action: function () { window.openProject(well.currentItem.projectId); } },
        { text: "&Delete...", enabled: !!well.currentItem, action: function () { remove(well.currentItem); } },
    ]
    onShown: projects.refresh()
    onVisibleChanged: if (visible) projects.refresh()
    ProjectList { id: projects }

    function newProject() {
        newDialog.show();
        newTitle.text = "";
        newDesc.text = "";
        newTitle.input.forceActiveFocus();
    }
    function remove(p) {
        var id = p.projectId;
        window.confirm("Delete Project", "Delete “" + Util.plain(p.title) + "” with its sources, notes and documents? Its chats are kept. This cannot be undone.",
                       function () { projects.remove(id); });
    }

    ListWell {
        id: well
        width: parent.width
        height: parent.height - descLabel.height - 36
        focus: true
        model: projects
        emptyText: "No projects yet.\n\nA project gathers articles, passages you select in them, and chats around one question. Ask questions about just that material, and write up what you find."
        columns: [
            { title: "Project", flex: true, min: 180, icon: function () { return "project"; }, text: function (p) { return p.title; } },
            { title: "Articles", w: 60, right: true, text: function (p) { return p.articles; } },
            { title: "Excerpts", w: 64, right: true, text: function (p) { return p.excerpts; } },
            { title: "Chats", w: 50, right: true, text: function (p) { return p.chats; } },
            { title: "Documents", w: 74, right: true, text: function (p) { return p.documents; } },
            { title: "Changed", w: 130, text: function (p) { return Qt.formatDateTime(p.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat)); } },
        ]
        onActivated: (i, p) => window.openProject(p.projectId)
        onMenuRequested: (i, p, x, y) => {
            var q = mapToItem(popup, x, y);
            popup.popup(q.x, q.y, [
                { text: "&Open", action: function () { window.openProject(p.projectId); } },
                { separator: true },
                { text: "&Delete...", action: function () { page.remove(p); } },
            ]);
        }
    }
    Label95 {
        id: descLabel
        y: well.height + 4
        width: parent.width
        height: 30
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        text: well.currentItem ? well.currentItem.description : ""
    }
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        spacing: 6
        Button95 { width: 80; text: "Open"; enabled: !!well.currentItem; onClicked: window.openProject(well.currentItem.projectId) }
        Button95 { width: 80; text: "Delete..."; enabled: !!well.currentItem; onClicked: page.remove(well.currentItem) }
        Button95 { width: 110; text: "New Project..."; onClicked: page.newProject() }
    }

    Dialog95 {
        id: newDialog
        title: "New Project"
        dialogWidth: 400
        dialogHeight: 220
        function accept() {
            if (newTitle.text.trim() === "")
                return;
            var id = projects.create(newTitle.text.trim(), newDesc.text.trim());
            close();
            if (id > 0)
                window.openProject(id);
        }
        Keys.onEscapePressed: close()
        Label95 { x: 12; y: 12; text: "Name:" }
        TextField95 { id: newTitle; x: 12; y: 30; width: parent.width - 24; onAccepted: newDialog.accept() }
        Label95 { x: 12; y: 60; text: "Goal (what you want to find out; the project's chat answers with it in mind):" ; width: parent.width - 24; elide: Text.ElideRight }
        TextArea95 { id: newDesc; x: 12; y: 78; width: parent.width - 24; height: 80 }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; enabled: newTitle.text.trim() !== ""; onClicked: newDialog.accept() }
            Button95 { text: "Cancel"; onClicked: newDialog.close() }
        }
    }
}
