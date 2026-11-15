import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Notepad

// Win95-style Notepad: a menu bar over a sunken, plain-text editing area.
Window {
    id: window

    width: 620
    height: 440
    minimumWidth: 240
    minimumHeight: 160
    visible: true
    color: Theme.face
    title: (documentPath ? Win95Fs.fileName(documentPath) : "Untitled") + " - Notepad"

    property string documentPath: ""
    property bool modified: false
    property bool crlf: false
    property bool wordWrap: false
    property bool loading: false
    property bool quitting: false
    property var afterSave: null

    readonly property bool modalOpen: fileDialog.visible || msgBox.visible || popup.visible

    Component.onCompleted: {
        if (initialFile)
            openFile(initialFile);
        editor.forceActiveFocus();
    }

    // closing the window (title bar X, Exit) asks about unsaved changes
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

    function focusEditor() { editor.forceActiveFocus(); }

    function currentDir() {
        return documentPath ? Win95Fs.parentDir(documentPath) : Win95Fs.homePath();
    }

    function setText(text) {
        loading = true;
        editor.text = text;
        loading = false;
        modified = false;
        editor.cursorPosition = 0;
        flick.contentX = 0;
        flick.contentY = 0;
    }

    function openFile(path) {
        var r = DocumentIO.load(path);
        if (!r.ok) {
            msgBox.show("Notepad", r.error, "error", ["OK"], focusEditor);
            return;
        }
        documentPath = r.path;
        crlf = r.crlf;
        setText(r.text);
    }

    // run `then` once the user has saved, discarded, or there was nothing
    // to lose; Cancel drops it
    function confirmDiscard(then) {
        if (!modified) {
            then();
            return;
        }
        msgBox.show("Notepad",
                    "The text in the " + (documentPath ? documentPath : "Untitled")
                    + " file has changed.\n\nDo you want to save the changes?",
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
            crlf = false;
            setText("");
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
        var error = DocumentIO.save(documentPath, editor.text, crlf);
        if (error) {
            msgBox.show("Notepad", error, "error", ["OK"], focusEditor);
            return;
        }
        modified = false;
        if (then)
            then();
        else
            focusEditor();
    }

    function saveAs(then) {
        afterSave = then || null;
        fileDialog.mode = "save";
        fileDialog.openAt(currentDir(), documentPath ? Win95Fs.fileName(documentPath) : "Untitled.txt");
    }

    // --------------------------------------------------------------- editing

    function deleteSelection() {
        if (editor.selectionStart !== editor.selectionEnd)
            editor.remove(editor.selectionStart, editor.selectionEnd);
    }

    function insertTimeDate() {
        deleteSelection();
        editor.insert(editor.cursorPosition, DocumentIO.timeDate());
    }

    function ensureVisible(r) {
        if (r.x < flick.contentX)
            flick.contentX = Math.max(0, r.x - 20);
        else if (r.x + r.width + 4 > flick.contentX + flick.width)
            flick.contentX = r.x + r.width + 24 - flick.width;
        if (r.y < flick.contentY)
            flick.contentY = r.y;
        else if (r.y + r.height > flick.contentY + flick.height)
            flick.contentY = r.y + r.height - flick.height;
    }

    function findNext() {
        var needle = findDialog.searchText;
        if (needle.length === 0) {
            findDialog.open();
            return;
        }
        var hay = findDialog.matchCase ? editor.text : editor.text.toLowerCase();
        var n = findDialog.matchCase ? needle : needle.toLowerCase();
        var idx = -1;
        if (findDialog.searchDown)
            idx = hay.indexOf(n, editor.selectionEnd);
        else if (editor.selectionStart > 0)
            idx = hay.lastIndexOf(n, editor.selectionStart - 1);
        if (idx < 0) {
            msgBox.show("Notepad", "Cannot find \"" + needle + "\"", "info", ["OK"], function () {
                if (findDialog.visible)
                    findDialog.refocus();
                else
                    focusEditor();
            });
            return;
        }
        editor.select(idx, idx + needle.length);
        ensureVisible(editor.positionToRectangle(idx + needle.length));
        ensureVisible(editor.positionToRectangle(idx));
    }

    // ----------------------------------------------------------------- menus

    function fileMenu() {
        return [
            { text: "New", shortcut: "Ctrl+N", action: window.newFile },
            { text: "Open...", shortcut: "Ctrl+O", action: window.showOpen },
            { text: "Save", shortcut: "Ctrl+S", action: function () { window.save(null); } },
            { text: "Save As...", action: function () { window.saveAs(null); } },
            { separator: true },
            { text: "Page Setup...", enabled: false },
            { text: "Print", enabled: false },
            { separator: true },
            { text: "Exit", action: window.close },
        ];
    }

    function editMenu() {
        var hasSel = editor.selectionStart !== editor.selectionEnd;
        return [
            { text: "Undo", shortcut: "Ctrl+Z", enabled: editor.canUndo, action: editor.undo },
            { separator: true },
            { text: "Cut", shortcut: "Ctrl+X", enabled: hasSel, action: editor.cut },
            { text: "Copy", shortcut: "Ctrl+C", enabled: hasSel, action: editor.copy },
            { text: "Paste", shortcut: "Ctrl+V", enabled: editor.canPaste, action: editor.paste },
            { text: "Delete", shortcut: "Del", enabled: hasSel, action: window.deleteSelection },
            { separator: true },
            { text: "Select All", shortcut: "Ctrl+A", action: editor.selectAll },
            { text: "Time/Date", shortcut: "F5", action: window.insertTimeDate },
            { separator: true },
            { text: "Word Wrap", checked: window.wordWrap, action: function () { window.wordWrap = !window.wordWrap; } },
        ];
    }

    function searchMenu() {
        return [
            { text: "Find...", shortcut: "Ctrl+F", action: function () { findDialog.open(); } },
            { text: "Find Next", shortcut: "F3", action: window.findNext },
        ];
    }

    function helpMenu() {
        return [
            { text: "About Notepad", action: function () {
                msgBox.show("About Notepad", "Notepad\n\nA plain-text editor for the Wayland 95 desktop.",
                            "info", ["OK"], window.focusEditor);
            } },
        ];
    }

    function contextMenu() {
        var hasSel = editor.selectionStart !== editor.selectionEnd;
        return [
            { text: "Undo", enabled: editor.canUndo, action: editor.undo },
            { separator: true },
            { text: "Cut", enabled: hasSel, action: editor.cut },
            { text: "Copy", enabled: hasSel, action: editor.copy },
            { text: "Paste", enabled: editor.canPaste, action: editor.paste },
            { text: "Delete", enabled: hasSel, action: window.deleteSelection },
            { separator: true },
            { text: "Select All", action: editor.selectAll },
        ];
    }

    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen; onActivated: window.newFile() }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.modalOpen; onActivated: window.showOpen() }
    Shortcut { sequence: "Ctrl+S"; enabled: !window.modalOpen; onActivated: window.save(null) }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.modalOpen; onActivated: findDialog.open() }
    Shortcut { sequence: "F3"; enabled: !window.modalOpen; onActivated: window.findNext() }
    Shortcut { sequence: "F5"; enabled: !window.modalOpen && editor.activeFocus; onActivated: window.insertTimeDate() }

    // ---------------------------------------------------------------- layout

    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Edit", items: window.editMenu },
            { title: "Search", items: window.searchMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    Bevel {
        id: well
        anchors.top: menuBar.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        style: "sunken"
        face: Theme.window

        Flickable {
            id: flick
            x: 2
            y: 2
            width: parent.width - 4 - (vbar.visible ? vbar.width : 0)
            height: parent.height - 4 - (hbar.visible ? hbar.height : 0)
            clip: true
            interactive: false
            contentWidth: window.wordWrap ? width : Math.max(width, editor.implicitWidth + 24)
            contentHeight: Math.max(height, editor.implicitHeight)

            TextEdit {
                id: editor
                width: window.wordWrap ? flick.width : Math.max(flick.width, implicitWidth + 24)
                height: Math.max(flick.height, implicitHeight)
                leftPadding: 3
                topPadding: 1
                wrapMode: window.wordWrap ? TextEdit.Wrap : TextEdit.NoWrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                persistentSelection: true
                focus: true
                color: Theme.text
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
                font.family: Theme.monoFamily
                font.pixelSize: 13
                tabStopDistance: metrics.averageCharacterWidth * 8

                onTextChanged: if (!window.loading) window.modified = true
                onCursorRectangleChanged: if (activeFocus) window.ensureVisible(cursorRectangle)

                FontMetrics {
                    id: metrics
                    font: editor.font
                }

                // right-click menu (left button stays with the editor)
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    cursorShape: Qt.IBeamCursor
                    onPressed: (mouse) => {
                        editor.forceActiveFocus();
                        var p = mapToItem(popup, mouse.x, mouse.y);
                        popup.popup(p.x, p.y, window.contextMenu());
                    }
                }
            }

            WheelHandler {
                onWheel: (event) => {
                    if (event.modifiers & Qt.ShiftModifier)
                        hbar.scrollBy(-event.angleDelta.y / 120 * 48);
                    else
                        vbar.scrollBy(-event.angleDelta.y / 120 * 48);
                    hbar.scrollBy(-event.angleDelta.x / 120 * 48);
                }
            }
        }

        ScrollBar95 {
            id: vbar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: flick.height
            flickable: flick
            stepSize: metrics.height
        }
        ScrollBar95 {
            id: hbar
            orientation: Qt.Horizontal
            x: 2
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 2
            width: flick.width
            flickable: flick
            stepSize: metrics.averageCharacterWidth * 4
        }
        // gray square where the two scrollbars meet
        Rectangle {
            visible: vbar.visible && hbar.visible
            x: vbar.x
            y: hbar.y
            width: vbar.width
            height: hbar.height
            color: Theme.face
        }
    }

    // ---------------------------------------------------------------- dialogs

    FileDialog95 {
        id: fileDialog
        messageBox: msgBox
        popupMenu: popup
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

    // Win95's Find box: modeless, so you can keep editing behind it
    Dialog95 {
        id: findDialog
        title: "Find"
        modal: false
        dialogWidth: 370
        dialogHeight: 120

        property alias searchText: findField.text
        property alias matchCase: matchCaseBox.checked
        property bool searchDown: true

        function open() {
            var sel = editor.selectedText;
            if (sel.length > 0 && sel.indexOf("\n") < 0)
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
            x: 10
            y: 12
            height: 21
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.StyledText
            text: "Fi<u>n</u>d what:"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 {
            id: findField
            x: 76
            y: 12
            width: 190
            focus: true
            onAccepted: window.findNext()
            onEscaped: findDialog.close()
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 11
            text: "Find Next"
            isDefault: true
            onClicked: window.findNext()
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 40
            text: "Cancel"
            onClicked: findDialog.close()
        }

        CheckBox95 {
            id: matchCaseBox
            x: 10
            y: 62
            text: "Match <u>c</u>ase"
        }

        // "Direction" group box
        Item {
            x: 120
            y: 42
            width: 146
            height: 40
            Rectangle { x: 1; y: 7; width: parent.width - 1; height: parent.height - 7; color: "transparent"; border.color: Theme.highlight }
            Rectangle { x: 0; y: 6; width: parent.width - 1; height: parent.height - 7; color: "transparent"; border.color: Theme.shadow }
            Rectangle {
                x: 6
                y: 0
                width: groupLabel.implicitWidth + 4
                height: groupLabel.implicitHeight
                color: Theme.face
                Text {
                    id: groupLabel
                    x: 2
                    text: "Direction"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Row {
                x: 10
                y: 16
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
    }

    // window-level overlays (last, so they stack on top)
    Menu95 {
        id: popup
        onClosed: if (!msgBox.visible && !fileDialog.visible && !findDialog.visible) window.focusEditor()
    }
    MessageBox95 { id: msgBox }
}
