import QtQuick
import Win95Ui

// One research project: its sources (articles, saved passages, chats), a
// chat that answers from them, and documents (written in a window of their
// own, or drafted by the AI from the sources).
Page95 {
    id: page
    property alias projectId: project.projectId
    property alias project: project
    property var startChatId: 0
    readonly property var course: project.projectId > 0 && project.exists ? Classroom.courseForProject(project.projectId) : ({})
    readonly property bool drafting: Drafting.running && Drafting.projectId === project.projectId
    title: Util.pageTitle(project.title)
    actions: [
        { text: "&Edit Name and Goal...", action: edit },
        { text: "Add &Article...", action: addArticle },
        { text: "New &Document", action: newDocument },
        { text: "Draft with A&I", enabled: !Drafting.running, submenu: draftMenu() },
        { separator: true },
        { text: "&Timeline", enabled: project.articles.length > 0, action: function () { window.openTimeline({ projectId: project.projectId }); } },
        { text: "&Bibliography...", enabled: project.articles.length > 0, action: bibliography },
        { text: "E&xport...", action: exportProject },
        { separator: true },
        { text: "Delete Project...", action: removeProject },
    ]
    Component.onCompleted: if (startChatId) showChat(startChatId)
    onShown: project.refresh()

    Project { id: project }
    Conversation {
        id: conversation
        projectId: project.projectId
    }
    Connections {
        target: conversation
        function onSaved() { project.refresh(); }
    }

    function showChat(chatId) {
        tabs.currentIndex = 1;
        conversation.chatId = chatId;
    }
    function edit() {
        editDialog.show();
        editName.text = project.title;
        editGoal.text = project.description;
        editName.input.forceActiveFocus();
    }
    function addArticle() {
        addDialog.show();
        addField.text = "";
        addDialog.found = [];
        addField.input.forceActiveFocus();
    }
    function newDocument() {
        var id = project.createDocument("Untitled");
        if (id > 0)
            window.openDocument(id);
    }
    function bibliography() {
        var n = project.articles.length;
        citeDialog.openWith(project.bibliography(), (n === 1 ? "1 article" : n + " articles")
                            + ", in alphabetical order. A document's Export lists only the articles it links to.", "Bibliography");
    }
    function exportProject() {
        window.chooseFile("save", [{ label: "Markdown (*.md)", patterns: ["*.md"] }], "md", project.exportName(), function (path) {
            var error = project.exportTo("file://" + path);
            window.notify(error !== "" ? error : "Exported to " + path, error !== "");
        });
    }
    function removeProject() {
        window.confirm("Delete Project", "Delete “" + Util.plain(project.title) + "” with its sources, notes and documents? Its chats are kept. This cannot be undone.",
                       function () {
                           projectList.remove(project.projectId);
                           window.show("projects");
                       });
    }
    function draftMenu() {
        var items = [
            { text: "Write From Your &Instructions...", action: function () { reportDialog.open(); } },
            { text: "&Summary of the Project", enabled: project.articles.length + project.excerpts.length > 0,
              action: function () { Drafting.summarizeProject(project.projectId); tabs.currentIndex = 2; } },
        ];
        if (project.chats.length > 0) {
            items.push({ separator: true }, { text: "Notes from a chat:", enabled: false });
            for (var i = 0; i < project.chats.length; ++i)
                items.push({ text: "    " + Util.plain(project.chats[i].title).replace(/&/g, "&&"),
                             action: (function (id) { return function () { Drafting.summarizeChat(project.projectId, id); tabs.currentIndex = 2; }; })(project.chats[i].chatId) });
        }
        return items;
    }

    // the sources as one list
    readonly property var sources: {
        var out = [];
        var a = project.articles, e = project.excerpts, c = project.chats;
        for (var i = 0; i < a.length; ++i)
            out.push({ kind: "article", item: a[i], title: a[i].title,
                       detail: (a[i].available ? a[i].libraryTitle : (a[i].libraryTitle || "library removed") + " (not installed)") + (a[i].lesson !== "" ? " · " + a[i].lesson : ""),
                       note: a[i].note });
        for (var k = 0; k < e.length; ++k) {
            var x = e[k];
            out.push({ kind: "excerpt", item: x, title: "“" + x.text + "”",
                       detail: (x.title === "" ? "Saved answer" : x.sources > 1 ? x.title + " and " + (x.sources - 1) + (x.sources === 2 ? " other" : " others")
                               : x.section !== "" ? x.title + " – " + x.section : x.title) + (x.lesson !== "" ? " · " + x.lesson : ""),
                       note: x.note });
        }
        for (var j = 0; j < c.length; ++j)
            out.push({ kind: "chat", item: c[j], title: c[j].title,
                       detail: (c[j].itemId > 0 ? "Added from Chats · " : "Asked in this project · ") + c[j].questions + (c[j].questions === 1 ? " question" : " questions"),
                       note: "" });
        return out;
    }
    function openSource(s) {
        if (s.kind === "chat") {
            if (s.item.itemId > 0)
                window.openChat(s.item.chatId, s.item.articleLibraryId, s.item.articlePath, 0, true);
            else
                showChat(s.item.chatId);
        } else if (s.item.available) {
            window.openArticle(s.item.libraryId, s.item.path, "");
        } else {
            window.notify("Its library is not installed.");
        }
    }
    function editNote(s) {
        var id = s.item.itemId;
        inputDialog.ask("Note", "Your note on this source:", s.note, function (t) { project.setNote(id, t); }, true);
    }
    function removeSource(s) {
        if (s.kind === "chat" && s.item.itemId <= 0)
            return;
        project.removeItem(s.item.itemId);
    }

    // ---- the project
    Label95 {
        id: goal
        width: parent.width
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
        dim: true
        visible: text !== ""
        height: visible ? implicitHeight + 4 : 0
        text: project.description
    }
    Row {
        id: courseRow
        y: goal.height
        visible: page.course.id !== undefined
        height: visible ? 26 : 0
        spacing: 6
        AthenaeumIcon { y: 4; name: "school" }
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Kept for the course “" + (page.course.title || "") + "”" }
        Button95 { width: 100; height: 22; text: "Open Course"; onClicked: window.openCourse(page.course.id) }
    }
    TabWidget95 {
        id: tabs
        y: courseRow.y + courseRow.height
        width: parent.width
        height: parent.height - y
        titles: ["Sources (" + (project.articles.length + project.excerpts.length + project.chats.length) + ")", "Chat", "Documents (" + project.documents.length + ")"]

        // ---- sources
        Item {
            ListWell {
                id: sourceWell
                width: parent.width
                height: parent.height - noteText.height - 34
                model: page.sources
                emptyText: "No sources yet.\n\nOpen an article and choose “Add to Project”, or select a passage, right-click it and choose “Save to Project”."
                columns: [
                    { title: "Source", flex: true, min: 180, icon: function (s) { return s.kind === "article" ? "article" : s.kind === "excerpt" ? "quote" : "chat"; },
                      text: function (s) { return s.title; } },
                    { title: "From", w: 220, text: function (s) { return s.detail; } },
                    { title: "Note", w: 160, text: function (s) { return s.note; } },
                ]
                onActivated: (i, s) => page.openSource(s)
                onMenuRequested: (i, s, x, y) => {
                    var p = mapToItem(popup, x, y);
                    popup.popup(p.x, p.y, [
                        { text: "&Open", action: function () { page.openSource(s); } },
                        { text: "Edit &Note...", enabled: s.kind !== "chat", action: function () { page.editNote(s); } },
                        { separator: true },
                        { text: "&Remove from Project", enabled: s.kind !== "chat" || s.item.itemId > 0, action: function () { page.removeSource(s); } },
                    ]);
                }
            }
            Label95 {
                id: noteText
                y: sourceWell.height + 4
                width: parent.width
                height: 30
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: { var s = sourceWell.currentItem; return !s ? "" : s.kind === "excerpt" ? s.item.text : s.note; }
            }
            Row {
                anchors.bottom: parent.bottom
                spacing: 6
                readonly property var s: sourceWell.currentItem
                Button95 { width: 70; text: "Open"; enabled: !!parent.s; onClicked: page.openSource(parent.s) }
                Button95 { width: 80; text: "Note..."; enabled: !!parent.s && parent.s.kind !== "chat"; onClicked: page.editNote(parent.s) }
                Button95 { width: 80; text: "Remove"; enabled: !!parent.s && (parent.s.kind !== "chat" || parent.s.item.itemId > 0); onClicked: page.removeSource(parent.s) }
            }
            Row {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 6
                Button95 { width: 100; text: "Add Article..."; onClicked: page.addArticle() }
                Button95 { width: 90; text: "Bibliography..."; enabled: project.articles.length > 0; onClicked: page.bibliography() }
            }
        }

        // ---- the project's chat
        Item {
            Row {
                id: chatTools
                height: 24
                spacing: 8
                CheckBox95 {
                    y: 3
                    text: "Also search the whole <u>l</u>ibrary"
                    checked: conversation.alsoLibrary
                    onToggled: (c) => conversation.alsoLibrary = c
                }
                Button95 {
                    width: 110
                    height: 22
                    text: "Earlier Chats..."
                    enabled: project.chats.some(function (c) { return c.itemId === 0; })
                    onClicked: {
                        var items = project.chats.filter(function (c) { return c.itemId === 0; }).map(function (c) {
                            return { text: Util.plain(c.title).replace(/&/g, "&&"), checked: c.chatId === conversation.chatId,
                                     action: function () { conversation.chatId = c.chatId; } };
                        });
                        var p = mapToItem(popup, 0, height);
                        popup.popup(p.x, p.y, items);
                    }
                }
                Button95 {
                    width: 80
                    height: 22
                    text: "New Chat"
                    enabled: conversation.chatId > 0 || conversation.busy
                    onClicked: conversation.chatId = 0
                }
            }
            ChatView {
                y: chatTools.height + 4
                width: parent.width
                height: parent.height - y
                conversation: conversation
                onOpenArticle: (libraryId, path, fragment) => window.openArticle(libraryId, path, fragment)
            }
        }

        // ---- documents
        Item {
            ListWell {
                id: docWell
                width: parent.width
                height: parent.height - (draftBox.visible ? draftBox.height + 6 : 0) - 34
                model: project.documents
                emptyText: page.drafting ? "" : "No documents yet.\n\nWrite notes, an outline or a report in Markdown, citing the project's articles and excerpts, or let the AI draft one from the sources."
                columns: [
                    { title: "Document", flex: true, min: 180, icon: function () { return "document"; }, text: function (d) { return d.title; } },
                    { title: "Characters", w: 80, right: true, text: function (d) { return d.characters; } },
                    { title: "Changed", w: 140, text: function (d) { return Qt.formatDateTime(d.updated, Qt.locale().dateTimeFormat(Locale.ShortFormat)); } },
                ]
                onActivated: (i, d) => window.openDocument(d.documentId)
                onMenuRequested: (i, d, x, y) => {
                    var p = mapToItem(popup, x, y);
                    popup.popup(p.x, p.y, [
                        { text: "&Open", action: function () { window.openDocument(d.documentId); } },
                        { separator: true },
                        { text: "&Delete...", action: function () { page.removeDocument(d); } },
                    ]);
                }
            }
            // an AI draft being written
            Bevel {
                id: draftBox
                visible: page.drafting
                y: docWell.height + 6
                width: parent.width
                height: 130
                style: "sunken"
                face: Theme.window
                Label95 {
                    id: draftLabel
                    x: 6
                    y: 4
                    width: parent.width - 100
                    elide: Text.ElideRight
                    text: Drafting.total > 1 ? Drafting.label + " (" + Drafting.step + " of " + Drafting.total + ")" : Drafting.label
                }
                Button95 { anchors.right: parent.right; anchors.rightMargin: 4; y: 2; width: 80; height: 20; text: "Stop"; onClicked: Drafting.stop() }
                ProgressBar95 { x: 6; y: 24; width: parent.width - 12; height: 16; value: Drafting.total > 1 ? Math.round(100 * Drafting.step / Drafting.total) : -1 }
                TextView95 {
                    x: 6
                    y: 44
                    width: parent.width - 12
                    height: parent.height - y - 20
                    textFormat: Text.MarkdownText
                    text: Util.markdown(Drafting.streamed)
                    edit.color: Theme.shadow
                }
                Label95 {
                    x: 6
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 3
                    small: true
                    dim: true
                    text: "A small local model writes the draft. It becomes a document you can edit; check it against the sources."
                }
            }
            Row {
                anchors.bottom: parent.bottom
                spacing: 6
                Button95 { width: 70; text: "Open"; enabled: !!docWell.currentItem; onClicked: window.openDocument(docWell.currentItem.documentId) }
                Button95 { width: 80; text: "Delete..."; enabled: !!docWell.currentItem; onClicked: page.removeDocument(docWell.currentItem) }
            }
            Row {
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                spacing: 6
                Button95 {
                    width: 110
                    text: "Draft with AI..."
                    enabled: !Drafting.running
                    onClicked: { var p = mapToItem(popup, 0, 0); popup.popup(p.x, p.y - 60, page.draftMenu()); }
                }
                Button95 { width: 110; text: "New Document"; onClicked: page.newDocument() }
                Button95 { width: 80; text: "Export..."; onClicked: page.exportProject() }
            }
        }
    }
    function removeDocument(d) {
        var id = d.documentId;
        window.confirm("Delete Document", "Delete “" + Util.plain(d.title) + "”? This cannot be undone.", function () { project.removeDocument(id); });
    }

    // ---- adding an article by its title
    Dialog95 {
        id: addDialog
        property var found: []
        title: "Add Article"
        dialogWidth: 460
        dialogHeight: 320
        function added(a) {
            return project.articles.some(function (x) { return x.libraryId === a.libraryId && x.path === a.path; });
        }
        function add(a) {
            if (a && !added(a) && projectList.addArticle(project.projectId, a.libraryId, a.path, a.title, ""))
                window.notify("Added \u201c" + a.title + "\u201d");
        }
        Keys.onEscapePressed: close()
        Label95 { x: 12; y: 12; text: "Article title:" }
        TextField95 {
            id: addField
            x: 12
            y: 30
            width: parent.width - 24
            onTextChanged: addDialog.found = text.trim() !== "" ? Search.suggest(text, -1) : []
            onAccepted: addDialog.add(addWell.currentItem || addDialog.found[0])
            input.Keys.onDownPressed: { addWell.forceActiveFocus(); addWell.currentIndex = Math.max(0, addWell.currentIndex); }
        }
        ListWell {
            id: addWell
            x: 12
            y: 58
            width: parent.width - 24
            height: parent.height - y - 46
            model: addDialog.found
            columns: [
                { title: "Article", flex: true, icon: function (a) { return addDialog.added(a) ? "ok" : "article"; }, text: function (a) { return a.title; } },
                { title: "Library", w: 150, text: function (a) { return addDialog.added(a) ? "already in the project" : a.libraryTitle; } },
            ]
            onActivated: (i, a) => addDialog.add(a)
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "Add"; isDefault: true; enabled: !!addWell.currentItem && !addDialog.added(addWell.currentItem); onClicked: addDialog.add(addWell.currentItem) }
            Button95 { text: "Close"; onClicked: addDialog.close() }
        }
    }

    // ---- editing the name and goal
    Dialog95 {
        id: editDialog
        title: "Edit Project"
        dialogWidth: 400
        dialogHeight: 220
        function accept() {
            if (editName.text.trim() === "")
                return;
            project.update(editName.text.trim(), editGoal.text.trim());
            close();
        }
        Keys.onEscapePressed: close()
        Label95 { x: 12; y: 12; text: "Name:" }
        TextField95 { id: editName; x: 12; y: 30; width: parent.width - 24; onAccepted: editDialog.accept() }
        Label95 { x: 12; y: 60; text: "Goal:" }
        TextArea95 { id: editGoal; x: 12; y: 78; width: parent.width - 24; height: 80 }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; enabled: editName.text.trim() !== ""; onClicked: editDialog.accept() }
            Button95 { text: "Cancel"; onClicked: editDialog.close() }
        }
    }

    // ---- a report written to the user's instructions: planned into sections, each written from its own sources
    Dialog95 {
        id: reportDialog
        title: "Write From Your Instructions"
        dialogWidth: 460
        dialogHeight: 330
        readonly property bool emptyProject: project.articles.length + project.excerpts.length === 0
        function open() {
            if (reportTitle.text === "")
                reportTitle.text = project.title;
            if (emptyProject)
                wholeLibrary.checked = true;
            show();
            instructions.edit.forceActiveFocus();
        }
        function accept() {
            if (reportTitle.text.trim() === "" || instructions.text.trim() === "" || Drafting.running)
                return;
            Drafting.writeReport(project.projectId, reportTitle.text, instructions.text, fullReport.checked, wholeLibrary.checked);
            close();
            tabs.currentIndex = 2;
        }
        Keys.onEscapePressed: close()
        Label95 { x: 12; y: 12; text: "Title:" }
        TextField95 { id: reportTitle; x: 12; y: 30; width: parent.width - 24 }
        Label95 { x: 12; y: 60; text: "Instructions:" }
        TextArea95 {
            id: instructions
            x: 12
            y: 78
            width: parent.width - 24
            height: 110
            placeholder: "For example: A full report on how Crater Lake formed. Start with Mount Mazama and the eruption, then the caldera and how the lake filled, then its water clarity and research since. Formal tone, with a short conclusion."
        }
        RadioButton95 { id: shortReport; x: 12; y: 196; text: "Short: about 3 sections"; checked: !fullReport.checked; onClicked: fullReport.checked = false }
        RadioButton95 { id: fullReport; x: 12; y: 216; text: "Full report: up to 6 longer sections (a few minutes)"; onClicked: checked = true }
        CheckBox95 {
            id: wholeLibrary
            x: 12
            y: 242
            enabled: !reportDialog.emptyProject
            text: reportDialog.emptyProject ? "Search the whole library (the project has no sources yet)" : "Also use the <u>w</u>hole library, not only the project's sources"
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "Write"; isDefault: true; enabled: reportTitle.text.trim() !== "" && instructions.text.trim() !== "" && !Drafting.running; onClicked: reportDialog.accept() }
            Button95 { text: "Cancel"; onClicked: reportDialog.close() }
        }
    }
}
