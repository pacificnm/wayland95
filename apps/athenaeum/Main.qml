import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import Win95Ui

// Athenaeum: an offline Wikipedia library with a local AI research
// assistant (KAthenaeum, in the Wayland 95 look). The places are listed on
// the left, like Outlook's bar; the page on the right is a stack, so Back
// returns through what was opened. Every page is a Page95 with a title and
// the commands of its Page menu.
Window {
    id: window

    title: page && page.title ? page.title + " - Athenaeum" : "Athenaeum"
    width: 980
    height: 680
    minimumWidth: 640
    minimumHeight: 440
    visible: true
    color: Theme.face

    readonly property var page: stack.currentItem
    property string section: "libraries"
    readonly property bool modalOpen: msgBox.visible || aboutDialog.visible || inputDialog.visible || projectPicker.visible
                                      || fileDialog.visible || citeDialog.visible || defineDialog.visible

    // ---- navigation (the same calls the KDE version's pages make)
    readonly property var places: [
        { group: "Read" },
        { section: "libraries", text: "Libraries", icon: "book" },
        { section: "search", text: "Search", icon: "find" },
        { group: "Learn" },
        { section: "classroom", text: "Classroom", icon: "school" },
        { group: "Research" },
        { section: "ask", text: "Ask", icon: "chat" },
        { section: "chats", text: "Chats", icon: "clock" },
        { section: "projects", text: "Projects", icon: "project" },
        { section: "notes", text: "Notes", icon: "highlight" },
        { group: "Manage" },
        { section: "manager", text: "Library Manager", icon: "download" },
        { section: "settings", text: "Settings", icon: "settings" },
    ]
    function componentOf(name) {
        return { libraries: librariesPage, search: searchPage, ask: askPage, chats: chatsPage, projects: projectsPage,
                 notes: notesPage, classroom: classroomPage, manager: libraryManagerPage, settings: settingsPage }[name];
    }
    function show(name, properties) {
        section = name;
        stack.clear(QQC2.StackView.Immediate);
        stack.push(componentOf(name), properties || {}, QQC2.StackView.Immediate);
    }
    function push(component, properties, sectionName) {
        if (sectionName)
            section = sectionName;
        stack.push(component, properties || {}, QQC2.StackView.Immediate);
    }
    function back() {
        if (stack.depth > 1) {
            stack.pop(QQC2.StackView.Immediate);
            if (stack.currentItem)
                stack.currentItem.shown();
        }
    }
    function showLibraryManager(tab) { show("manager", tab !== undefined ? { startTab: tab } : {}); }
    function findLibrary(title) {
        LibraryManager.catalog.category = "";
        LibraryManager.catalog.flavour = "";
        LibraryManager.catalog.query = title;
        showLibraryManager(1);
    }
    function findDictionaries() { findLibrary("Wiktionary"); }
    function openLibrary(libraryId) { push(libraryPage, { libraryId: libraryId }); }
    function openTitles(libraryId, label, key, isPrefix) {
        push(titlesPage, { libraryId: libraryId, label: label, key: key, isPrefix: isPrefix });
    }
    function openSearch(query, libraryId) {
        if (section === "search" && page && page.objectName === "searchPage" && stack.depth === 1)
            page.run(query, libraryId === undefined ? -1 : libraryId);
        else
            push(searchPage, { initialQuery: query || "", libraryId: libraryId === undefined ? -1 : libraryId });
    }
    function openArticle(libraryId, path, fragment) {
        push(articlePage, { url: LibraryBrowser.articleUrl(libraryId, path), fragment: fragment || "" });
    }
    function openArticleUrl(url) { push(articlePage, { url: url }); }
    function openReading(libraryId, path, lessonId) {
        push(articlePage, { url: LibraryBrowser.articleUrl(libraryId, path), lessonId: lessonId });
    }
    function openTimeline(properties) { push(timelinePage, properties); }
    function openProject(projectId) { push(projectPage, { projectId: projectId }, "projects"); }
    function openDocument(documentId) { documentWindows.open(documentId); }
    function openChat(chatId, articleLibraryId, articlePath, projectId, onTop) {
        if (projectId > 0) {
            show("projects");
            push(projectPage, { projectId: projectId, startChatId: chatId });
        } else if (articleLibraryId > 0) {
            var props = { url: LibraryBrowser.articleUrl(articleLibraryId, articlePath), openChatId: chatId };
            if (!onTop)
                show("chats");
            push(articlePage, props);
        } else if (onTop) {
            push(askPage, { chatId: chatId });
        } else {
            show("ask", { chatId: chatId });
        }
    }
    function openCourse(courseId) { push(coursePage, { courseId: courseId }, "classroom"); }
    function openLesson(lessonId, replace) {
        section = "classroom";
        if (replace)
            stack.replace(lessonPage, { lessonId: lessonId }, QQC2.StackView.Immediate);
        else
            push(lessonPage, { lessonId: lessonId });
    }
    function askTeacher(lessonId, question) { push(lessonPage, { lessonId: lessonId, startTab: 2, askText: question }, "classroom"); }
    function openUnitTest(unitId, unitTitle, courseTitle) {
        push(unitTestPage, { unitId: unitId, unitTitle: unitTitle, courseTitle: courseTitle || "" }, "classroom");
    }

    // a second start was given something to open (a zim: address or a .zim file), or just wants the window
    function openExternal(what) {
        window.raise();
        window.requestActivate();
        what = String(what || "");
        if (what.indexOf("zim:") === 0 && LibraryBrowser.isArticleUrl(what))
            openArticleUrl(what);
        else if (/\.zim$/i.test(what))
            LibraryManager.addFile(what.indexOf("file:") === 0 ? what : "file://" + what);
    }

    // ---- messages: the status bar, or a box for errors
    property string statusText: ""
    property string hoverText: ""   // a link or source pointed at (shown while it is)
    property string statusActionLabel: ""
    property var statusAction: null
    // notify(text, isError, actionLabel, action): the status bar says it (with a button, "Undo"), an error in a box too
    function notify(text, isError, actionLabel, action) {
        if (isError)
            msgBox.show("Athenaeum", Util.plain(text), "error", ["OK"], null);
        statusText = Util.plain(text);
        statusActionLabel = actionLabel || "";
        statusAction = action || null;
        statusTimer.restart();
    }
    function msg(title, text) { msgBox.show(title, Util.plain(text), "info", ["OK"], null); }
    function pickProject(title, preview, withNote, already, callback) { projectPicker.pick(title, preview, withNote, already, callback); }
    function notifyAction(text, actionLabel, action) {
        msgBox.show("Athenaeum", Util.plain(text), "info", [actionLabel, "Close"], function (i) { if (i === 0) action(); });
    }
    Timer { id: statusTimer; interval: 8000; onTriggered: { window.statusText = ""; window.statusActionLabel = ""; window.statusAction = null; } }

    // ---- shared dialogs pages ask for
    // ask(title, label, text, callback(text)) - a line of text
    function ask(title, label, text, callback) { inputDialog.ask(title, label, text, callback); }
    // confirm(title, message, callback()) - Yes / No
    function confirm(title, message, callback) {
        msgBox.show(title, message, "question", ["Yes", "No"], function (i) { if (i === 0) callback(); });
    }
    function chooseFile(mode, filters, suffix, name, callback) {
        fileDialog.mode = mode;
        fileDialog.filters = filters;
        fileDialog.filterIndex = 0;
        fileDialog.defaultSuffix = suffix;
        fileDialog.callback = callback;
        fileDialog.openAt(fileDialog.folder, name || "");
    }
    function cite(libraryId, path, title) { citeDialog.open(libraryId, path, title); }
    function define(word) { defineDialog.open(word); }

    // ---- menus
    function fileMenu() {
        return [
            { text: "New &Question", shortcut: "Ctrl+N", action: function () { show("ask"); } },
            { text: "New &Project...", action: function () { newProject(); } },
            { separator: true },
            { text: "&Add a ZIM File...", action: addZimFile },
            { separator: true },
            { text: "E&xit", action: function () { window.close(); } },
        ];
    }
    function newProject() {
        ask("New Project", "Name of the new research project:", "", function (t) {
            if (t.trim() === "")
                return;
            var id = projectList.create(t.trim(), "");
            if (id > 0) {
                show("projects");
                openProject(id);
            }
        });
    }
    function addZimFile() {
        chooseFile("open", [{ label: "ZIM files (*.zim)", patterns: ["*.zim"] }, { label: "All Files (*.*)", patterns: ["*"] }], "zim", "",
                   function (path) { LibraryManager.addFile("file://" + path); });
    }
    function goMenu() {
        var items = [
            { text: "&Back", shortcut: "Alt+Left", enabled: stack.depth > 1, action: back },
            { separator: true },
        ];
        for (var i = 0; i < places.length; ++i) {
            var p = places[i];
            if (p.section)
                items.push({ text: p.text, checked: section === p.section, action: (function (s) { return function () { show(s); }; })(p.section) });
        }
        return items;
    }
    function pageMenu() {
        var list = page && page.actions ? page.actions : [];
        return list.length ? list : [{ text: "(nothing for this page)", enabled: false }];
    }
    function toolsMenu() {
        return [
            { text: "Use the &Internet", checked: Connectivity.allowed, action: function () { Connectivity.allowed = !Connectivity.allowed; } },
            { text: "Check for Library &Updates", enabled: Connectivity.online && !LibraryManager.checkingUpdates,
              action: function () { LibraryManager.checkForUpdates(false); } },
            { text: "Check &Ollama Again", action: function () { Models.refresh(); } },
            { separator: true },
            { text: "&Settings...", action: function () { show("settings"); } },
        ];
    }
    function helpMenu() {
        return [{ text: "&About Athenaeum", action: function () { aboutDialog.show(); } }];
    }

    Shortcut { sequence: "Alt+Left"; enabled: !window.modalOpen; onActivated: window.back() }
    Shortcut { sequence: "Backspace"; enabled: !window.modalOpen && !(activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)); onActivated: window.back() }
    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen; onActivated: window.show("ask") }
    Shortcut { sequences: [StandardKey.Find, "F3"]; enabled: !window.modalOpen; onActivated: findField.input.forceActiveFocus() }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Go", items: window.goMenu },
            { title: "Page", items: window.pageMenu },
            { title: "Tools", items: window.toolsMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ---- the toolbar: Back, the address of the page, and a search box
    Item {
        id: toolBar
        x: 2
        y: menuBar.y + menuBar.height + 2
        width: parent.width - 4
        height: 30
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
        Row {
            x: 2
            y: 4
            spacing: 2
            IconButton {
                icon: "back"
                text: "Back"
                flat: true
                enabled: stack.depth > 1
                onClicked: window.back()
            }
            IconButton {
                icon: "home"
                flat: true
                enabled: window.section !== "libraries" || stack.depth > 1
                onClicked: window.show("libraries")
            }
        }
        // where we are: the stack's titles
        Label95 {
            x: 90
            y: 4
            width: parent.width - x - findBox.width - 12
            height: 24
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
            text: window.trail
        }
        Row {
            id: findBox
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 5
            spacing: 4
            Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Find:" }
            TextField95 {
                id: findField
                width: 200
                height: 22
                onAccepted: {
                    if (text.trim() !== "")
                        window.openSearch(text.trim(), -1);
                }
            }
            Button95 {
                width: 50
                height: 22
                text: "Go"
                onClicked: findField.accepted()
            }
        }
    }
    property string trail: {
        stack.depth;
        var t = [];
        for (var i = 0; i < stack.depth; ++i) {
            var it = stack.get(i, QQC2.StackView.DontLoad);
            if (it && it.title)
                t.push(it.title);
        }
        return t.join(" › ");
    }

    // ---- the places
    Bevel {
        id: placesWell
        x: 4
        y: toolBar.y + toolBar.height + 2
        width: 156
        height: statusBar.y - y - 4
        style: "sunken"
        face: Theme.window
        Column {
            x: 2
            y: 4
            width: parent.width - 4
            Repeater {
                model: window.places
                Item {
                    id: placeRow
                    required property var modelData
                    readonly property bool isGroup: modelData.group !== undefined
                    readonly property bool current: !isGroup && window.section === modelData.section
                    readonly property int badge: modelData.section === "manager"
                                                 ? (LibraryManager.activeDownloads > 0 ? LibraryManager.activeDownloads : LibraryManager.updateCount) : 0
                    width: parent.width
                    height: isGroup ? 22 : 24
                    Text {
                        visible: placeRow.isGroup
                        x: 6
                        y: 6
                        text: placeRow.modelData.group || ""
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Rectangle {
                        visible: placeRow.current
                        x: 2
                        width: parent.width - 4
                        height: parent.height
                        color: Theme.selection
                    }
                    AthenaeumIcon {
                        visible: !placeRow.isGroup
                        x: 10
                        y: 4
                        name: placeRow.modelData.icon || ""
                    }
                    Text {
                        visible: !placeRow.isGroup
                        x: 32
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        text: placeRow.modelData.text || ""
                        color: placeRow.current ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Rectangle {
                        visible: placeRow.badge > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(16, badgeText.implicitWidth + 6)
                        height: 14
                        color: "#800000"
                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: placeRow.badge
                            color: "#ffffff"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize - 1
                            font.bold: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !placeRow.isGroup
                        onClicked: window.show(placeRow.modelData.section)
                    }
                }
            }
        }
        // Ollama and the internet, at the bottom
        Column {
            x: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            width: parent.width - 16
            spacing: 2
            Label95 {
                width: parent.width
                small: true
                wrapMode: Text.Wrap
                color: (Models.claude || Models.reachable) && Models.answerModelInstalled ? "#006000" : "#800000"
                text: Models.claude ? (Models.claudeKeySet ? "Claude: " + Models.answerModel : "Claude: no API key set (Settings).")
                    : Models.checking ? "Looking for Ollama..."
                    : !Models.reachable ? "Ollama is not running: Ask and the Classroom need it."
                    : !Models.answerModelInstalled ? "Ollama has no " + Models.answerModel + " model."
                    : "Ollama: " + Models.answerModel
            }
            Label95 {
                width: parent.width
                small: true
                dim: true
                text: Connectivity.online ? "Internet: on" : Connectivity.allowed ? "Internet: no connection" : "Internet: off"
            }
        }
    }

    // ---- the page
    QQC2.StackView {
        id: stack
        x: placesWell.x + placesWell.width + 4
        y: placesWell.y
        width: parent.width - x - 4
        height: placesWell.height
        clip: true
        pushEnter: null
        pushExit: null
        popEnter: null
        popExit: null
        replaceEnter: null
        replaceExit: null
        initialItem: librariesPage
    }

    // ---- the status bar
    Item {
        id: statusBar
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        Bevel { width: parent.width - progressCell.width - 2; height: parent.height; style: "thinSunken" }
        Text {
            x: 4
            width: parent.width - progressCell.width - 10
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: window.hoverText !== "" ? window.hoverText
                : window.statusText !== "" ? window.statusText
                : Drafting.running ? Drafting.label
                : Classroom.busy ? Classroom.label
                : LibraryManager.activeDownloads > 0 ? (LibraryManager.activeDownloads === 1 ? "1 library downloading" : LibraryManager.activeDownloads + " libraries downloading")
                : LibraryManager.installed.count === 1 ? "1 library" : LibraryManager.installed.count + " libraries"
        }
        Button95 {
            visible: window.statusActionLabel !== ""
            anchors.right: progressCell.left
            anchors.rightMargin: 4
            y: 1
            width: 60
            height: 18
            text: window.statusActionLabel
            onClicked: {
                var a = window.statusAction;
                window.statusActionLabel = "";
                window.statusAction = null;
                window.statusText = "";
                if (a)
                    a();
            }
        }
        Item {
            id: progressCell
            anchors.right: parent.right
            width: 160
            height: parent.height
            Bevel { anchors.fill: parent; style: "thinSunken" }
            ProgressBar95 {
                x: 2
                y: 2
                width: parent.width - 4
                height: parent.height - 4
                visible: Drafting.running || Classroom.busy
                value: Drafting.running ? (Drafting.total > 0 ? Math.round(100 * Drafting.step / Drafting.total) : -1)
                     : Classroom.total > 0 ? Math.round(100 * Classroom.step / Classroom.total) : -1
            }
        }
    }

    // ---- what the C++ side reports
    Connections {
        target: LibraryManager
        function onMessage(text, isError) { window.notify(text, isError); }
    }
    Connections {
        target: Notes
        function onMessage(text) { window.notify(text, false); }
    }
    Connections {
        target: Drafting
        function onDocumentReady(documentId, projectId, title) {
            if (window.page && window.page.projectId === projectId)
                window.openDocument(documentId);
            else
                window.notifyAction("“" + title + "” is ready.", "Open", function () { window.openDocument(documentId); });
        }
        function onFailed(message) { window.notify(message, true); }
    }
    Connections {
        target: Classroom
        function onCourseReady(courseId, title) {
            if (!(window.page && window.page.courseId === courseId))
                window.notifyAction("“" + title + "” is ready.", "Open", function () { window.openCourse(courseId); });
        }
        function onFailed(message) { window.notify(message, true); }
    }

    ProjectList { id: projectList }

    // ---- the pages
    Component { id: librariesPage; LibrariesPage {} }
    Component { id: libraryPage; LibraryPage {} }
    Component { id: titlesPage; TitlesPage {} }
    Component { id: searchPage; SearchPage {} }
    Component { id: articlePage; ArticlePage {} }
    Component { id: timelinePage; TimelinePage {} }
    Component { id: askPage; AskPage {} }
    Component { id: chatsPage; ChatsPage {} }
    Component { id: projectsPage; ProjectsPage {} }
    Component { id: projectPage; ProjectPage {} }
    Component { id: notesPage; NotesPage {} }
    Component { id: classroomPage; ClassroomPage {} }
    Component { id: coursePage; CoursePage {} }
    Component { id: lessonPage; LessonPage {} }
    Component { id: unitTestPage; UnitTestPage {} }
    Component { id: libraryManagerPage; LibraryManagerPage {} }
    Component { id: settingsPage; SettingsPage {} }

    DocumentWindows { id: documentWindows; mainWindow: window }

    // ---- dialogs
    Dialog95 {
        id: aboutDialog
        title: "About Athenaeum"
        dialogWidth: 400
        dialogHeight: 230
        AthenaeumIcon { x: 14; y: 14; size: 32; name: "athenaeum" }
        Text {
            x: 60
            y: 14
            width: parent.width - 74
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            text: "<b>Athenaeum</b><br>Wayland 95<br><br>A private, offline Wikipedia library with an AI research assistant: "
                  + "KAthenaeum by Jaimie Garner, in the Wayland 95 look.<br><br>"
                  + "Articles come from Wikipedia through Kiwix ZIM files (libzim) and are under their own licence "
                  + "(Wikipedia: CC BY-SA 4.0). Answers are written by models running on this computer through Ollama; nothing leaves it."
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: aboutDialog.close()
        }
    }

    InputDialog { id: inputDialog }
    ProjectPicker { id: projectPicker; popupMenu: popup }
    CitationDialog { id: citeDialog; popupMenu: popup }
    DefinitionDialog { id: defineDialog; onFindDictionaries: window.findDictionaries() }

    FileDialog95 {
        id: fileDialog
        property var callback: null
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => { if (callback) callback(path); }
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    // W95_ATHENAEUM_GRAB: a picture of the window, for the tests;
    // W95_ATHENAEUM_PAGE / _QUERY: the page to show first
    Component.onCompleted: {
        if (testPage !== "") {
            if (testPage === "search")
                openSearch(testQuery, -1);
            else if (testPage === "article") {
                var libs = LibraryManager.libraryList();
                if (libs.length) {
                    var a = testQuery ? { path: testQuery } : LibraryBrowser.randomArticle(libs[0].libraryId);
                    openArticle(libs[0].libraryId, a.path || "");
                }
            } else if (testPage === "library") {
                var l = LibraryManager.libraryList();
                if (l.length)
                    openLibrary(l[0].libraryId);
            } else if (testPage === "about")
                aboutDialog.show();
            else if (componentOf(testPage))
                show(testPage);
        }
        if (openingArgument !== "")
            openExternal(openingArgument);
    }
    TestDriver { name: testScript; grabFile: testGrab }
    Timer {
        running: testGrab !== "" && testScript === ""
        interval: 4000
        onTriggered: window.contentItem.grabToImage(function (r) { r.saveToFile(testGrab); console.log("grabbed " + testGrab); })
    }
}
