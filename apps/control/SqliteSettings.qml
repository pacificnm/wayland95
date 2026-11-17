import QtQuick
import Win95Ui
import Win95Control

// SQLite's settings: DB Browser for SQLite as an extra, and your database
// files: those in a folder (~/Databases, or one you choose), what's in each
// (looked at read-only), a new one, opening one in sqlite3 or DB Browser,
// and moving one to the Recycle Bin.
PropertySheet {
    id: sheet

    title: "SQLite Properties"
    titles: ["General", "Databases"]

    readonly property var tool: Services.services.filter(function (s) { return s.id === "sqlite"; })[0] || ({})
    property bool browserInstalled: false
    property bool wantBrowser: false
    property bool startedHere: false
    property string folder: Services.exists(Services.homePath + "/Databases") ? Services.homePath + "/Databases" : Services.homePath
    property var files: []
    property var info: ({})

    function loadComponents() {
        Services.installedPackages(["sqlitebrowser"], function (names) {
            sheet.browserInstalled = names.indexOf("sqlitebrowser") >= 0;
            sheet.wantBrowser = sheet.browserInstalled;
            sheet.dirty = false;
        });
    }
    function loadFiles() {
        var chosen = fileList.selectedKey;
        files = Services.sqliteFiles(folder).map(function (f) {
            return { key: f.path, name: f.name, path: f.path, size: f.size };
        });
        if (!files.some(function (f) { return f.key === chosen; }))
            fileList.selectedKey = files.length ? files[0].key : "";
        loadInfo();
    }
    // (by the key: `selected` may not have caught up yet when it changes)
    function loadInfo() {
        var key = fileList.selectedKey;
        info = files.some(function (f) { return f.key === key; }) ? Services.sqliteInfo(key) : ({});
    }
    Component.onCompleted: {
        loadComponents();
        loadFiles();
    }

    onApplyRequested: {
        if (wantBrowser === browserInstalled)
            return;
        startedHere = true;
        Services.changePackages(wantBrowser ? "Installing DB Browser for SQLite" : "Removing DB Browser for SQLite",
                                wantBrowser ? ["sqlitebrowser"] : [], wantBrowser ? [] : ["sqlitebrowser"]);
    }
    Connections {
        target: Services
        function onTaskFinished(ok, message) {
            if (!ok && sheet.startedHere)
                sheet.messageBox.show("SQLite", message, "error", ["OK"]);
            sheet.startedHere = false;
            sheet.loadComponents();
        }
    }

    function sizeText(n) {
        if (n < 1024) return n + " bytes";
        if (n < 1024 * 1024) return Math.round(n / 1024) + " KB";
        if (n < 1024 * 1024 * 1024) return (n / 1048576).toFixed(1) + " MB";
        return (n / 1073741824).toFixed(2) + " GB";
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "sqlite" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: sheet.tool.installed
                  ? "SQLite " + (sheet.tool.version || "") + " is installed, as <b>sqlite3</b>."
                  : "The <b>sqlite3</b> command isn't installed (turn SQLite on in Services)."
        }
        Label {
            x: 46
            y: 22
            width: parent.width - x
            text: "An SQLite database is a single file: there's no server to start, and a program that "
                  + "uses one just opens the file. Copy the file and you've copied the database."
        }
        GroupBox95 {
            id: extrasBox
            y: 86
            width: parent.width
            height: 72
            title: "Extras"
            CheckBox95 {
                text: "<u>D</u>B Browser for SQLite: a window for looking at and editing databases"
                checked: sheet.wantBrowser
                onToggled: (on) => { sheet.wantBrowser = on; sheet.dirty = sheet.wantBrowser !== sheet.browserInstalled; }
            }
            Label {
                y: 24
                width: parent.width
                color: Theme.shadow
                text: Services.busy && sheet.startedHere ? Services.status : "Click Apply to install or remove it."
            }
        }
        Label {
            y: extrasBox.y + extrasBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: "Programs written in Python, Node.js and most other languages can use SQLite without "
                  + "installing anything else."
        }
    }

    // ---------------------------------------------------------- Databases
    Item {
        Label { y: 3; text: "<u>L</u>ook in:" }
        TextField95 {
            id: folderField
            x: 56
            width: parent.width - x - 80
            text: sheet.folder
            onAccepted: {
                if (Services.exists(text)) { sheet.folder = text; sheet.loadFiles(); }
                else sheet.messageBox.show("SQLite", "There's no folder " + text + ".", "error", ["OK"]);
            }
        }
        Button95 {
            anchors.right: parent.right
            width: 74
            text: "<u>B</u>rowse..."
            onClicked: folderDialog.openAt(sheet.folder, "")
        }
        ProgramList {
            id: fileList
            objectName: "sqliteFiles"
            y: 28
            width: parent.width
            height: 100
            model: sheet.files
            iconComponent: AppletIcon { size: parent.size; kind: "database" }
            noteFor: function (item) { return sheet.sizeText(item.size); }
            emptyText: "No databases in this folder."
            onSelectedKeyChanged: sheet.loadInfo()
            onActivated: (item) => {
                if (sheet.tool.installed)
                    Services.openTerminal(sheet.folder, ["sqlite3", item.path]);
            }
        }
        Label {
            id: tablesLabel
            y: fileList.y + fileList.height + 6
            width: parent.width
            text: !fileList.selected ? "" : sheet.info.error ? "It couldn't be read: " + sheet.info.error
                  : (sheet.info.tables || []).length === 0 ? "<b>" + fileList.selected.name + "</b> is empty: no tables yet."
                  : "In <b>" + fileList.selected.name + "</b>:"
        }
        ProgramList {
            id: tableList
            y: tablesLabel.y + 18
            width: parent.width
            height: 64
            visible: (sheet.info.tables || []).length > 0
            model: (sheet.info.tables || []).map(function (t) { return { key: t.name, name: t.name, type: t.type, rows: t.rows }; })
            noteFor: function (item) {
                return item.type === "view" ? "view" : item.rows === 1 ? "1 row" : item.rows < 0 ? "" : item.rows + " rows";
            }
        }
        Row {
            id: fileButtons
            y: tableList.y + tableList.height + 8
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>N</u>ew..."
                onClicked: newDialog.open()
            }
            Button95 {
                width: 100
                text: "Open in <u>s</u>qlite3"
                enabled: !!fileList.selected && !!sheet.tool.installed
                onClicked: Services.openTerminal(sheet.folder, ["sqlite3", fileList.selected.path])
            }
            Button95 {
                width: 110
                visible: sheet.browserInstalled
                text: "Open in DB <u>B</u>rowser"
                enabled: !!fileList.selected
                onClicked: Services.run("sqlitebrowser", [fileList.selected.path], "", function () {})
            }
            Button95 {
                width: 120
                text: "Move to <u>R</u>ecycle Bin"
                enabled: !!fileList.selected
                onClicked: {
                    var f = fileList.selected;
                    sheet.messageBox.show("SQLite", "Move the database \"" + f.name + "\" to the Recycle Bin?\n\n"
                                          + "Programs that use it won't find it there.", "question", ["Yes", "No"], function (b) {
                        if (b !== 0)
                            return;
                        if (!Services.moveToTrash(f.path))
                            sheet.messageBox.show("SQLite", f.name + " couldn't be moved to the Recycle Bin.", "error", ["OK"]);
                        sheet.loadFiles();
                    });
                }
            }
        }
    }

    extras: [
        Dialog95 {
            id: newDialog
            title: "New Database"
            dialogWidth: 320
            dialogHeight: 112

            function open() {
                nameField.text = "";
                show();
                nameField.forceActiveFocus();
            }
            function ok() {
                var name = nameField.text.trim();
                if (!/^[^\/\0]+$/.test(name) || name === "." || name === "..") {
                    sheet.messageBox.show("SQLite", "A file name can't have / in it.", "error", ["OK"],
                                          function () { nameField.forceActiveFocus(); });
                    return;
                }
                if (!/\.[A-Za-z0-9]+$/.test(name))
                    name += ".db";
                var error = Services.sqliteCreate(sheet.folder + "/" + name);
                if (error) {
                    sheet.messageBox.show("SQLite", error, "error", ["OK"], function () { nameField.forceActiveFocus(); });
                    return;
                }
                newDialog.close();
                sheet.loadFiles();
                fileList.selectedKey = sheet.folder + "/" + name;
            }
            Text {
                x: 12
                y: 16
                text: "<u>N</u>ame:"
                textFormat: Text.StyledText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: nameField
                objectName: "sqliteName"
                x: 60
                y: 12
                width: 246
                onAccepted: newDialog.ok()
            }
            Text {
                x: 60
                y: 40
                text: "(.db is added if it has no extension)"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                spacing: 6
                Button95 {
                    text: "OK"
                    isDefault: true
                    enabled: nameField.text.trim().length > 0
                    onClicked: newDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: newDialog.close()
                }
            }
        },
        FileDialog95 {
            id: folderDialog
            mode: "folder"
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            onAccepted: (path) => { sheet.folder = path; sheet.loadFiles(); }
        }
    ]
}
