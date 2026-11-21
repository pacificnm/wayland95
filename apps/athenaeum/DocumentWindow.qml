import QtQuick
import QtQuick.Window
import Win95Ui

// A project document in a window of its own: a Markdown editor beside a
// live preview (KAthenaeum had Kate's editor here). What is typed is saved
// 900 ms after typing stops, and when the window closes. Cite inserts a
// link to one of the project's articles, a saved excerpt as a quotation, or
// the list of works cited.
Window {
    id: win
    property var documentId: 0
    property var mainWindow
    property bool showPreview: true
    property bool wordWrap: true

    title: (doc.title || "Untitled") + (projectName !== "" ? " - " + projectName : "") + " - Athenaeum"
    width: 1000
    height: 700
    minimumWidth: 480
    minimumHeight: 320
    color: Theme.face
    readonly property string projectName: { doc.title; return doc.projectTitle(); }

    ProjectDocument {
        id: doc
        documentId: win.documentId
        Component.onCompleted: {
            titleField.text = doc.title;
            editor.text = doc.content;
            editor.edit.cursorPosition = 0;
            win.refreshPreview();
        }
    }
    // deleted, or its project was: the window goes
    Connections {
        target: Notes   // (any change to the database; a project change reloads the preview)
        function onChanged() { previewTimer.restart(); }
    }
    Timer {
        interval: 2000
        running: true
        repeat: true
        onTriggered: if (!doc.stillExists()) win.close()
    }

    function refreshPreview() {
        var y = preview.flickable.contentY;
        preview.text = Util.markdown(doc.previewMarkdown());
        preview.flickable.contentY = y;
    }
    Timer { id: previewTimer; interval: 250; onTriggered: win.refreshPreview() }

    function insert(markdown) {
        var text = markdown;
        var e = editor.edit;
        if (text.indexOf(">") === 0) { // a quotation is a block of its own
            var before = e.text.slice(0, e.cursorPosition);
            var lineStart = before.lastIndexOf("\n") + 1;
            var lineHasText = before.slice(lineStart).trim() !== "";
            text = (lineHasText ? "\n\n" : "") + text + "\n\n";
        }
        e.insert(e.cursorPosition, text);
        e.forceActiveFocus();
    }
    function citeMenu() {
        return doc.citeChoices().map(function (c) {
            if (c.separator)
                return { separator: true };
            return { text: Util.plain(c.text).replace(/&/g, "&&"), enabled: c.enabled, action: function () { win.insert(c.insert); } };
        });
    }
    function exportDocument() {
        fileDialog.openAt(Win95Fs.homePath() + "/Documents", doc.exportName());
    }

    function fileMenu() {
        return [
            { text: "&Save", shortcut: "Ctrl+S", action: function () { doc.save(); } },
            { text: "&Export...", action: exportDocument },
            { separator: true },
            { text: "&Close", action: function () { win.close(); } },
        ];
    }
    function editMenu() {
        var e = editor.edit;
        return [
            { text: "&Undo", shortcut: "Ctrl+Z", enabled: e.canUndo, action: function () { e.undo(); } },
            { text: "&Redo", shortcut: "Ctrl+Y", enabled: e.canRedo, action: function () { e.redo(); } },
            { separator: true },
            { text: "Cu&t", shortcut: "Ctrl+X", enabled: e.selectedText !== "", action: function () { e.cut(); } },
            { text: "&Copy", shortcut: "Ctrl+C", enabled: e.selectedText !== "", action: function () { e.copy(); } },
            { text: "&Paste", shortcut: "Ctrl+V", enabled: e.canPaste, action: function () { e.paste(); } },
            { separator: true },
            { text: "Select &All", shortcut: "Ctrl+A", action: function () { e.selectAll(); } },
        ];
    }
    function viewMenu() {
        return [
            { text: "&Preview", shortcut: "Ctrl+Shift+P", checked: showPreview, action: function () { showPreview = !showPreview; } },
            { text: "&Word Wrap", checked: wordWrap, action: function () { wordWrap = !wordWrap; } },
        ];
    }
    Shortcut { sequence: "Ctrl+S"; onActivated: doc.save() }
    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: win.showPreview = !win.showPreview }

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: win.fileMenu },
            { title: "Edit", items: win.editMenu },
            { title: "Cite", items: win.citeMenu },
            { title: "View", items: win.viewMenu },
        ]
    }
    Row {
        id: titleRow
        x: 6
        y: menuBar.y + menuBar.height + 4
        spacing: 6
        height: 24
        Label95 { height: 22; verticalAlignment: Text.AlignVCenter; text: "Title:" }
        TextField95 {
            id: titleField
            width: win.width - 200
            height: 22
            onTextChanged: if (text !== doc.title) { doc.title = text; previewTimer.restart(); }
        }
        IconButton {
            icon: "link"
            text: "Cite"
            onClicked: { var p = mapToItem(popup, 0, height); popup.popup(p.x, p.y, win.citeMenu()); }
        }
    }

    TextArea95 {
        id: editor
        x: 4
        y: titleRow.y + titleRow.height + 4
        width: win.showPreview ? Math.round((win.width - 12) / 2) : win.width - 8
        height: statusBar.y - y - 4
        monospace: true
        edit.wrapMode: win.wordWrap ? TextEdit.Wrap : TextEdit.NoWrap
        focus: true
        edit.onTextChanged: if (edit.text !== doc.content) { doc.content = edit.text; previewTimer.restart(); }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            cursorShape: Qt.IBeamCursor
            onClicked: (mouse) => {
                var p = mapToItem(popup, mouse.x, mouse.y);
                popup.popup(p.x, p.y, win.editMenu().concat([{ separator: true }, { text: "C&ite", submenu: win.citeMenu() }]));
            }
        }
    }
    TextView95 {
        id: preview
        visible: win.showPreview
        x: editor.x + editor.width + 4
        y: editor.y
        width: win.width - x - 4
        height: editor.height
        textFormat: Text.MarkdownText
        padding: 10
        // a link to an article opens it in the main window; one to the web, in the Web Browser
        onLinkActivated: (link) => {
            if (link.indexOf("zim:") === 0 && win.mainWindow) {
                win.mainWindow.openArticleUrl(link);
                win.mainWindow.raise();
                win.mainWindow.requestActivate();
            } else if (/^(https?|mailto):/i.test(link)) {
                Qt.openUrlExternally(link);
            }
        }
    }

    Item {
        id: statusBar
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        Bevel { anchors.fill: parent; style: "thinSunken" }
        Text {
            x: 4
            width: parent.width - 8
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: doc.error !== "" ? "#c00000" : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: win.statusNote !== "" ? win.statusNote : doc.error !== "" ? doc.error : doc.saved ? "Saved" : "Saving..."
        }
    }
    property string statusNote: ""
    Timer { id: noteTimer; interval: 8000; onTriggered: win.statusNote = "" }

    onClosing: doc.save() // what was typed is kept (the status bar says so if it could not be)

    FileDialog95 {
        id: fileDialog
        mode: "save"
        filters: [{ label: "Markdown (*.md)", patterns: ["*.md"] }]
        defaultSuffix: "md"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            doc.save();
            var error = doc.exportTo("file://" + path);
            win.statusNote = error !== "" ? error : "Exported to " + path;
            noteTimer.restart();
        }
    }
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
