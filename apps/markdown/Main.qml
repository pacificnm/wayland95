import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Markdown

// The Markdown Editor: a WordPad-style window for Markdown files. The page
// shows the document formatted (and is where you edit it, with the format
// bar's buttons); View > Markdown shows the same text as Markdown.
//
// --reader FILE makes it the Markdown Reader: the page only, to read (a
// manual): no editing, no format bar, a reader's menus, and Back, Forward
// and Home for the links followed from page to page.
Window {
    id: window

    width: 760
    height: 560
    minimumWidth: 360
    minimumHeight: 240
    visible: true
    color: Theme.face
    title: (documentPath ? Win95Fs.fileName(documentPath) : "Untitled") + (readerOnly ? " - Markdown Reader" : " - Markdown Editor")

    // --reader: only to read (it never edits, and can't be switched to)
    readonly property bool readerOnly: initialReader
    // (the reader's history: { path, y } of the pages before and after this one)
    property var backStack: []
    property var forwardStack: []
    property string homePath: ""

    property string documentPath: ""
    property bool sourceMode: false
    property bool sourceChanged: false
    property bool readingView: false
    property bool showToolbar: true
    property bool showFormatBar: true
    property bool showStatusBar: true
    property bool quitting: false
    property var afterSave: null
    property string tip: ""
    property int words: 0

    readonly property bool modified: md.modified || sourceChanged
    readonly property bool modalOpen: fileDialog.visible || exportDialog.visible || pictureDialog.visible
                                      || msgBox.visible || popup.visible || linkDialog.visible
                                      || tableDialog.visible
    readonly property var styleNames: ["Normal", "Heading 1", "Heading 2", "Heading 3", "Heading 4",
                                       "Heading 5", "Heading 6", "Code Block"]
    readonly property var markdownFilters: [
        { label: "Markdown Documents (*.md)", patterns: ["*.md", "*.markdown", "*.mdown", "*.mkd"] },
        { label: "Text Documents (*.txt)", patterns: ["*.txt"] },
        { label: "All Files (*.*)", patterns: ["*"] },
    ]

    function log(msg) {
        if (markdownDebug)
            console.log("markdown: " + msg);
    }

    Component.onCompleted: {
        // (--read: a manual, to read and follow its links; before it's
        // loaded, so it opens at its start)
        if (initialReading || readerOnly)
            readingView = true;
        if (initialFile)
            openFile(initialFile);
        // Home: the folder's index.md (a manual's Contents), else the first page
        if (readerOnly && documentPath) {
            var index = Win95Fs.parentDir(documentPath) + "/index.md";
            homePath = Win95Fs.isFile(index) ? index : documentPath;
        }
        focusEditor();
        log("ready " + (documentPath || "untitled"));
    }

    onClosing: (close) => {
        if (modified && !quitting) {
            close.accepted = false;
            confirmDiscard(function () {
                window.quitting = true;
                window.close();
            });
        }
    }

    // ------------------------------------------------------------ documents

    function focusEditor() {
        if (sourceMode)
            sourceEdit.forceActiveFocus();
        else
            editor.forceActiveFocus();
    }

    function currentDir() {
        return documentPath ? Win95Fs.parentDir(documentPath) : Win95Fs.homePath();
    }

    function afterLoad() {
        if (sourceMode)
            fillSource();
        // at the start, cursor and all (or focusing the editor scrolls to where the cursor was left)
        editor.cursorPosition = 0;
        flick.contentY = 0;
        countWords();
    }

    function openFile(path) {
        var r = md.load(path);
        if (!r.ok) {
            msgBox.show("Markdown Editor", r.error, "error", ["OK"], focusEditor);
            return;
        }
        documentPath = r.path;
        afterLoad();
        log("opened " + r.path);
        if (markdownDebug)
            Qt.callLater(logCheckBoxes);
    }

    // (for the tests) where each check box and link is, in the window
    function logCheckBoxes() {
        var items = md.checkItems();
        for (var i = 0; i < items.length; ++i) {
            var r = editor.positionToRectangle(items[i]);
            var p = editor.mapToItem(null, r.x - 10, r.y + r.height / 2);
            log("checkbox " + i + " " + Math.round(p.x) + " " + Math.round(p.y));
        }
        var links = md.linkStarts();
        for (var j = 0; j < links.length; ++j) {
            var lr = editor.positionToRectangle(links[j] + 1);
            var lp = editor.mapToItem(null, lr.x, lr.y + lr.height / 2);
            log("linkat " + j + " " + Math.round(lp.x) + " " + Math.round(lp.y));
        }
    }

    function confirmDiscard(then) {
        if (!modified) {
            then();
            return;
        }
        msgBox.show("Markdown Editor",
                    "Save changes to " + (documentPath ? Win95Fs.fileName(documentPath) : "Untitled") + "?",
                    "warning", ["Yes", "No", "Cancel"], function (i) {
                        if (i === 0)
                            save(then);
                        else if (i === 1)
                            then();
                        else
                            focusEditor();
                    });
    }

    function newFile() {
        confirmDiscard(function () {
            documentPath = "";
            md.baseDir = "";
            md.setMarkdown("");
            afterLoad();
            focusEditor();
        });
    }

    function showOpen() {
        confirmDiscard(function () {
            fileDialog.mode = "open";
            fileDialog.openAt(currentDir(), "");
        });
    }

    function save(then) {
        if (!documentPath) {
            saveAs(then);
            return;
        }
        if (!sourceMode && md.hasHtml && md.modified) {
            log("html warning");
            msgBox.show("Markdown Editor",
                        Win95Fs.fileName(documentPath) + " has HTML in it, which the page can't keep: saving "
                        + "from the page leaves it out.\n\nSave anyway? (To keep it, choose No and edit "
                        + "in View > Markdown.)",
                        "warning", ["Yes", "No"], function (i) {
                            if (i === 0)
                                window.writeFile(then);
                            else
                                window.focusEditor();
                        });
            return;
        }
        writeFile(then);
    }

    function writeFile(then) {
        var error = sourceMode ? md.saveText(documentPath, sourceEdit.text) : md.save(documentPath);
        if (error) {
            msgBox.show("Markdown Editor", error, "error", ["OK"], focusEditor);
            return;
        }
        sourceChanged = false;
        log("saved " + documentPath);
        if (then)
            then();
        else
            focusEditor();
    }

    function saveAs(then) {
        afterSave = then || null;
        fileDialog.mode = "save";
        fileDialog.openAt(currentDir(), documentPath ? Win95Fs.fileName(documentPath) : "Untitled.md");
    }

    function exportHtml() {
        var name = documentPath ? Win95Fs.fileName(documentPath).replace(/\.[^.]*$/, "") : "Untitled";
        exportDialog.mode = "save";
        exportDialog.openAt(currentDir(), name + ".html");
    }

    // --------------------------------------------------------- the two views

    function fillSource() {
        sourceEdit.text = md.viewText();
        sourceChanged = false;
    }

    function setSourceMode(on) {
        if (on === sourceMode)
            return;
        if (on) {
            readingView = false;
            fillSource();
            sourceMode = true;
            sourceEdit.cursorPosition = 0;
        } else {
            if (sourceChanged) {
                md.setMarkdown(sourceEdit.text);
                md.modified = true;
                sourceChanged = false;
            }
            sourceMode = false;
        }
        countWords();
        focusEditor();
        log("view " + (on ? "markdown" : "page"));
    }

    function countWords() {
        words = sourceMode ? (sourceEdit.text.match(/[\w'’-]+/g) || []).length : md.wordCount();
    }

    // ------------------------------------------------------------- editing

    readonly property bool hasSelection: sourceMode ? sourceEdit.selectionStart !== sourceEdit.selectionEnd
                                                    : editor.selectionStart !== editor.selectionEnd
    readonly property var activeEdit: sourceMode ? sourceEdit : editor

    function deleteSelection() {
        var e = activeEdit;
        if (e.selectionStart !== e.selectionEnd)
            e.remove(e.selectionStart, e.selectionEnd);
    }

    function insertDateTime() {
        var now = new Date();
        var text = now.toLocaleDateString(Qt.locale(), Locale.ShortFormat) + " "
                + now.toLocaleTimeString(Qt.locale(), Locale.ShortFormat);
        if (sourceMode) {
            deleteSelection();
            sourceEdit.insert(sourceEdit.cursorPosition, text);
        } else {
            md.insertText(text);
        }
    }

    function format(what) {
        if (sourceMode || readingView)
            return;
        switch (what) {
        case "bold": md.toggleBold(); break;
        case "italic": md.toggleItalic(); break;
        case "underline": md.toggleUnderline(); break;
        case "strike": md.toggleStrike(); break;
        case "code": md.toggleCode(); break;
        case "clear": md.clearFormatting(); break;
        case "bullets": md.toggleList(MarkdownDocument.Bullets); break;
        case "numbers": md.toggleList(MarkdownDocument.Numbers); break;
        case "checks": md.toggleList(MarkdownDocument.Checks); break;
        case "indent": md.indent(); break;
        case "outdent": md.outdent(); break;
        case "quote": md.toggleQuote(); break;
        case "rule": md.insertRule(); break;
        }
        log("format " + what);
        editor.forceActiveFocus();
    }

    function setStyle(style) {
        if (sourceMode || readingView)
            return;
        md.setBlockStyle(style);
        log("style " + style);
        editor.forceActiveFocus();
    }

    property string hoverLink: ""

    // the link under a point of the page ("" if none)
    function linkUnder(x, y) {
        var pos = editor.positionAt(x, y);
        var r = editor.positionToRectangle(pos);
        if (Math.abs(r.y + r.height / 2 - y) > r.height)
            return "";
        return md.linkAt(pos);
    }

    function openLink(url) {
        if (!url)
            return;
        log("link " + url);
        if (url.charAt(0) === "#" || markdownNoOpen)
            return;
        var isWeb = /^[a-z][a-z0-9+.-]*:/i.test(url);
        if (!isWeb) {
            var path = url.split("#")[0];
            path = decodeURIComponent(path);
            if (path.charAt(0) !== "/")
                path = (md.baseDir || Win95Fs.homePath()) + "/" + path;
            if (/\.(md|markdown|mdown|mkd)$/i.test(path) && Win95Fs.isFile(path)) {
                if (readerOnly)
                    goTo(path);
                else
                    confirmDiscard(function () { window.openFile(path); });
                return;
            }
            Qt.openUrlExternally("file://" + path);
            return;
        }
        Qt.openUrlExternally(url);
    }

    // --- the reader's history ---

    // another page, after this one (Forward is forgotten)
    function goTo(path) {
        if (path === documentPath)
            return;
        backStack = backStack.concat([{ path: documentPath, y: flick.contentY }]);
        forwardStack = [];
        openFile(path);
        log("go " + Win95Fs.fileName(path) + " back " + backStack.length);
    }
    function goBack() {
        if (backStack.length === 0)
            return;
        var to = backStack[backStack.length - 1];
        backStack = backStack.slice(0, -1);
        forwardStack = forwardStack.concat([{ path: documentPath, y: flick.contentY }]);
        showAgain(to);
    }
    function goForward() {
        if (forwardStack.length === 0)
            return;
        var to = forwardStack[forwardStack.length - 1];
        forwardStack = forwardStack.slice(0, -1);
        backStack = backStack.concat([{ path: documentPath, y: flick.contentY }]);
        showAgain(to);
    }
    function goHome() {
        if (homePath)
            goTo(homePath);
    }
    // a page from the history, scrolled to where it was
    function showAgain(to) {
        openFile(to.path);
        log("go " + Win95Fs.fileName(to.path) + " back " + backStack.length + " forward " + forwardStack.length);
        Qt.callLater(function () {
            flick.contentY = Math.max(0, Math.min(to.y, flick.contentHeight - flick.height));
        });
    }

    function showLinkDialog() {
        if (sourceMode || readingView)
            return;
        var info = md.linkInfo();
        linkDialog.start = info.start;
        linkDialog.end = info.end;
        linkDialog.hadLink = !!info.url;
        linkText.text = info.text;
        linkUrl.text = info.url || "https://";
        linkDialog.show();
        if (info.text)
            linkUrl.forceActiveFocus();
        else
            linkText.forceActiveFocus();
        linkUrl.input.selectAll();
    }

    function acceptLink() {
        var url = linkUrl.text.trim();
        if (url === "https://" || url === "http://")
            url = "";
        var text = linkText.text;
        if (!text && !url) {
            linkDialog.close();
            return;
        }
        if (!text)
            text = url;
        linkDialog.close();
        md.setLink(linkDialog.start, linkDialog.end, text, url);
        log("link set " + url);
        editor.forceActiveFocus();
    }

    function showTableDialog() {
        if (sourceMode || readingView)
            return;
        tableDialog.show();
        rowsSpin.forceActiveFocus();
    }

    function showPictureDialog() {
        if (sourceMode || readingView)
            return;
        pictureDialog.mode = "open";
        pictureDialog.openAt(md.baseDir || Win95Fs.homePath(), "");
    }

    // ---------------------------------------------------------------- find

    function findNext() {
        var needle = findDialog.searchText;
        if (needle.length === 0) {
            findDialog.open(false);
            return false;
        }
        var e = activeEdit;
        var down = findDialog.searchDown;
        var range;
        if (sourceMode) {
            var hay = findDialog.matchCase ? e.text : e.text.toLowerCase();
            var n = findDialog.matchCase ? needle : needle.toLowerCase();
            var idx = down ? hay.indexOf(n, e.selectionEnd)
                           : (e.selectionStart > 0 ? hay.lastIndexOf(n, e.selectionStart - 1) : -1);
            range = idx < 0 ? [] : [idx, idx + needle.length];
        } else {
            range = md.find(needle, down ? e.selectionEnd : e.selectionStart, findDialog.matchCase, !down);
        }
        if (range.length === 0) {
            msgBox.show("Markdown Editor", "Finished searching the document.", "info", ["OK"], function () {
                if (findDialog.visible)
                    findDialog.refocus();
                else
                    focusEditor();
            });
            return false;
        }
        e.select(range[0], range[1]);
        log("found " + range[0]);
        return true;
    }

    function selectionMatches() {
        var e = activeEdit;
        var sel = e.selectedText;
        var needle = findDialog.searchText;
        return needle.length > 0 && (findDialog.matchCase ? sel === needle : sel.toLowerCase() === needle.toLowerCase());
    }

    function replaceOne() {
        if (selectionMatches()) {
            var e = activeEdit;
            if (sourceMode) {
                var s = e.selectionStart;
                e.remove(s, e.selectionEnd);
                e.insert(s, findDialog.replaceText);
                e.select(s, s + findDialog.replaceText.length);
            } else {
                md.replaceRange(e.selectionStart, e.selectionEnd, findDialog.replaceText);
            }
        }
        findNext();
    }

    function replaceAll() {
        var needle = findDialog.searchText;
        if (!needle)
            return;
        var n = 0;
        if (sourceMode) {
            var e = sourceEdit;
            var pos = 0;
            while (true) {
                var hay = findDialog.matchCase ? e.text : e.text.toLowerCase();
                var idx = hay.indexOf(findDialog.matchCase ? needle : needle.toLowerCase(), pos);
                if (idx < 0)
                    break;
                e.remove(idx, idx + needle.length);
                e.insert(idx, findDialog.replaceText);
                pos = idx + findDialog.replaceText.length;
                ++n;
            }
        } else {
            n = md.replaceAll(needle, findDialog.replaceText, findDialog.matchCase);
        }
        log("replaced " + n);
        msgBox.show("Markdown Editor", n === 1 ? "Replaced 1 occurrence." : "Replaced " + n + " occurrences.",
                    "info", ["OK"], function () { findDialog.refocus(); });
    }

    // --------------------------------------------------------------- menus

    readonly property bool canFormat: !sourceMode && !readingView

    function fileMenu() {
        return [
            { text: "&New", shortcut: "Ctrl+N", action: window.newFile },
            { text: "&Open...", shortcut: "Ctrl+O", action: window.showOpen },
            { text: "&Save", shortcut: "Ctrl+S", action: function () { window.save(null); } },
            { text: "Save &As...", action: function () { window.saveAs(null); } },
            { text: "&Export as Web Page...", action: window.exportHtml },
            { separator: true },
            { text: "&Print...", enabled: false },
            { separator: true },
            { text: "E&xit", action: window.close },
        ];
    }

    function editMenu() {
        var e = window.activeEdit;
        var editable = !window.readingView;
        return [
            { text: "&Undo", shortcut: "Ctrl+Z", enabled: editable && e.canUndo, action: e.undo },
            { text: "&Redo", shortcut: "Ctrl+Y", enabled: editable && e.canRedo, action: e.redo },
            { separator: true },
            { text: "Cu&t", shortcut: "Ctrl+X", enabled: editable && window.hasSelection, action: e.cut },
            { text: "&Copy", shortcut: "Ctrl+C", enabled: window.hasSelection, action: e.copy },
            { text: "&Paste", shortcut: "Ctrl+V", enabled: editable && e.canPaste, action: e.paste },
            { text: "Cle&ar", shortcut: "Del", enabled: editable && window.hasSelection, action: window.deleteSelection },
            { text: "Select A&ll", shortcut: "Ctrl+A", action: e.selectAll },
            { separator: true },
            { text: "&Find...", shortcut: "Ctrl+F", action: function () { findDialog.open(false); } },
            { text: "Find &Next", shortcut: "F3", action: window.findNext },
            { text: "R&eplace...", shortcut: "Ctrl+H", enabled: editable, action: function () { findDialog.open(true); } },
            { separator: true },
            { text: "Lin&k...", shortcut: "Ctrl+K", enabled: window.canFormat, action: window.showLinkDialog },
            { text: "Remove Lin&k", enabled: window.canFormat && !!md.link, action: function () { md.removeLink(); } },
            { text: "&Open Link", enabled: !window.sourceMode && !!md.link, action: function () { window.openLink(md.link); } },
        ];
    }

    function viewMenu() {
        return [
            { text: "&Toolbar", checked: window.showToolbar, action: function () { window.showToolbar = !window.showToolbar; } },
            { text: "&Format Bar", checked: window.showFormatBar, action: function () { window.showFormatBar = !window.showFormatBar; } },
            { text: "&Status Bar", checked: window.showStatusBar, action: function () { window.showStatusBar = !window.showStatusBar; } },
            { separator: true },
            { text: "&Page", checked: !window.sourceMode && !window.readingView, action: function () { window.readingView = false; window.setSourceMode(false); } },
            { text: "&Reading View", checked: window.readingView, action: function () { window.setSourceMode(false); window.readingView = true; window.log("view reading"); if (markdownDebug) Qt.callLater(window.logCheckBoxes); } },
            { text: "&Markdown", shortcut: "Ctrl+M", checked: window.sourceMode, action: function () { window.setSourceMode(true); } },
        ];
    }

    function insertMenu() {
        var f = window.canFormat;
        return [
            { text: "&Picture...", enabled: f, action: window.showPictureDialog },
            { text: "&Link...", enabled: f, action: window.showLinkDialog },
            { text: "&Table...", enabled: f, action: window.showTableDialog },
            { text: "&Horizontal Line", enabled: f, action: function () { window.format("rule"); } },
            { text: "&Date and Time", enabled: !window.readingView, action: window.insertDateTime },
        ];
    }

    function formatMenu() {
        var f = window.canFormat;
        return [
            { text: "&Bold", shortcut: "Ctrl+B", enabled: f, checked: f && md.bold, action: function () { window.format("bold"); } },
            { text: "&Italic", shortcut: "Ctrl+I", enabled: f, checked: f && md.italic, action: function () { window.format("italic"); } },
            { text: "&Underline", shortcut: "Ctrl+U", enabled: f, checked: f && md.underline, action: function () { window.format("underline"); } },
            { text: "S&trikethrough", enabled: f, checked: f && md.strike, action: function () { window.format("strike"); } },
            { text: "&Code", shortcut: "Ctrl+`", enabled: f, checked: f && md.code, action: function () { window.format("code"); } },
            { text: "Clear &Formatting", enabled: f, action: function () { window.format("clear"); } },
            { separator: true },
            { text: "&Normal", shortcut: "Ctrl+0", enabled: f, checked: f && md.blockStyle === 0, action: function () { window.setStyle(0); } },
            { text: "Heading &1", shortcut: "Ctrl+1", enabled: f, checked: f && md.blockStyle === 1, action: function () { window.setStyle(1); } },
            { text: "Heading &2", shortcut: "Ctrl+2", enabled: f, checked: f && md.blockStyle === 2, action: function () { window.setStyle(2); } },
            { text: "Heading &3", shortcut: "Ctrl+3", enabled: f, checked: f && md.blockStyle === 3, action: function () { window.setStyle(3); } },
            { text: "Code Bloc&k", enabled: f, checked: f && md.blockStyle === 7, action: function () { window.setStyle(7); } },
            { text: "Block &Quote", enabled: f, checked: f && md.quote, action: function () { window.format("quote"); } },
            { separator: true },
            { text: "Bu&llets", enabled: f, checked: f && md.listStyle === 1, action: function () { window.format("bullets"); } },
            { text: "N&umbering", enabled: f, checked: f && md.listStyle === 2, action: function () { window.format("numbers"); } },
            { text: "Check Li&st", enabled: f, checked: f && md.listStyle === 3, action: function () { window.format("checks"); } },
            { text: "Increase In&dent", enabled: f, action: function () { window.format("indent"); } },
            { text: "Decrease Ind&ent", enabled: f, action: function () { window.format("outdent"); } },
        ];
    }

    function tableMenu() {
        var t = window.canFormat && md.inTable;
        return [
            { text: "&Insert Table...", enabled: window.canFormat, action: window.showTableDialog },
            { separator: true },
            { text: "Insert Row &Above", enabled: t, action: function () { md.tableInsertRow(false); } },
            { text: "Insert Row &Below", enabled: t, action: function () { md.tableInsertRow(true); } },
            { text: "Insert Column &Left", enabled: t, action: function () { md.tableInsertColumn(false); } },
            { text: "Insert Column &Right", enabled: t, action: function () { md.tableInsertColumn(true); } },
            { separator: true },
            { text: "Delete R&ow", enabled: t, action: function () { md.tableRemoveRow(); } },
            { text: "Delete Colu&mn", enabled: t, action: function () { md.tableRemoveColumn(); } },
            { text: "&Delete Table", enabled: t, action: function () { md.tableRemove(); } },
        ];
    }

    function helpMenu() {
        return [
            { text: "&Markdown Basics", shortcut: "F1", action: window.showHelp },
            { separator: true },
            { text: "&About Markdown Editor", action: function () {
                msgBox.show("About Markdown Editor",
                            "Markdown Editor\n\nReads and writes Markdown (.md) files for the Wayland 95 desktop.",
                            "info", ["OK"], window.focusEditor);
            } },
        ];
    }

    function showHelp() {
        msgBox.show("Markdown Basics",
                    "The page shows your document formatted. Use the format bar, or type as usual and pick a style.\n\n"
                    + "Ctrl+B bold, Ctrl+I italic, Ctrl+U underline, Ctrl+` code\n"
                    + "Ctrl+0 normal text, Ctrl+1/2/3 headings, Ctrl+K link\n"
                    + "Tab and Shift+Tab move list items in and out, and between table cells.\n"
                    + "Click a check box to tick it. Ctrl+click a link to open it.\n\n"
                    + "View > Markdown (Ctrl+M) shows and edits the Markdown itself.",
                    "info", ["OK"], window.focusEditor);
    }

    // the reader's menus
    function readerFileMenu() {
        return [
            { text: "&Print...", enabled: false },
            { separator: true },
            { text: "&Close", shortcut: "Ctrl+W", action: window.close },
        ];
    }
    function readerEditMenu() {
        return [
            { text: "&Copy", shortcut: "Ctrl+C", enabled: window.hasSelection, action: editor.copy },
            { text: "Select &All", shortcut: "Ctrl+A", action: editor.selectAll },
            { separator: true },
            { text: "&Find...", shortcut: "Ctrl+F", action: function () { findDialog.open(false); } },
            { text: "Find &Next", shortcut: "F3", action: window.findNext },
        ];
    }
    function readerViewMenu() {
        return [
            { text: "&Toolbar", checked: window.showToolbar, action: function () { window.showToolbar = !window.showToolbar; } },
            { text: "&Status Bar", checked: window.showStatusBar, action: function () { window.showStatusBar = !window.showStatusBar; } },
        ];
    }
    function goMenu() {
        return [
            { text: "&Back", shortcut: "Alt+Left", enabled: window.backStack.length > 0, action: window.goBack },
            { text: "&Forward", shortcut: "Alt+Right", enabled: window.forwardStack.length > 0, action: window.goForward },
            { separator: true },
            { text: "&Home", shortcut: "Alt+Home", enabled: !!window.homePath && window.homePath !== window.documentPath, action: window.goHome },
        ];
    }
    function readerHelpMenu() {
        return [
            { text: "&About Markdown Reader", action: function () {
                msgBox.show("About Markdown Reader",
                            "Markdown Reader\n\nShows Markdown (.md) documents to read: click a link to go to another, "
                            + "and Back (Alt+Left) to come back.",
                            "info", ["OK"], window.focusEditor);
            } },
        ];
    }

    function contextMenu() {
        if (window.readerOnly) {
            var r = [
                { text: "&Back", enabled: window.backStack.length > 0, action: window.goBack },
                { text: "&Forward", enabled: window.forwardStack.length > 0, action: window.goForward },
                { separator: true },
                { text: "&Copy", enabled: window.hasSelection, action: editor.copy },
            ];
            if (md.link)
                r.push({ text: "&Open Link", action: function () { window.openLink(md.link); } });
            r.push({ separator: true });
            r.push({ text: "Select &All", action: editor.selectAll });
            return r;
        }
        var e = window.activeEdit;
        var editable = !window.readingView;
        var items = [
            { text: "Cu&t", enabled: editable && window.hasSelection, action: e.cut },
            { text: "&Copy", enabled: window.hasSelection, action: e.copy },
            { text: "&Paste", enabled: editable && e.canPaste, action: e.paste },
        ];
        if (!window.sourceMode) {
            items.push({ separator: true });
            if (md.link) {
                items.push({ text: "&Open Link", action: function () { window.openLink(md.link); } });
                items.push({ text: "&Edit Link...", enabled: window.canFormat, action: window.showLinkDialog });
                items.push({ text: "&Remove Link", enabled: window.canFormat, action: function () { md.removeLink(); } });
            } else {
                items.push({ text: "Lin&k...", enabled: window.canFormat, action: window.showLinkDialog });
            }
            if (md.inTable && window.canFormat) {
                items.push({ separator: true });
                items = items.concat(window.tableMenu().slice(2));
            }
        }
        items.push({ separator: true });
        items.push({ text: "Select &All", action: e.selectAll });
        return items;
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen && !window.readerOnly; onActivated: window.newFile() }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.modalOpen && !window.readerOnly; onActivated: window.showOpen() }
    Shortcut { sequence: "Ctrl+S"; enabled: !window.modalOpen && !window.readerOnly; onActivated: window.save(null) }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.modalOpen; onActivated: findDialog.open(false) }
    Shortcut { sequence: "Ctrl+W"; enabled: !window.modalOpen && window.readerOnly; onActivated: window.close() }
    Shortcut { sequences: ["Alt+Left", "Backspace"]; enabled: !window.modalOpen && window.readerOnly; onActivated: window.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: !window.modalOpen && window.readerOnly; onActivated: window.goForward() }
    Shortcut { sequence: "Alt+Home"; enabled: !window.modalOpen && window.readerOnly; onActivated: window.goHome() }
    Shortcut { sequence: "Ctrl+H"; enabled: !window.modalOpen && !window.readingView; onActivated: findDialog.open(true) }
    Shortcut { sequence: "F3"; enabled: !window.modalOpen; onActivated: window.findNext() }
    Shortcut { sequence: "F1"; enabled: !window.modalOpen && !window.readerOnly; onActivated: window.showHelp() }
    Shortcut { sequence: "Ctrl+M"; enabled: !window.modalOpen && !window.readerOnly; onActivated: window.setSourceMode(!window.sourceMode) }
    Shortcut { sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]; enabled: !window.modalOpen && !window.readingView; onActivated: window.activeEdit.redo() }
    Shortcut { sequence: "Ctrl+B"; enabled: !window.modalOpen && window.canFormat; onActivated: window.format("bold") }
    Shortcut { sequence: "Ctrl+I"; enabled: !window.modalOpen && window.canFormat; onActivated: window.format("italic") }
    Shortcut { sequence: "Ctrl+U"; enabled: !window.modalOpen && window.canFormat; onActivated: window.format("underline") }
    Shortcut { sequence: "Ctrl+`"; enabled: !window.modalOpen && window.canFormat; onActivated: window.format("code") }
    Shortcut { sequence: "Ctrl+K"; enabled: !window.modalOpen && window.canFormat; onActivated: window.showLinkDialog() }
    Shortcut { sequence: "Ctrl+0"; enabled: !window.modalOpen && window.canFormat; onActivated: window.setStyle(0) }
    Shortcut { sequence: "Ctrl+1"; enabled: !window.modalOpen && window.canFormat; onActivated: window.setStyle(1) }
    Shortcut { sequence: "Ctrl+2"; enabled: !window.modalOpen && window.canFormat; onActivated: window.setStyle(2) }
    Shortcut { sequence: "Ctrl+3"; enabled: !window.modalOpen && window.canFormat; onActivated: window.setStyle(3) }

    // ------------------------------------------------------------- layout

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: window.readerOnly ? [
            { title: "File", items: window.readerFileMenu },
            { title: "Edit", items: window.readerEditMenu },
            { title: "View", items: window.readerViewMenu },
            { title: "Go", items: window.goMenu },
            { title: "Help", items: window.readerHelpMenu },
        ] : [
            { title: "File", items: window.fileMenu },
            { title: "Edit", items: window.editMenu },
            { title: "View", items: window.viewMenu },
            { title: "Insert", items: window.insertMenu },
            { title: "Format", items: window.formatMenu },
            { title: "Table", items: window.tableMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    component Etch: Item {
        width: parent ? parent.width : 0
        height: 2
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
    }
    component BarSep: Item {
        width: 8
        height: 22
        Rectangle { x: 3; y: 2; width: 1; height: 18; color: Theme.shadow }
        Rectangle { x: 4; y: 2; width: 1; height: 18; color: Theme.highlight }
    }

    Column {
        id: bars
        anchors.top: menuBar.bottom
        anchors.topMargin: 1
        width: parent.width

        Etch {}

        // the reader's toolbar: back, forward, home, find, copy
        Row {
            id: readerBar
            visible: window.showToolbar && window.readerOnly
            x: 4
            height: 28
            topPadding: 3
            spacing: 0

            FormatButton { icon: "back"; tip: "Goes back to the page before (Alt+Left)."; enabled: window.backStack.length > 0; onClicked: window.goBack() }
            FormatButton { icon: "forward"; tip: "Goes forward again (Alt+Right)."; enabled: window.forwardStack.length > 0; onClicked: window.goForward() }
            FormatButton { icon: "home"; tip: "Goes to the first page: the Contents (Alt+Home)."; enabled: !!window.homePath && window.homePath !== window.documentPath; onClicked: window.goHome() }
            BarSep {}
            FormatButton { icon: "find"; tip: "Finds text."; onClicked: findDialog.open(false) }
            FormatButton { icon: "copy"; tip: "Copies the selection to the Clipboard."; enabled: window.hasSelection; onClicked: editor.copy() }
        }

        // the toolbar: files, find, clipboard, undo, date
        Row {
            id: toolbar
            visible: window.showToolbar && !window.readerOnly
            x: 4
            height: 28
            topPadding: 3
            spacing: 0

            FormatButton { icon: "new"; tip: "Creates a new document."; onClicked: window.newFile() }
            FormatButton { icon: "open"; tip: "Opens an existing document."; onClicked: window.showOpen() }
            FormatButton { icon: "save"; tip: "Saves the document."; onClicked: window.save(null) }
            BarSep {}
            FormatButton { icon: "find"; tip: "Finds text."; onClicked: findDialog.open(false) }
            BarSep {}
            FormatButton { icon: "cut"; tip: "Cuts the selection to the Clipboard."; enabled: !window.readingView && window.hasSelection; onClicked: window.activeEdit.cut() }
            FormatButton { icon: "copy"; tip: "Copies the selection to the Clipboard."; enabled: window.hasSelection; onClicked: window.activeEdit.copy() }
            FormatButton { icon: "paste"; tip: "Inserts what's on the Clipboard."; enabled: !window.readingView && window.activeEdit.canPaste; onClicked: window.activeEdit.paste() }
            FormatButton { icon: "undo"; tip: "Undoes the last change."; enabled: !window.readingView && window.activeEdit.canUndo; onClicked: window.activeEdit.undo() }
            BarSep {}
            FormatButton { icon: "datetime"; tip: "Inserts the date and time."; enabled: !window.readingView; onClicked: window.insertDateTime() }
        }
        Etch { visible: window.showToolbar }  // (either toolbar)

        // the format bar: paragraph style, character formats, lists, insert
        Row {
            id: formatBar
            visible: window.showFormatBar && !window.readerOnly
            x: 4
            height: 29
            topPadding: 3
            spacing: 0

            ComboBox95 {
                id: styleCombo
                width: 110
                anchors.verticalCenter: undefined
                y: 1
                model: window.styleNames
                popupMenu: popup
                currentIndex: md.blockStyle
                enabled: window.canFormat
                onActivated: (i) => window.setStyle(i)
                Connections {
                    target: md
                    function onFormatChanged() { styleCombo.currentIndex = md.blockStyle; }
                }
            }
            BarSep {}
            FormatButton { letter: "B"; letterBold: true; tip: "Bold (Ctrl+B)"; enabled: window.canFormat; checked: window.canFormat && md.bold; onClicked: window.format("bold") }
            FormatButton { letter: "I"; letterItalic: true; letterBold: true; tip: "Italic (Ctrl+I)"; enabled: window.canFormat; checked: window.canFormat && md.italic; onClicked: window.format("italic") }
            FormatButton { letter: "U"; letterUnderline: true; letterBold: true; tip: "Underline (Ctrl+U)"; enabled: window.canFormat; checked: window.canFormat && md.underline; onClicked: window.format("underline") }
            FormatButton { letter: "S"; letterStrike: true; letterBold: true; tip: "Strikethrough"; enabled: window.canFormat; checked: window.canFormat && md.strike; onClicked: window.format("strike") }
            FormatButton { icon: "code"; tip: "Code (Ctrl+`)"; enabled: window.canFormat; checked: window.canFormat && md.code; onClicked: window.format("code") }
            FormatButton { icon: "link"; tip: "Link (Ctrl+K)"; enabled: window.canFormat; checked: window.canFormat && !!md.link; onClicked: window.showLinkDialog() }
            BarSep {}
            FormatButton { icon: "bullets"; tip: "Bullets"; enabled: window.canFormat; checked: window.canFormat && md.listStyle === 1; onClicked: window.format("bullets") }
            FormatButton { icon: "numbers"; tip: "Numbering"; enabled: window.canFormat; checked: window.canFormat && md.listStyle === 2; onClicked: window.format("numbers") }
            FormatButton { icon: "checks"; tip: "Check list"; enabled: window.canFormat; checked: window.canFormat && md.listStyle === 3; onClicked: window.format("checks") }
            FormatButton { icon: "outdent"; tip: "Decrease indent (Shift+Tab)"; enabled: window.canFormat; onClicked: window.format("outdent") }
            FormatButton { icon: "indent"; tip: "Increase indent (Tab)"; enabled: window.canFormat; onClicked: window.format("indent") }
            FormatButton { icon: "quote"; tip: "Block quote"; enabled: window.canFormat; checked: window.canFormat && md.quote; onClicked: window.format("quote") }
            BarSep {}
            FormatButton { icon: "picture"; tip: "Inserts a picture."; enabled: window.canFormat; onClicked: window.showPictureDialog() }
            FormatButton { icon: "table"; tip: "Inserts a table."; enabled: window.canFormat; onClicked: window.showTableDialog() }
            FormatButton { icon: "rule"; tip: "Inserts a horizontal line."; enabled: window.canFormat; onClicked: window.format("rule") }
        }
        Etch { visible: window.showFormatBar && !window.readerOnly }
    }

    // the status bar shows what a button does while the mouse is over it
    function buttonTip(row) {
        for (var i = 0; i < row.children.length; ++i) {
            var c = row.children[i];
            if (c.hovered && c.tip)
                return c.tip;
        }
        return "";
    }

    Bevel {
        id: well
        anchors.top: bars.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
        anchors.bottomMargin: statusBar.visible ? 2 : 0
        style: "sunken"
        face: Theme.window

        // the page
        Flickable {
            id: flick
            visible: !window.sourceMode
            x: 2
            y: 2
            width: parent.width - 4 - vbar.width
            height: parent.height - 4
            clip: true
            interactive: false
            contentWidth: width
            contentHeight: Math.max(height, editor.height)

            TextEdit {
                id: editor
                width: flick.width
                height: Math.max(flick.height, implicitHeight)
                leftPadding: 28
                rightPadding: 28
                topPadding: 18
                bottomPadding: 18
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.RichText
                selectByMouse: true
                persistentSelection: true
                readOnly: window.readingView
                focus: true
                color: "#000000"
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
                font.family: "Liberation Serif"
                font.pixelSize: 16
                baseUrl: "file://" + (md.baseDir || Win95Fs.homePath()) + "/"

                onCursorRectangleChanged: if (activeFocus) window.ensureVisible(flick, cursorRectangle)
                onTextChanged: wordTimer.restart()
                // (a read-only TextEdit follows links on a click itself)
                onLinkActivated: (link) => { if (window.readingView) window.openLink(link); }

                Keys.onPressed: (event) => {
                    // (the reader's history keys: the page would take them as cursor moves)
                    if (window.readerOnly) {
                        var alt = event.modifiers & Qt.AltModifier;
                        if ((alt && event.key === Qt.Key_Left) || (event.key === Qt.Key_Backspace && !event.modifiers))
                            window.goBack();
                        else if (alt && event.key === Qt.Key_Right)
                            window.goForward();
                        else if (alt && event.key === Qt.Key_Home)
                            window.goHome();
                        else
                            return;
                        event.accepted = true;
                        return;
                    }
                    if (window.readingView)
                        return;
                    if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ControlModifier)) {
                        if (md.inTable) {
                            var p = md.tableNextCell(false);
                            if (p >= 0)
                                editor.cursorPosition = p;
                            event.accepted = true;
                        } else if (md.listStyle !== 0) {
                            md.indent();
                            event.accepted = true;
                        }
                    } else if (event.key === Qt.Key_Backtab) {
                        if (md.inTable) {
                            var q = md.tableNextCell(true);
                            if (q >= 0)
                                editor.cursorPosition = q;
                            event.accepted = true;
                        } else if (md.listStyle !== 0 || md.quote) {
                            md.outdent();
                            event.accepted = true;
                        }
                    } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                               && md.listStyle !== 0 && md.selectionStart === md.selectionEnd
                               && md.isBlockEmpty(editor.cursorPosition)) {
                        // Enter on an empty list item ends the list (or goes up a level)
                        md.outdent();
                        event.accepted = true;
                    }
                }

                // right-click menu; Ctrl+click follows a link
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    acceptedModifiers: Qt.NoModifier
                    onTapped: (point) => {
                        var x = point.position.x, y = point.position.y;
                        var pos = editor.positionAt(x, y);
                        if (window.readingView || !md.isCheckItem(pos))
                            return;
                        var r = editor.positionToRectangle(md.blockStart(pos));
                        // (TextEdit ticks it itself; this is for the tests)
                        if (x < r.x && x > r.x - 30 && y >= r.y - 2 && y <= r.y + r.height + 2)
                            window.log("check " + pos);
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    acceptedModifiers: Qt.ControlModifier
                    onTapped: (point) => window.openLink(window.linkUnder(point.position.x, point.position.y))
                }
                HoverHandler {
                    id: hover
                    cursorShape: window.hoverLink && window.readingView ? Qt.PointingHandCursor : Qt.IBeamCursor
                    onPointChanged: window.hoverLink = hovered ? window.linkUnder(point.position.x, point.position.y) : ""
                    onHoveredChanged: if (!hovered) window.hoverLink = ""
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    cursorShape: undefined
                    onPressed: (mouse) => {
                        editor.forceActiveFocus();
                        var pos = editor.positionAt(mouse.x, mouse.y);
                        if (pos < editor.selectionStart || pos > editor.selectionEnd || editor.selectionStart === editor.selectionEnd)
                            editor.cursorPosition = pos;
                        var p = mapToItem(popup, mouse.x, mouse.y);
                        popup.popup(p.x, p.y, window.contextMenu());
                    }
                }
            }

            WheelHandler {
                onWheel: (event) => vbar.scrollBy(-event.angleDelta.y / 120 * 54)
            }
        }
        ScrollBar95 {
            id: vbar
            visible: !window.sourceMode
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: flick
            stepSize: 20
        }

        // the Markdown view
        Flickable {
            id: sourceFlick
            visible: window.sourceMode
            x: 2
            y: 2
            width: parent.width - 4 - sourceBar.width
            height: parent.height - 4
            clip: true
            interactive: false
            contentWidth: width
            contentHeight: Math.max(height, sourceEdit.height)

            TextEdit {
                id: sourceEdit
                width: sourceFlick.width
                height: Math.max(sourceFlick.height, implicitHeight)
                leftPadding: 6
                rightPadding: 6
                topPadding: 4
                bottomPadding: 4
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                persistentSelection: true
                color: Theme.text
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
                font.family: Theme.monoFamily
                font.pixelSize: 13
                tabStopDistance: 32
                onTextChanged: {
                    if (window.sourceMode && activeFocus)
                        window.sourceChanged = true;
                    wordTimer.restart();
                }
                onCursorRectangleChanged: if (activeFocus) window.ensureVisible(sourceFlick, cursorRectangle)

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    cursorShape: Qt.IBeamCursor
                    onPressed: (mouse) => {
                        sourceEdit.forceActiveFocus();
                        var p = mapToItem(popup, mouse.x, mouse.y);
                        popup.popup(p.x, p.y, window.contextMenu());
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => sourceBar.scrollBy(-event.angleDelta.y / 120 * 48)
            }
        }
        ScrollBar95 {
            id: sourceBar
            visible: window.sourceMode
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: sourceFlick
            stepSize: 16
        }
    }

    function ensureVisible(f, r) {
        if (r.y < f.contentY)
            f.contentY = Math.max(0, r.y - 4);
        else if (r.y + r.height > f.contentY + f.height)
            f.contentY = Math.min(f.contentHeight - f.height, r.y + r.height + 4 - f.height);
    }

    MarkdownDocument {
        id: md
        document: editor.textDocument
        cursorPosition: editor.cursorPosition
        selectionStart: editor.selectionStart
        selectionEnd: editor.selectionEnd
        onSelectRequested: (s, e) => {
            if (s === e)
                editor.cursorPosition = s;
            else
                editor.select(s, e);
        }
    }

    Timer {
        id: wordTimer
        interval: 400
        onTriggered: window.countWords()
    }

    // ---------------------------------------------------------- status bar

    Item {
        id: statusBar
        visible: window.showStatusBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 22

        readonly property string hint: {
            var t = window.buttonTip(toolbar) || window.buttonTip(formatBar) || window.buttonTip(readerBar);
            if (t)
                return t;
            if (!window.sourceMode && window.hoverLink)
                return window.hoverLink + (window.readingView ? "" : "  (Ctrl+click to open)");
            if (window.readerOnly)
                return window.backStack.length > 0 ? "Click a link to go to it; Back (Alt+Left) comes back." : "Click a link to go to it.";
            if (window.readingView)
                return "Reading View: click a link to open it.";
            if (window.sourceMode)
                return "Markdown: press Ctrl+M to go back to the page.";
            return "For Help, press F1";
        }

        Bevel {
            x: 2
            y: 2
            width: parent.width - wordsBox.width - 8
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 8
                elide: Text.ElideRight
                text: statusBar.hint
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
        }
        Bevel {
            id: wordsBox
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            width: 110
            height: 19
            style: "sunken"
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: window.words === 1 ? "1 word" : window.words + " words"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
        }
    }

    // -------------------------------------------------------------- dialogs

    FileDialog95 {
        id: fileDialog
        messageBox: msgBox
        popupMenu: popup
        filters: window.markdownFilters
        defaultSuffix: "md"
        onAccepted: (path) => {
            if (mode === "open") {
                window.openFile(path);
                window.focusEditor();
                return;
            }
            window.documentPath = path;
            var then = window.afterSave;
            window.afterSave = null;
            window.save(then);
        }
        onClosed: if (!msgBox.visible) window.focusEditor()
    }

    FileDialog95 {
        id: exportDialog
        messageBox: msgBox
        popupMenu: popup
        acceptLabel: "Export"
        filters: [{ label: "Web Pages (*.html)", patterns: ["*.html", "*.htm"] }]
        defaultSuffix: "html"
        onAccepted: (path) => {
            var error = md.exportHtml(path);
            if (error)
                msgBox.show("Markdown Editor", error, "error", ["OK"], window.focusEditor);
            else
                window.log("exported " + path);
        }
        onClosed: if (!msgBox.visible) window.focusEditor()
    }

    FileDialog95 {
        id: pictureDialog
        messageBox: msgBox
        popupMenu: popup
        acceptLabel: "Insert"
        filters: [
            { label: "Pictures (*.png;*.jpg;*.gif;*.svg)", patterns: ["*.png", "*.jpg", "*.jpeg", "*.gif", "*.svg", "*.webp", "*.bmp"] },
            { label: "All Files (*.*)", patterns: ["*"] },
        ]
        onAccepted: (path) => {
            var name = Win95Fs.fileName(path).replace(/\.[^.]*$/, "");
            md.insertImage(md.relativePath(path), name);
            window.log("picture " + md.relativePath(path));
            window.focusEditor();
        }
        onClosed: if (!msgBox.visible) window.focusEditor()
    }

    Dialog95 {
        id: linkDialog
        title: "Link"
        dialogWidth: 380
        dialogHeight: 128
        property int start: 0
        property int end: 0
        property bool hadLink: false
        onClosed: window.focusEditor()

        Text {
            x: 10; y: 12; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Text to display:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: linkText
            x: 100; y: 12
            width: 180
            KeyNavigation.tab: linkUrl
            onAccepted: window.acceptLink()
            onEscaped: linkDialog.close()
        }
        Text {
            x: 10; y: 42; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Address:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: linkUrl
            x: 100; y: 42
            width: 180
            KeyNavigation.tab: linkText
            onAccepted: window.acceptLink()
            onEscaped: linkDialog.close()
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 11
            text: "OK"
            isDefault: true
            onClicked: window.acceptLink()
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 40
            text: "Cancel"
            onClicked: linkDialog.close()
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 69
            text: "Remove Link"
            enabled: linkDialog.hadLink
            onClicked: {
                linkDialog.close();
                md.setLink(linkDialog.start, linkDialog.end, "", "");
            }
        }
    }

    Dialog95 {
        id: tableDialog
        title: "Insert Table"
        dialogWidth: 250
        dialogHeight: 110
        onClosed: window.focusEditor()

        function accept() {
            tableDialog.close();
            md.insertTable(rowsSpin.value, colsSpin.value);
            window.log("table " + rowsSpin.value + "x" + colsSpin.value);
            editor.forceActiveFocus();
        }

        Text {
            x: 10; y: 12; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Rows:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SpinBox95 { id: rowsSpin; x: 80; y: 12; width: 60; from: 1; to: 100; value: 3 }
        Text {
            x: 10; y: 42; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Columns:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        SpinBox95 { id: colsSpin; x: 80; y: 42; width: 60; from: 1; to: 20; value: 3 }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 11
            text: "OK"
            isDefault: true
            onClicked: tableDialog.accept()
        }
        Button95 {
            anchors.right: parent.right; anchors.rightMargin: 10
            y: 40
            text: "Cancel"
            onClicked: tableDialog.close()
        }
        Keys.onReturnPressed: tableDialog.accept()
        Keys.onEnterPressed: tableDialog.accept()
    }

    // Find and Replace: modeless, so you can keep editing behind it
    Dialog95 {
        id: findDialog
        title: replacing ? "Replace" : "Find"
        modal: false
        dialogWidth: 380
        dialogHeight: replacing ? 150 : 120

        property bool replacing: false
        property alias searchText: findField.text
        property alias replaceText: replaceField.text
        property alias matchCase: matchCaseBox.checked
        property bool searchDown: true

        function open(replace) {
            replacing = replace && !window.readingView;
            var sel = window.activeEdit.selectedText;
            if (sel.length > 0 && sel.indexOf("\n") < 0 && sel.indexOf(" ") < 0)
                findField.text = sel;
            show();
            refocus();
        }
        function refocus() {
            findField.forceActiveFocus();
            findField.input.selectAll();
        }
        onClosed: window.focusEditor()

        Text {
            x: 10; y: 12; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Find what:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: findField
            x: 84; y: 12
            width: 190
            focus: true
            onAccepted: window.findNext()
            onEscaped: findDialog.close()
        }
        Text {
            visible: findDialog.replacing
            x: 10; y: 42; height: 21
            verticalAlignment: Text.AlignVCenter
            text: "Replace with:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: replaceField
            visible: findDialog.replacing
            x: 84; y: 42
            width: 190
            onAccepted: window.replaceOne()
            onEscaped: findDialog.close()
        }
        Column {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 11
            spacing: 5
            Button95 { width: 80; text: "Find Next"; isDefault: true; onClicked: window.findNext() }
            Button95 { width: 80; visible: findDialog.replacing; text: "Replace"; onClicked: window.replaceOne() }
            Button95 { width: 80; visible: findDialog.replacing; text: "Replace All"; onClicked: window.replaceAll() }
            Button95 { width: 80; text: "Cancel"; onClicked: findDialog.close() }
        }
        CheckBox95 {
            id: matchCaseBox
            x: 10
            y: findDialog.replacing ? 92 : 62
            text: "Match <u>c</u>ase"
        }
        Row {
            visible: !findDialog.replacing
            x: 120
            y: 62
            spacing: 12
            RadioButton95 {
                text: "<u>U</u>p"
                checked: !findDialog.searchDown
                onClicked: findDialog.searchDown = false
            }
            RadioButton95 {
                text: "<u>D</u>own"
                checked: findDialog.searchDown
                onClicked: findDialog.searchDown = true
            }
        }
    }

    // window-level overlays (last, so they stack on top)
    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen && !findDialog.visible) window.focusEditor()
    }
    MessageBox95 { id: msgBox }
}
