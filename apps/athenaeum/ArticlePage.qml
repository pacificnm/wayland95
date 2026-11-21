import QtQuick
import QtWebEngine
import Win95Ui

// An article from zim:// (sanitised by the program, JavaScript off). Links
// inside the library stay in this page, with Back and Forward; links to the
// web open in the Web Browser. Beside the article, one panel at a time: Ask
// (a chat about this article), its Notes (highlights), or the Lesson it is
// a reading for.
Page95 {
    id: page
    required property url url
    // a section to show once the article has loaded and is laid out
    property string fragment: ""
    property var info: ({})
    property string panel: lessonId > 0 ? "lesson" : ""   // "", "ask", "notes", "lesson"
    property var lessonId: 0
    property var lesson: ({})
    property var highlights: []
    property var openChatId: 0    // a saved chat about this article to show once it has loaded
    property alias conversation: conversation

    title: info.title !== undefined ? Util.pageTitle(info.found === false && conversation.articleTitle !== "" ? conversation.articleTitle : info.title) : "Loading..."
    readonly property bool found: info.found === true
    actions: [
        { text: "&Back", enabled: view.canGoBack, action: function () { view.goBack(); } },
        { text: "&Forward", enabled: view.canGoForward, action: function () { view.goForward(); } },
        { separator: true },
        { text: "&Ask About This Article", checked: panel === "ask", enabled: found, action: function () { togglePanel("ask"); } },
        { text: page.highlights.length > 0 ? "&Notes (" + page.highlights.length + ")" : "&Notes", checked: panel === "notes", enabled: found,
          action: function () { togglePanel("notes"); } },
        { text: "&Lesson", checked: panel === "lesson", enabled: lessonId > 0, action: function () { togglePanel("lesson"); } },
        { separator: true },
        { text: "&Contents", enabled: (info.toc || []).length > 0, submenu: tocItems() },
        { text: "&Cite This Article...", enabled: found, action: citeArticle },
        { text: "Add to &Project...", enabled: found, action: addToProject },
        { text: "&Timeline", enabled: found, action: function () { window.openTimeline({ libraryId: info.libraryId, path: info.path, articleTitle: info.title }); } },
        { text: "Show on Open&StreetMap", enabled: found && info.hasCoordinates === true && Connectivity.online, action: showOnMap },
        { separator: true },
        { text: "Use the &Whole Window", checked: LibraryBrowser.fullWidth, action: function () { LibraryBrowser.fullWidth = !LibraryBrowser.fullWidth; } },
        { text: "Select &All", action: function () { view.triggerWebAction(WebEngineView.SelectAll); } },
    ]

    function togglePanel(name) {
        panel = panel === name ? "" : name;
        if (panel === "ask")
            chat.focusInput();
    }
    function tocItems() {
        var toc = info.toc || [];
        return toc.map(function (t) {
            return { text: (t.level === 3 ? "      " : "") + Util.plain(t.text).replace(/&/g, "&&"), action: function () { page.jumpTo(t.id); } };
        });
    }
    function jumpTo(id) {
        view.url = LibraryBrowser.articleUrl(info.libraryId, info.path).toString() + "#" + encodeURIComponent(id);
    }
    function citeArticle() { window.cite(info.libraryId, info.path, info.title); }
    function addToProject() {
        var i = info;
        window.pickProject("Add Article to Project", "", true, projectList.containing(i.libraryId, i.path), function (projectId, projectTitle, note) {
            if (projectList.addArticle(projectId, i.libraryId, i.path, i.title, note))
                window.notify("Added to “" + projectTitle + "”");
        });
    }
    // the place on the map, in the Web Browser (only when the internet may be used, and only when asked)
    function showOnMap() {
        Qt.openUrlExternally("https://www.openstreetmap.org/?mlat=" + info.lat + "&mlon=" + info.lon + "#map=14/" + info.lat + "/" + info.lon);
        window.notify("Opened in the Web Browser");
    }

    // the chat follows the article shown: following a link to another article starts a chat about that one
    function syncChat() {
        if (!info.path || conversation.articlePath === info.path && conversation.articleLibraryId === info.libraryId)
            return;
        conversation.chatId = 0;
        conversation.articleLibraryId = info.libraryId;
        conversation.articlePath = info.path;
        conversation.articleTitle = info.title;
        if (page.openChatId) {
            conversation.chatId = page.openChatId;
            page.openChatId = 0;
            page.panel = "ask";
        }
    }
    onInfoChanged: {
        syncChat();
        refreshNotes();
    }
    function refreshLesson() { page.lesson = page.lessonId > 0 ? Classroom.lesson(page.lessonId) : {}; }
    // the questions to read with first, where they are easy to refer back to, then the rest of the introduction
    readonly property var lessonParts: {
        var intro = page.lesson.intro || "";
        var m = intro.match(/(^|\n)##\s*As you read[^\n]*\n([\s\S]*?)(?=\n##\s|$)/i);
        if (!m)
            return { questions: "", rest: intro };
        return { questions: m[2].trim(), rest: (intro.slice(0, m.index) + intro.slice(m.index + m[0].length)).trim() };
    }
    onLessonIdChanged: refreshLesson()
    Component.onCompleted: refreshLesson()
    Connections {
        target: Classroom
        enabled: page.lessonId > 0
        function onCoursesChanged() { page.refreshLesson(); }
        function onIntroReady(lessonId) { if (lessonId === page.lessonId) page.refreshLesson(); }
    }
    function refreshNotes() { page.highlights = page.found ? Notes.forArticle(page.info.libraryId, page.info.path) : []; }
    function showHighlight(id) { jumpTo("hl-" + id); }
    // marked in the page once it is saved (Notes.changed reloads the article)
    function highlight(text, color, note) {
        if (Notes.add(page.info.libraryId, page.info.path, page.info.title, text, color, note || "") <= 0)
            return;
        // reading for a lesson whose course is kept in a project: the highlight goes there too
        if (page.lessonId > 0 && Classroom.keepHighlight(page.lessonId, page.info.libraryId, page.info.path, page.info.title, text, note || ""))
            window.notify("Highlighted, and saved to “" + page.lesson.projectTitle + "”");
        else if (page.panel !== "notes")
            window.notify("Highlighted. Your notes are under Notes.", false, "Show", function () { if (page) page.panel = "notes"; });
    }
    // a citation in the chat: a section of this article scrolls here; another article opens on its own page
    function showSource(libraryId, path, fragment) {
        if (libraryId !== info.libraryId || path !== info.path)
            window.openArticle(libraryId, path, fragment);
        else
            jumpTo(fragment);
    }

    Connections {
        target: LibraryBrowser
        function onFullWidthChanged() { view.reload(); }
    }
    // a highlight changed here or on the Notes page: the panel and the marks follow
    Connections {
        target: Notes
        function onChanged() {
            var key = function (list) { return JSON.stringify(list.map(function (h) { return [h.id, h.color, h.note]; })); };
            var before = key(page.highlights);
            page.refreshNotes();
            if (key(page.highlights) !== before)
                view.reload();
        }
    }
    // a different colour scheme: the article in the new colours
    Connections {
        target: Wayland95Settings
        function onChanged() { view.reload(); }
    }

    Conversation { id: conversation }
    ChatList {
        id: articleChats
        articleLibraryId: page.info.libraryId || 0
        articlePath: page.info.path || ""
    }
    Connections {
        target: conversation
        function onSaved() { articleChats.refresh(); }
    }

    // ---- the article's own toolbar
    Row {
        id: tools
        height: 26
        spacing: 2
        IconButton { icon: "back"; flat: true; enabled: view.canGoBack; onClicked: view.goBack() }
        IconButton { icon: "forward"; flat: true; enabled: view.canGoForward; onClicked: view.goForward() }
        Item { width: 6; height: 1 }
        IconButton {
            icon: "bullets"
            text: "Contents"
            flat: true
            enabled: (page.info.toc || []).length > 0
            onClicked: { var p = mapToItem(popup, 0, height); popup.popup(p.x, p.y, page.tocItems()); }
        }
        IconButton { icon: "chat"; text: "Ask"; flat: true; checked: page.panel === "ask"; enabled: page.found; onClicked: page.togglePanel("ask") }
        IconButton {
            icon: "highlight"
            text: page.highlights.length > 0 ? "Notes (" + page.highlights.length + ")" : "Notes"
            flat: true
            checked: page.panel === "notes"
            enabled: page.found
            onClicked: page.togglePanel("notes")
        }
        IconButton { icon: "school"; text: "Lesson"; flat: true; visible: page.lessonId > 0; checked: page.panel === "lesson"; onClicked: page.togglePanel("lesson") }
        IconButton { icon: "cite"; text: "Cite"; flat: true; enabled: page.found; onClicked: page.citeArticle() }
        IconButton { icon: "project"; text: "Add to Project"; flat: true; enabled: page.found; onClicked: page.addToProject() }
        IconButton {
            icon: "timeline"
            text: "Timeline"
            flat: true
            enabled: page.found
            onClicked: window.openTimeline({ libraryId: page.info.libraryId, path: page.info.path, articleTitle: page.info.title })
        }
    }

    // ---- the article
    Bevel {
        id: frame
        y: tools.height + 2
        width: page.panel !== "" ? Math.round(parent.width * 0.62) : parent.width
        height: parent.height - y
        style: "sunken"
        WebEngineView {
            id: view
            x: 2
            y: 2
            width: parent.width - 4
            height: parent.height - 4
            url: page.url
            settings.javascriptEnabled: false
            settings.javascriptCanOpenWindows: false
            settings.pluginsEnabled: false
            settings.localStorageEnabled: false
            settings.localContentCanAccessRemoteUrls: false
            settings.localContentCanAccessFileUrls: false
            settings.navigateOnDropEnabled: false  // a file dropped on the article must not replace it
            onLoadingChanged: (loadingInfo) => {
                if (loadingInfo.status === WebEngineView.LoadSucceededStatus && page.fragment !== "")
                    fragmentTimer.start();
            }
            onUrlChanged: {
                if (LibraryBrowser.isArticleUrl(url))
                    page.info = LibraryBrowser.articleInfo(url);
            }
            onLinkHovered: (hoveredUrl) => {
                var s = hoveredUrl.toString();
                window.hoverText = s === "" ? "" : s.indexOf("zim:") === 0 ? (LibraryBrowser.articleInfo(hoveredUrl).title || "") : s;
            }
            // only zim:// is shown here; a link to the web goes to the Web Browser, anything else nowhere
            function openOutside(target) {
                var s = target.toString();
                if (/^(https?|mailto):/i.test(s)) {
                    Qt.openUrlExternally(target);
                    window.notify("Opened in the Web Browser");
                }
            }
            onNavigationRequested: function (request) {
                if (request.url.toString().indexOf("zim:") === 0)
                    return;
                request.reject();
                // only a link the reader clicked opens in the browser, never a page navigating by itself
                if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation)
                    openOutside(request.url);
            }
            onNewWindowRequested: function (request) {
                if (request.requestedUrl.toString().indexOf("zim:") === 0)
                    view.url = request.requestedUrl;
                else
                    openOutside(request.requestedUrl);
            }
            // always the program's own menu: Chromium's (Save Page, View Page Source) has no place here
            onContextMenuRequested: function (request) {
                request.accepted = true;
                var p = view.mapToItem(popup, request.position.x, request.position.y);
                var sel = request.selectedText.trim();
                popup.popup(p.x, p.y, sel !== "" ? page.selectionMenu(sel) : page.pageMenu(request.linkUrl.toString()));
            }
        }
    }

    // a right-click with nothing selected: a link, or the page
    function pageMenu(link) {
        var items = [];
        if (link !== "")
            items.push({ text: "&Open Link", action: function () { link.indexOf("zim:") === 0 ? view.url = link : view.openOutside(link); } }, { separator: true });
        return items.concat([
            { text: "&Back", enabled: view.canGoBack, action: function () { view.goBack(); } },
            { text: "&Forward", enabled: view.canGoForward, action: function () { view.goForward(); } },
            { separator: true },
            { text: "&Ask About This Article", enabled: found, action: function () { panel = "ask"; chat.focusInput(); } },
            { text: page.highlights.length > 0 ? "&Notes (" + page.highlights.length + ")" : "&Notes", enabled: found, action: function () { panel = "notes"; } },
            { text: "&Cite This Article...", enabled: found, action: citeArticle },
            { text: "C&ontents", enabled: (info.toc || []).length > 0, submenu: tocItems() },
            { separator: true },
            { text: "Select &All", action: function () { view.triggerWebAction(WebEngineView.SelectAll); } },
        ]);
    }
    // selected text: copy, define, highlight, save, or ask about it
    function selectionMenu(sel) {
        var word = sel.length <= 40 && sel.split(/\s+/).length <= 3;
        var colours = Notes.colors.map(function (c) {
            return { text: Notes.colorName(c), action: function () { page.highlight(sel, c, ""); } };
        });
        var items = [{ text: "&Copy", shortcut: "Ctrl+C", action: function () { view.triggerWebAction(WebEngineView.Copy); } }];
        if (word)
            items.push({ text: "&Define “" + Util.plain(sel).replace(/&/g, "&&") + "”", action: function () { window.define(sel); } });
        return items.concat([
            { separator: true },
            { text: "&Highlight", submenu: colours },
            { text: "Highlight with &Note...", action: function () { noteDialog.ask(sel); } },
            { text: "&Save to Project...", action: function () {
                var i = page.info;
                window.pickProject("Save Passage to Project", sel, true, [], function (projectId, projectTitle, note) {
                    if (projectList.addExcerpt(projectId, sel, i.libraryId, i.path, i.title, "", note)) {
                        window.notify("Saved to “" + projectTitle + "”");
                        view.reload(); // shows it marked
                    }
                });
            } },
            { text: "As&k About This", action: function () {
                page.panel = "ask";
                var quote = sel.length > 300 ? sel.slice(0, 300) + "…" : sel;
                chat.setQuestion("What does the article mean by “" + quote + "”?");
            } },
        ]);
    }
    Shortcut { sequence: "Alt+Right"; enabled: page.visible && !window.modalOpen; onActivated: view.goForward() }

    // ---- the panel beside it
    Item {
        id: side
        visible: page.panel !== ""
        x: frame.width + 4
        y: frame.y
        width: parent.width - x
        height: frame.height

        // its title, like a docked window's
        Rectangle {
            id: sideTitle
            width: parent.width
            height: 20
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.titleActiveStart }
                GradientStop { position: 1; color: Theme.titleActiveEnd }
            }
            Text {
                x: 6
                width: parent.width - 60
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                textFormat: Text.PlainText
                color: Theme.titleActiveText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
                text: page.panel === "ask" ? (conversation.chatId > 0 ? conversation.title : "Ask about this article")
                    : page.panel === "notes" ? "Notes in this article"
                    : "Lesson " + (page.lesson.number || 0) + " of " + (page.lesson.count || 0) + ": " + (page.lesson.title || "")
            }
            ToolButton95 {
                anchors.right: parent.right
                anchors.rightMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 14
                glyph: "×"
                onClicked: page.panel = ""
            }
        }

        // Ask
        Item {
            visible: page.panel === "ask"
            y: sideTitle.height + 4
            width: parent.width
            height: parent.height - y
            Row {
                id: chatTools
                spacing: 4
                height: 24
                Button95 {
                    width: 110
                    height: 22
                    text: "Earlier Chats..."
                    enabled: articleChats.count > 0
                    onClicked: {
                        articleChats.refresh();
                        var items = [];
                        for (var i = 0; i < articleChats.count; ++i) {
                            var c = articleChats.data(articleChats.index(i, 0), 258); // ChatList's TitleRole (after ChatIdRole)
                            var id = articleChats.data(articleChats.index(i, 0), 257);
                            items.push({ text: Util.plain(c).replace(/&/g, "&&"), checked: id === conversation.chatId,
                                         action: (function (chatId) { return function () { conversation.chatId = chatId; }; })(id) });
                        }
                        var p = mapToItem(popup, 0, height);
                        popup.popup(p.x, p.y, items);
                    }
                }
                Button95 {
                    width: 80
                    height: 22
                    text: "New Chat"
                    enabled: conversation.chatId > 0 || conversation.busy
                    onClicked: { conversation.chatId = 0; chat.focusInput(); }
                }
            }
            ChatView {
                id: chat
                y: chatTools.height + 2
                width: parent.width
                height: parent.height - y
                conversation: conversation
                onOpenArticle: (libraryId, path, fragment) => page.showSource(libraryId, path, fragment)
            }
        }

        // Notes
        Bevel {
            visible: page.panel === "notes"
            y: sideTitle.height + 4
            width: parent.width
            height: parent.height - y
            style: "sunken"
            face: Theme.face
            Flickable {
                id: notesFlick
                x: 2
                y: 2
                width: parent.width - 4 - (notesBar.visible ? notesBar.width : 0)
                height: parent.height - 4
                clip: true
                interactive: false
                contentHeight: notesCol.implicitHeight + 12
                Column {
                    id: notesCol
                    x: 6
                    y: 6
                    width: parent.width - 12
                    spacing: 6
                    Repeater {
                        model: page.highlights
                        HighlightCard {
                            required property var modelData
                            width: notesCol.width
                            highlight: modelData
                            onGoTo: page.showHighlight(modelData.id)
                            onAddToProject: {
                                var id = modelData.id;
                                window.pickProject("Save Highlight to Project", modelData.text, false, [], function (projectId, projectTitle) {
                                    if (Notes.addToProject(projectId, id))
                                        window.notify("Saved to “" + projectTitle + "”");
                                });
                            }
                        }
                    }
                    Label95 {
                        visible: page.highlights.length === 0
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "No highlights yet.\n\nSelect a passage, right-click it and choose a colour under Highlight. Add a note to say why it matters."
                    }
                }
                WheelHandler {
                    onWheel: (event) => {
                        notesFlick.contentY = Math.max(0, Math.min(notesFlick.contentHeight - notesFlick.height, notesFlick.contentY - event.angleDelta.y / 120 * 48));
                    }
                }
            }
            ScrollBar95 {
                id: notesBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: notesFlick
            }
        }

        // the lesson: what the teacher asked to look for, beside the reading
        Item {
            visible: page.panel === "lesson"
            y: sideTitle.height + 4
            width: parent.width
            height: parent.height - y
            TextView95 {
                id: lessonText
                width: parent.width
                height: parent.height - lessonButtons.height - 6
                textFormat: Text.MarkdownText
                text: {
                    var l = page.lesson;
                    if ((l.intro || "") === "")
                        return "*The teacher has not written this lesson's introduction yet. Open the lesson to have it written.*";
                    var md = "";
                    if (l.objective)
                        md += "*" + Util.markdown(l.objective) + "*\n\n";
                    if (page.lessonParts.questions !== "")
                        md += "## As you read\n\n" + Util.markdown(page.lessonParts.questions) + "\n\n";
                    md += Util.markdown(page.lessonParts.rest);
                    var r = l.readings || [];
                    if (r.length > 1) {
                        md += "\n\n## Readings\n\n";
                        for (var i = 0; i < r.length; ++i) {
                            var cur = r[i].libraryId === page.info.libraryId && r[i].path === page.info.path;
                            var t = r[i].n + ". " + Util.markdown(r[i].title).replace(/[\[\]]/g, "");
                            md += "- " + (cur ? "**" + t + "** (this one)" : r[i].available ? "[" + t + "](reading:" + i + ")" : t) + "\n";
                        }
                    }
                    return md;
                }
                onLinkActivated: (link) => {
                    if (link.indexOf("reading:") === 0) {
                        var r = page.lesson.readings[parseInt(link.slice(8))];
                        view.url = LibraryBrowser.articleUrl(r.libraryId, r.path);
                    }
                }
            }
            Row {
                id: lessonButtons
                anchors.bottom: parent.bottom
                spacing: 4
                Button95 {
                    width: 170
                    text: page.lesson.completed ? "Mark as Not Done" : "I've Finished This Lesson"
                    onClicked: Classroom.setCompleted(page.lessonId, !page.lesson.completed)
                }
                Button95 {
                    width: 110
                    visible: (page.lesson.projectId || 0) > 0
                    text: "Open Project"
                    onClicked: window.openProject(page.lesson.projectId)
                }
            }
        }
    }

    // the passage highlighted with a note at once
    Dialog95 {
        id: noteDialog
        property string passage: ""
        property string color: "yellow"
        title: "Highlight with Note"
        dialogWidth: 400
        dialogHeight: 250
        function ask(text) {
            passage = text;
            noteArea.text = "";
            show();
            noteArea.edit.forceActiveFocus();
        }
        function accept() { close(); page.highlight(passage, color, noteArea.text); }
        Keys.onEscapePressed: close()
        Label95 {
            id: quoted
            x: 12
            y: 10
            width: parent.width - 24
            height: Math.min(implicitHeight, 50)
            clip: true
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 3
            font.italic: true
            text: "“" + (noteDialog.passage.length > 280 ? noteDialog.passage.slice(0, 280) + "…" : noteDialog.passage) + "”"
        }
        TextArea95 {
            id: noteArea
            x: 12
            y: quoted.y + quoted.height + 8
            width: parent.width - 24
            height: 80
            placeholder: "Why this matters, a question it raises..."
            onCtrlReturn: noteDialog.accept()
        }
        Row {
            x: 12
            y: noteArea.y + noteArea.height + 8
            spacing: 4
            Label95 { height: 18; verticalAlignment: Text.AlignVCenter; text: "Colour:" }
            Repeater {
                model: Notes.colors
                Rectangle {
                    required property string modelData
                    width: 18
                    height: 18
                    color: Notes.swatch(modelData)
                    border.color: noteDialog.color === modelData ? "#000000" : Theme.shadow
                    border.width: noteDialog.color === modelData ? 2 : 1
                    MouseArea { anchors.fill: parent; onClicked: noteDialog.color = parent.modelData }
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: noteDialog.accept() }
            Button95 { text: "Cancel"; onClicked: noteDialog.close() }
        }
    }

    // jumping as soon as the page has loaded does not always scroll: a moment later it does
    Timer {
        id: fragmentTimer
        interval: 300
        onTriggered: {
            var id = page.fragment;
            page.fragment = "";
            page.jumpTo(id);
        }
    }
}
