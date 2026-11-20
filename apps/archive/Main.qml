import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Archive

// Archive Manager: an archive's files in a list (name, date, size, ratio,
// packed size, path), with New, Open, Add, Extract, View, Delete and Test
// on its toolbar. Files dropped on the window are added to the archive
// open (or to a new one).
//
// Run with --extract-here / --extract-subfolders / --extract-to / --add /
// --add-dialog (the file manager's right-click menu), the window is just
// the progress box (and the Add box), and closes when it's done.
Window {
    id: window

    readonly property bool compact: cliMode !== ""
    // the progress box is the size it says (a fixed size: the compositor
    // keeps it, rather than one remembered from before)
    property int boxWidth: 380
    property int boxHeight: 130
    width: compact ? boxWidth : 720
    height: compact ? boxHeight : 460
    minimumWidth: compact ? boxWidth : 420
    minimumHeight: compact ? boxHeight : 260
    maximumWidth: compact ? boxWidth : 16777215
    maximumHeight: compact ? boxHeight : 16777215
    visible: true
    color: Theme.face
    title: compact ? (addDialog.visible ? "Add to Archive" : (doc.operation || "Archive Manager")) + " - Archive Manager"
                   : (doc.loaded ? doc.fileName + " - Archive Manager" : "Archive Manager")

    property string status: ""
    property bool showToolbar: true
    // what to do once the archive being opened is open
    property var afterOpen: null
    // extract's "open the folder afterwards"
    property string openWhenDone: ""
    // files waiting for a new archive to go into
    property var pendingFiles: []
    property bool testPasswordUsed: false
    readonly property bool modalOpen: msgBox.visible || popup.visible || openDialog.visible || newDialog.visible
                                      || addFilesDialog.visible || addFolderDialog.visible || folderDialog.visible
                                      || saveDialog.visible || addDialog.visible || extractDialog.visible
                                      || passwordDialog.visible || progress.visible || formatsDialog.visible

    function log(msg) {
        if (archiveDebug)
            console.log("archive: " + msg);
    }

    function refocus() {
        if (!window.compact && !window.modalOpen)
            list.forceActiveFocus();
    }

    function lastFolder() {
        var f = Archives.setting("folder", "");
        return f && Win95Fs.isDir(f) ? f : Win95Fs.homePath();
    }

    ArchiveDocument {
        id: doc

        onOpened: {
            window.log("opened " + doc.path + " format=" + doc.format + " tool=" + doc.tool + " files=" + doc.fileCount
                       + " encrypted=" + doc.encrypted);
            var then = window.afterOpen;
            window.afterOpen = null;
            if (then)
                then();
            else
                window.refocus();
        }
        onFailed: (title, message) => {
            window.log("failed: " + message.replace(/\n+/g, " "));
            window.afterOpen = null;
            window.error(title, message, window.compact ? cli.next : window.refocus);
        }
        onPasswordNeeded: (operation, wrong) => {
            window.log("password needed for " + operation + (wrong ? " (wrong)" : ""));
            if (testPassword && !window.testPasswordUsed) {
                window.testPasswordUsed = true;
                return doc.retry(testPassword);
            }
            if (window.compact)
                window.fit(passwordDialog.dialogWidth, passwordDialog.dialogHeight);
            passwordDialog.ask(Win95Fs.fileName(doc.path || cli.current), wrong);
        }
        onOverwriteAsked: (path) => {
            window.log("overwrite? " + path);
            if (testOverwrite >= 0)
                return doc.answerOverwrite(testOverwrite);
            if (window.compact)
                window.fit(440, 150);
            msgBox.show("Confirm File Replace", "This folder already has a file called\n" + Win95Fs.fileName(path)
                        + ".\n\nWould you like to replace it?", "question",
                        ["Yes", "Yes to All", "No", "No to All", "Cancel"], function (i) {
                            if (window.compact)
                                window.fit(progress.dialogWidth, progress.dialogHeight);
                            doc.answerOverwrite(i);
                        });
        }
        onFinished: (operation, ok, summary, problems, written) => {
            window.log(operation + (ok ? " ok: " : " failed: ") + summary + (problems.length ? " problems=" + problems.join("|") : ""));
            window.status = summary;
            var then = window.compact ? cli.next : window.refocus;
            var more = problems.length ? "\n\n" + problems.slice(0, 12).join("\n") + (problems.length > 12 ? "\n..." : "") : "";
            if (operation === "test") {
                window.message("Test", summary + more, ok ? "info" : "error", then);
            } else if (!ok && summary.indexOf("cancelled") < 0) {
                window.error(operation === "add" ? "Add" : operation === "delete" ? "Delete" : "Extract", summary + more, then);
            } else if (problems.length) {
                window.message(operation === "extract" ? "Extract" : "Archive Manager",
                               (operation === "extract" ? "Some files weren't extracted:" : "Some files were skipped:") + more,
                               "warning", then);
            } else {
                if (operation === "extract" && ok && window.openWhenDone)
                    Archives.showFolder(written.length === 1 ? written[0] : window.openWhenDone);
                then();
            }
            if (operation === "extract")
                window.openWhenDone = "";
        }
    }

    // ------------------------------------------------------------- messages

    function fit(w, h) {
        if (!compact)
            return;
        window.boxWidth = Math.max(window.boxWidth, w);
        window.boxHeight = Math.max(window.boxHeight, h);
    }
    function message(title, text, icon, then) {
        log("message " + icon + ": " + text.replace(/\n+/g, " "));
        if (compact)
            fit(460, 120 + Math.min(14, text.split("\n").length) * 15);
        msgBox.show(title, text, icon, ["OK"], function () { if (then) then(); });
    }
    function error(title, text, then) {
        message(title, text, "error", then);
    }

    // ---------------------------------------------------------------- actions

    function openArchive(path) {
        if (!path)
            return;
        Archives.setSetting("folder", Win95Fs.parentDir(path));
        window.status = "";
        doc.open(path);
    }

    function newArchive(files) {
        pendingFiles = files || [];
        var name = "";
        if (pendingFiles.length)
            name = Archives.baseName(Win95Fs.fileName(pendingFiles.length === 1 ? pendingFiles[0] : Win95Fs.parentDir(pendingFiles[0])))
                   + "." + newDialog.filters[newDialog.filterIndex].suffix;
        newDialog.openAt(pendingFiles.length ? Win95Fs.parentDir(pendingFiles[0]) : lastFolder(), name);
    }

    function createArchive(path) {
        var format = Archives.formatForName(Win95Fs.fileName(path));
        if (!format || !Archives.canWrite(format) && !Archives.isSingleFile(format)) {
            error("New Archive", "Archive Manager can't make that kind of archive. Name it .zip, .7z, .tar.gz, "
                  + ".tar.bz2, .tar.xz, .tar.zst or .tar.", function () { window.newArchive(window.pendingFiles); });
            return;
        }
        Archives.setSetting("folder", Win95Fs.parentDir(path));
        var files = pendingFiles;
        pendingFiles = [];
        if (Archives.exists(path)) {
            // as WinZip did: an archive that's there is opened
            afterOpen = function () { if (files.length) window.addFiles(files); };
            doc.open(path);
            return;
        }
        doc.create(path, format);
        window.status = "A new archive: add files to it with Add, or drag them here.";
        log("new " + path + " " + format);
        if (files.length)
            addFiles(files);
        else
            chooseFiles();
    }

    function chooseFiles() {
        if (!doc.loaded)
            return newArchive([]);
        if (!doc.writable)
            return error("Add", doc.readOnlyReason, window.refocus);
        addFilesDialog.openAt(Archives.setting("addFolder", "") || Win95Fs.parentDir(doc.path), "");
    }

    function chooseFolder() {
        if (!doc.loaded)
            return newArchive([]);
        if (!doc.writable)
            return error("Add", doc.readOnlyReason, window.refocus);
        addFolderDialog.openAt(Archives.setting("addFolder", "") || Win95Fs.parentDir(doc.path), "");
    }

    // the Add box for these files (into the archive open)
    function addFiles(files) {
        if (!files.length)
            return;
        if (!doc.writable)
            return error("Add", doc.readOnlyReason, window.refocus);
        addDialog.askArchive = false;
        addDialog.newArchive = false;
        addDialog.existingFormat = doc.format;
        addDialog.open(files, "");
    }

    function extract() {
        if (!doc.loaded || doc.count === 0)
            return;
        var dir = Win95Fs.parentDir(doc.path);
        extractDialog.selectionCount = doc.selectionCount;
        extractDialog.open(Win95Fs.resolve(dir, Archives.baseName(doc.fileName)));
    }

    function viewSelected() {
        var row = doc.firstSelectedRow();
        if (row >= 0)
            doc.view(row);
    }

    function deleteSelected() {
        if (doc.selectionCount === 0)
            return;
        if (!doc.writable)
            return error("Delete", doc.readOnlyReason, window.refocus);
        var n = doc.selectionCount;
        msgBox.show("Delete", "Delete " + (n === 1 ? doc.pathAt(doc.firstSelectedRow()) : n + " items")
                    + " from " + doc.fileName + "?", "question", ["Yes", "No"], function (i) {
                        if (i === 0)
                            doc.removeSelected();
                        else
                            window.refocus();
                    });
    }

    function dropped(paths) {
        if (!paths.length)
            return;
        log("dropped " + paths.join(" "));
        if (paths.length === 1 && Archives.formatForName(Win95Fs.fileName(paths[0])) && !doc.loaded && Win95Fs.isFile(paths[0]))
            openArchive(paths[0]);
        else if (doc.loaded)
            addFiles(paths);
        else
            newArchive(paths);
    }

    // ---------------------------------------------------------------- menus

    function fileMenu() {
        return [
            { text: "&New Archive...", shortcut: "Ctrl+N", action: function () { window.newArchive([]); } },
            { text: "&Open Archive...", shortcut: "Ctrl+O", action: function () { openDialog.openAt(window.lastFolder(), ""); } },
            { text: "&Close Archive", enabled: doc.loaded, action: function () { doc.close(); window.status = ""; } },
            { separator: true },
            { text: "&Show in Folder", enabled: doc.loaded && Archives.exists(doc.path), action: function () { Archives.showFolder(doc.path); } },
            { text: "&Reload", shortcut: "F5", enabled: doc.loaded, action: doc.reload },
            { separator: true },
            { text: "E&xit", action: function () { Qt.quit(); } },
        ];
    }
    function actionsMenu() {
        var sel = doc.selectionCount;
        return [
            { text: "&Add Files...", enabled: !doc.loaded || doc.writable, action: window.chooseFiles },
            { text: "Add &Folder...", enabled: !doc.loaded || doc.writable, action: window.chooseFolder },
            { text: "&Delete...", shortcut: "Del", enabled: sel > 0 && doc.writable, action: window.deleteSelected },
            { text: "&Extract...", shortcut: "Ctrl+E", enabled: doc.count > 0, action: window.extract },
            { text: "&View", shortcut: "Enter", enabled: sel > 0, action: window.viewSelected },
            { separator: true },
            { text: "Select &All", shortcut: "Ctrl+A", enabled: doc.count > 0, action: doc.selectAll },
            { text: "&Test", shortcut: "Ctrl+T", enabled: doc.count > 0, action: doc.test },
        ];
    }
    function viewMenu() {
        var cols = [["Name", ArchiveDocument.SortName], ["Modified", ArchiveDocument.SortModified],
                    ["Size", ArchiveDocument.SortSize], ["Ratio", ArchiveDocument.SortRatio],
                    ["Packed", ArchiveDocument.SortPacked], ["Path", ArchiveDocument.SortPath]];
        return [
            { text: "&Toolbar", checked: window.showToolbar, action: function () { window.showToolbar = !window.showToolbar; } },
            { separator: true },
        ].concat(cols.map(function (c) {
            return { text: "Sort by " + c[0], checked: doc.sortColumn === c[1], action: function () { doc.sortColumn = c[1]; } };
        })).concat([
            { text: "&Descending", checked: !doc.sortAscending, action: function () { doc.sortAscending = !doc.sortAscending; } },
        ]);
    }
    function helpMenu() {
        return [
            { text: "Formats and &Programs", action: function () { formatsDialog.show(); } },
            { separator: true },
            { text: "&About Archive Manager", action: function () {
                window.message("About Archive Manager",
                               "Archive Manager\n\nOpens, extracts and makes zip, 7z and tar archives, "
                               + "and reads rar, iso and cab, for the Wayland 95 desktop.", "info", window.refocus);
            } },
        ];
    }
    function rowMenu() {
        var sel = doc.selectionCount;
        return [
            { text: "&View", enabled: sel > 0, action: window.viewSelected },
            { text: "&Extract...", action: window.extract },
            { separator: true },
            { text: "&Delete", enabled: sel > 0 && doc.writable, action: window.deleteSelected },
            { separator: true },
            { text: "Select &All", action: doc.selectAll },
        ];
    }

    // -------------------------------------------------------------- shortcuts

    readonly property bool keys: !compact && !modalOpen && !doc.busy
    Shortcut { sequence: "Ctrl+N"; enabled: window.keys; onActivated: window.newArchive([]) }
    Shortcut { sequence: "Ctrl+O"; enabled: window.keys; onActivated: openDialog.openAt(window.lastFolder(), "") }
    Shortcut { sequence: "Ctrl+E"; enabled: window.keys && doc.count > 0; onActivated: window.extract() }
    Shortcut { sequence: "Ctrl+T"; enabled: window.keys && doc.count > 0; onActivated: doc.test() }
    Shortcut { sequence: "Ctrl+A"; enabled: window.keys; onActivated: doc.selectAll() }
    Shortcut { sequence: "Del"; enabled: window.keys && doc.selectionCount > 0; onActivated: window.deleteSelected() }
    Shortcut { sequence: "F5"; enabled: window.keys && doc.loaded; onActivated: doc.reload() }

    // ----------------------------------------------------------------- layout

    component Etch: Item {
        width: parent ? parent.width : 0
        height: 2
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
    }
    component BarSep: Item {
        width: 8
        height: 54
        Rectangle { x: 3; y: 4; width: 1; height: 46; color: Theme.shadow }
        Rectangle { x: 4; y: 4; width: 1; height: 46; color: Theme.highlight }
    }


    Item {
        id: main
        anchors.fill: parent
        visible: !window.compact

        MenuBar95 {
            id: menuBar
            x: 2
            y: 2
            width: parent.width - 4
            height: 20
            popupMenu: popup
            menus: [
                { title: "File", items: window.fileMenu },
                { title: "Actions", items: window.actionsMenu },
                { title: "View", items: window.viewMenu },
                { title: "Help", items: window.helpMenu },
            ]
        }

        Column {
            id: bars
            anchors.top: menuBar.bottom
            anchors.topMargin: 1
            width: parent.width
            Etch {}
            Row {
                id: toolbar
                visible: window.showToolbar
                x: 4
                height: 58
                topPadding: 2
                ArchiveButton { icon: "new"; text: "New"; tip: "Makes a new archive (Ctrl+N)."; onClicked: window.newArchive([]) }
                ArchiveButton { icon: "open"; text: "Open"; tip: "Opens an archive (Ctrl+O)."; onClicked: openDialog.openAt(window.lastFolder(), "") }
                BarSep {}
                ArchiveButton { icon: "add"; text: "Add"; tip: "Adds files to the archive (or drag them onto the window).";
                                enabled: !doc.loaded || doc.writable; onClicked: window.chooseFiles() }
                ArchiveButton { icon: "extract"; text: "Extract"; tip: "Extracts the selected files, or all of them, to a folder (Ctrl+E).";
                                enabled: doc.count > 0; onClicked: window.extract() }
                ArchiveButton { icon: "view"; text: "View"; tip: "Opens the selected file with its program (Enter).";
                                enabled: doc.selectionCount > 0; onClicked: window.viewSelected() }
                BarSep {}
                ArchiveButton { icon: "delete"; text: "Delete"; tip: "Deletes the selected files from the archive (Del).";
                                enabled: doc.selectionCount > 0 && doc.writable; onClicked: window.deleteSelected() }
                ArchiveButton { icon: "test"; text: "Test"; tip: "Checks that every file in the archive can be extracted (Ctrl+T).";
                                enabled: doc.count > 0; onClicked: doc.test() }
            }
            Etch { visible: window.showToolbar }
        }

        // ---- the list
        Bevel {
            id: well
            anchors.top: bars.bottom
            anchors.topMargin: 2
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: statusBar.top
            anchors.bottomMargin: 2
            style: "sunken"
            face: Theme.window

            readonly property var columns: [
                { title: "Name", sort: ArchiveDocument.SortName, width: Math.max(140, list.width - 120 - 80 - 50 - 80 - pathWidth), right: false },
                { title: "Modified", sort: ArchiveDocument.SortModified, width: 120, right: false },
                { title: "Size", sort: ArchiveDocument.SortSize, width: 80, right: true },
                { title: "Ratio", sort: ArchiveDocument.SortRatio, width: 50, right: true },
                { title: "Packed", sort: ArchiveDocument.SortPacked, width: 80, right: true },
                { title: "Path", sort: ArchiveDocument.SortPath, width: pathWidth, right: false },
            ]
            readonly property real pathWidth: Math.max(100, Math.round(list.width * 0.25))

            Row {
                id: header
                x: 2
                y: 2
                height: 18
                clip: true
                width: parent.width - 4
                Repeater {
                    model: well.columns
                    Item {
                        id: headerCell
                        required property var modelData
                        width: modelData.width
                        height: 18
                        Bevel { anchors.fill: parent; style: headMouse.pressed ? "pressed" : "raised" }
                        Text {
                            x: 4
                            width: parent.width - 8
                            height: parent.height
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: headerCell.modelData.right ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                            text: headerCell.modelData.title
                                  + (doc.sortColumn === headerCell.modelData.sort ? (doc.sortAscending ? " ▲" : " ▼") : "")
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        MouseArea {
                            id: headMouse
                            anchors.fill: parent
                            onClicked: {
                                if (doc.sortColumn === headerCell.modelData.sort)
                                    doc.sortAscending = !doc.sortAscending;
                                else {
                                    doc.sortColumn = headerCell.modelData.sort;
                                    doc.sortAscending = true;
                                }
                            }
                        }
                    }
                }
            }

            ListView {
                id: list
                x: 2
                y: header.y + header.height
                width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
                height: parent.height - y - 2
                clip: true
                focus: true
                interactive: false
                model: doc
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

                function move(to, event) {
                    to = Math.max(0, Math.min(doc.count - 1, to));
                    if (doc.count === 0)
                        return;
                    currentIndex = to;
                    doc.select(to, (event.modifiers & Qt.ShiftModifier) ? ArchiveDocument.Extend : ArchiveDocument.Replace);
                }
                Keys.onPressed: (event) => {
                    var page = Math.max(1, Math.floor(height / 18) - 1);
                    switch (event.key) {
                    case Qt.Key_Down: move(currentIndex + 1, event); break;
                    case Qt.Key_Up: move(currentIndex - 1, event); break;
                    case Qt.Key_PageDown: move(currentIndex + page, event); break;
                    case Qt.Key_PageUp: move(currentIndex - page, event); break;
                    case Qt.Key_Home: move(0, event); break;
                    case Qt.Key_End: move(doc.count - 1, event); break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter: window.viewSelected(); break;
                    case Qt.Key_Menu: {
                        var p = list.mapToItem(popup, 20, 20);
                        popup.popup(p.x, p.y, window.rowMenu());
                        break;
                    }
                    default: return;
                    }
                    event.accepted = true;
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property string name
                    required property string folder
                    required property string modified
                    required property string sizeText
                    required property string packedText
                    required property string ratioText
                    required property bool encrypted
                    required property bool selected
                    required property string kind

                    width: list.width
                    height: 18

                    Rectangle {
                        anchors.fill: parent
                        color: row.selected ? Theme.selection : "transparent"
                    }
                    Loader {
                        anchors.fill: parent
                        active: list.currentIndex === row.index && list.activeFocus
                        sourceComponent: FocusRect95 { color: row.selected ? "#ffff80" : "#000000" }
                    }
                    Row {
                        x: 0
                        height: parent.height
                        Repeater {
                            model: well.columns
                            Item {
                                id: cell
                                required property var modelData
                                required property int index
                                width: modelData.width
                                height: 18
                                ArchiveIcon {
                                    visible: cell.index === 0
                                    x: 3
                                    y: 1
                                    size: 16
                                    name: row.kind
                                }
                                Text {
                                    x: cell.index === 0 ? 23 : 4
                                    width: parent.width - x - 4
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: cell.modelData.right ? Text.AlignRight : Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: [row.name + (row.encrypted ? "+" : ""), row.modified, row.sizeText, row.ratioText,
                                           row.packedText, row.folder][cell.index]
                                    color: row.selected ? Theme.selectionText : Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            list.forceActiveFocus();
                            list.currentIndex = row.index;
                            if (mouse.button === Qt.RightButton) {
                                if (!row.selected)
                                    doc.select(row.index, ArchiveDocument.Replace);
                                return;
                            }
                            var mode = (mouse.modifiers & Qt.ShiftModifier) ? ArchiveDocument.Extend
                                     : (mouse.modifiers & Qt.ControlModifier) ? ArchiveDocument.Toggle : ArchiveDocument.Replace;
                            doc.select(row.index, mode);
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var p = mapToItem(popup, mouse.x, mouse.y);
                                popup.popup(p.x, p.y, window.rowMenu());
                            }
                        }
                        onDoubleClicked: (mouse) => { if (mouse.button === Qt.LeftButton) doc.view(row.index); }
                    }
                }

                // a click below the rows clears the selection
                MouseArea {
                    z: -1
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: { list.forceActiveFocus(); doc.clearSelection(); }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton && doc.loaded) {
                            var p = mapToItem(popup, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, window.rowMenu());
                        }
                    }
                }
                WheelHandler {
                    onWheel: (event) => {
                        list.contentY = Math.max(0, Math.min(list.contentHeight - list.height,
                                                             list.contentY - event.angleDelta.y / 120 * 54));
                    }
                }
            }
            ScrollBar95 {
                id: listBar
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: list.y
                height: list.height
                flickable: list
                stepSize: 18
                visible: needed
            }

            // nothing open: how to start
            Text {
                visible: !doc.loaded || doc.count === 0
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: !doc.loaded ? "Open an archive with Open, or make one with New.\nYou can also drag files here to put them in a new archive."
                                  : "This archive is empty. Add files to it with Add, or drag them here."
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    if (!drop.hasUrls)
                        return;
                    drop.acceptProposedAction();
                    window.dropped(Archives.urlsToPaths(drop.urls));
                }
            }
        }

        // ---- the status bar: what a button does, the selection; the total
        function buttonTip() {
            for (var i = 0; i < toolbar.children.length; ++i) {
                var c = toolbar.children[i];
                if (c.hovered && c.tip)
                    return c.tip;
            }
            return "";
        }
        Item {
            id: statusBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 22
            Bevel {
                x: 2; y: 2
                width: parent.width - totalBox.width - 8
                height: 18
                style: "sunken"
                Text {
                    x: 4
                    width: parent.width - 8
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: main.buttonTip() || (doc.selectionCount > 0 ? doc.selectionText : window.status)
                          || (doc.loaded ? doc.formatName + (doc.tool ? " (with " + (doc.tool === "7z" ? "7-Zip" : doc.tool) + ")" : "")
                                           + (doc.writable ? "" : ", read-only") : "")
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            Bevel {
                id: totalBox
                x: parent.width - width - 18
                y: 2
                width: Math.max(160, totalText.implicitWidth + 12)
                height: 18
                style: "sunken"
                Text {
                    id: totalText
                    x: 4
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: doc.totalText
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            SizeGrip95 {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }
    }

    // ----------------------------------------------------------------- dialogs

    readonly property var archivePatterns: ["*.zip", "*.7z", "*.rar", "*.tar", "*.tar.gz", "*.tgz", "*.tar.bz2", "*.tbz2", "*.tbz",
                                            "*.tar.xz", "*.txz", "*.tar.zst", "*.tzst", "*.gz", "*.bz2", "*.xz", "*.zst",
                                            "*.iso", "*.cab", "*.jar", "*.deb", "*.cpio", "*.lzh", "*.lha"]

    FileDialog95 {
        id: openDialog
        title: "Open Archive"
        messageBox: msgBox
        popupMenu: popup
        filters: [
            { label: "Archives", patterns: window.archivePatterns },
            { label: "All Files (*.*)", patterns: ["*"] },
        ]
        onAccepted: (path) => window.openArchive(path)
        onClosed: if (!msgBox.visible) window.refocus()
    }

    FileDialog95 {
        id: newDialog
        mode: "save"
        title: "New Archive"
        acceptLabel: "OK"
        messageBox: msgBox
        popupMenu: popup
        filters: [
            { label: "ZIP archive (*.zip)", patterns: ["*.zip"], suffix: "zip" },
            { label: "7-Zip archive (*.7z)", patterns: ["*.7z"], suffix: "7z" },
            { label: "Tar, gzip (*.tar.gz)", patterns: ["*.tar.gz", "*.tgz"], suffix: "tar.gz" },
            { label: "Tar, bzip2 (*.tar.bz2)", patterns: ["*.tar.bz2", "*.tbz2"], suffix: "tar.bz2" },
            { label: "Tar, xz (*.tar.xz)", patterns: ["*.tar.xz", "*.txz"], suffix: "tar.xz" },
            { label: "Tar, Zstandard (*.tar.zst)", patterns: ["*.tar.zst", "*.tzst"], suffix: "tar.zst" },
            { label: "Tar (*.tar)", patterns: ["*.tar"], suffix: "tar" },
        ]
        defaultSuffix: "zip"
        onFilterIndexChanged: defaultSuffix = filters[filterIndex].suffix
        // (a name that's there opens that archive, so no "replace it?")
        onAccepted: (path) => window.createArchive(path)
        onClosed: if (!msgBox.visible && !addDialog.visible && !addFilesDialog.visible) window.refocus()
    }

    FileDialog95 {
        id: addFilesDialog
        title: "Add Files"
        acceptLabel: "Add"
        multiple: true
        messageBox: msgBox
        popupMenu: popup
        filters: [{ label: "All Files (*.*)", patterns: ["*"] }]
        onAcceptedPaths: (paths) => {
            Archives.setSetting("addFolder", Win95Fs.parentDir(paths[0]));
            window.addFiles(paths);
        }
        onClosed: if (!msgBox.visible && !addDialog.visible) window.refocus()
    }

    FileDialog95 {
        id: addFolderDialog
        mode: "folder"
        title: "Add Folder"
        acceptLabel: "Add"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => {
            Archives.setSetting("addFolder", Win95Fs.parentDir(path));
            window.addFiles([path]);
        }
        onClosed: if (!msgBox.visible && !addDialog.visible) window.refocus()
    }

    // Extract's Browse...
    FileDialog95 {
        id: folderDialog
        mode: "folder"
        title: "Extract To"
        messageBox: msgBox
        popupMenu: popup
        onClosed: if (extractDialog.visible) extractDialog.forceActiveFocus()
    }

    // the Add box's Browse...
    FileDialog95 {
        id: saveDialog
        mode: "save"
        title: "Archive"
        acceptLabel: "OK"
        messageBox: msgBox
        popupMenu: popup
        filters: newDialog.filters
        defaultSuffix: "zip"
        onFilterIndexChanged: defaultSuffix = filters[filterIndex].suffix
        onClosed: if (addDialog.visible) addDialog.forceActiveFocus()
    }

    ExtractDialog {
        id: extractDialog
        folderDialog: folderDialog
        onAccepted: (dest, selectedOnly, useFolders, openAfter) => {
            window.openWhenDone = openAfter ? dest : "";
            window.log("extract to " + dest + (selectedOnly ? " selected" : " all"));
            doc.extract(dest, selectedOnly, useFolders);
        }
        onClosed: if (!doc.busy) window.refocus()
    }

    AddDialog {
        id: addDialog
        embedded: window.compact
        saveDialog: saveDialog
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (archive, format, level, password) => {
            if (window.compact) {
                cli.add(archive, format, level, password);
                return;
            }
            window.log("add " + files.length + " level=" + level);
            doc.add(files, format, level, password);
        }
        onClosed: if (window.compact && !doc.busy && !cli.started) Qt.quit(); else window.refocus()
    }

    PasswordDialog {
        id: passwordDialog
        embedded: window.compact
        onEntered: (password) => doc.retry(password)
        onCancelled: {
            if (window.compact)
                cli.next();
            else {
                window.afterOpen = null;
                window.refocus();
            }
        }
    }

    // the job's progress: after a moment (quick ones don't flash it up)
    ProgressDialog {
        id: progress
        embedded: window.compact
        heading: doc.operation
        file: doc.currentFile
        percent: doc.percent
        onCancelRequested: if (doc.busy) doc.cancel()
    }
    Timer {
        id: progressDelay
        interval: window.compact ? 0 : 400
        onTriggered: if (doc.busy && !progress.visible) progress.show()
    }
    Connections {
        target: doc
        function onBusyChanged() {
            if (doc.busy)
                progressDelay.restart();
            else {
                progressDelay.stop();
                if (progress.visible)
                    progress.close();
            }
        }
    }

    Dialog95 {
        id: formatsDialog
        title: "Formats and Programs"
        dialogWidth: 480
        dialogHeight: formatsText.implicitHeight + 70
        onClosed: window.refocus()
        Text {
            id: formatsText
            x: 12
            y: 12
            width: parent.width - 24
            wrapMode: Text.Wrap
            text: formatsDialog.visible ? Archives.formatsText() : ""
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: formatsDialog.close()
        }
        Keys.onReturnPressed: formatsDialog.close()
    }

    // ------------------------------------------------- the command-line jobs

    QtObject {
        id: cli
        property var queue: []
        property string current: ""
        property bool started: false

        // extract: the next archive (or quit)
        function next() {
            if (cliMode !== "extract" || queue.length === 0) {
                window.log("done");
                Qt.quit();
                return;
            }
            window.boxWidth = progress.dialogWidth;
            window.boxHeight = progress.dialogHeight;
            current = queue[0];
            queue = queue.slice(1);
            doc.close();
            window.afterOpen = function () {
                if (doc.path !== cli.current)
                    return cli.next();
                var dir = Win95Fs.parentDir(cli.current);
                var dest = cliDest === ":here" ? dir
                         : cliDest === ":subfolder" ? Win95Fs.resolve(dir, Archives.baseName(Win95Fs.fileName(cli.current)))
                         : cliDest;
                window.log("extract " + cli.current + " to " + dest);
                doc.extract(dest, false, true);
            };
            doc.open(current);
        }

        // add: into the archive (opened if it's there), then quit
        function add(archive, format, level, password) {
            started = true;
            window.boxWidth = progress.dialogWidth;
            window.boxHeight = progress.dialogHeight;
            var go = function () {
                if (!doc.loaded)
                    return Qt.quit();
                if (!doc.writable)
                    return window.error("Add", doc.readOnlyReason, Qt.quit);
                window.log("add " + cliFiles.length + " to " + archive);
                doc.add(cliFiles, format, level, password);
            };
            if (Archives.exists(archive)) {
                window.afterOpen = go;
                doc.open(archive);
            } else {
                doc.create(archive, format);
                go();
            }
        }

        function start() {
            if (cliMode === "extract") {
                queue = cliArchives;
                next();
            } else if (cliMode === "add") {
                var fmt = Archives.formatForName(Win95Fs.fileName(initialArchive));
                if (!fmt)
                    return window.error("Add", "Name the archive .zip, .7z, .tar.gz, .tar.bz2, .tar.xz, .tar.zst or .tar.", Qt.quit);
                add(initialArchive, fmt, Number(Archives.setting("level", "6")), "");
            } else if (cliMode === "addDialog") {
                var first = cliFiles[0];
                var dir = Win95Fs.parentDir(first);
                var base = cliFiles.length === 1 ? Archives.baseName(Win95Fs.fileName(first)) : Win95Fs.fileName(dir);
                var fmt2 = Archives.setting("format", "zip");
                if (Archives.isSingleFile(fmt2))
                    fmt2 = "zip";
                addDialog.askArchive = true;
                window.boxWidth = addDialog.dialogWidth;
                window.boxHeight = addDialog.dialogHeight;
                addDialog.newArchive = true;
                addDialog.open(cliFiles, Win95Fs.resolve(dir, base + "." + fmt2));
            }
        }
    }

    // a size remembered from before that's too small for the window
    Timer {
        running: !window.compact
        interval: 300
        onTriggered: {
            if (window.width < window.minimumWidth || window.height < window.minimumHeight) {
                window.width = 720;
                window.height = 460;
            }
        }
    }

    Component.onCompleted: {
        if (compact) {
            cli.start();
            return;
        }
        if (initialArchive)
            openArchive(initialArchive);
        list.forceActiveFocus();
    }

    // (for the tests: W95_ARCHIVE_GRAB)
    Timer {
        running: testGrab !== ""
        interval: 1500
        onTriggered: window.contentItem.grabToImage(function (r) { r.saveToFile(testGrab); window.log("grabbed " + testGrab); })
    }

    // window-level overlays (last, so they stack on top)
    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen) window.refocus()
    }
    MessageBox95 { id: msgBox }
}
