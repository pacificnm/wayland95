import QtQuick
import Qt.labs.folderlistmodel

// The Win95 common Open / Save As dialog: "Look in" folder with an Up
// button, a file list, "File name" and "Files of type". Typing a folder
// path into File name and pressing Enter goes there.
//
// mode "folder" chooses a folder (only folders are listed; the button
// takes the selected one, or the one being looked in). `multiple` lets
// Open take several files (Ctrl+click and Shift+click), answered with
// acceptedPaths.
//
//   FileDialog95 {
//       mode: "save"
//       messageBox: msgBox      // a MessageBox95 for errors/confirmations
//       popupMenu: popup        // a Menu95 for the Files of type list
//       onAccepted: (path) => ...
//   }
//   fileDialog.openAt(folder, suggestedName)
Dialog95 {
    id: fd

    property string mode: "open" // or "save", "folder"
    property bool multiple: false
    // the button's text ("Open", "Save" or "Select Folder" otherwise)
    property string acceptLabel: ""
    // with multiple: the files picked by Ctrl/Shift+click (file names)
    property var picked: []
    property string folder: Win95Fs.homePath()
    property var filters: [
        { label: "Text Documents (*.txt)", patterns: ["*.txt"] },
        { label: "All Files (*.*)", patterns: ["*"] },
    ]
    property int filterIndex: 0
    property string defaultSuffix: "txt"
    property MessageBox95 messageBox
    property Menu95 popupMenu

    signal accepted(string path)
    // every path chosen (one, unless multiple)
    signal acceptedPaths(var paths)

    title: mode === "save" ? "Save As" : mode === "folder" ? "Select Folder" : "Open"
    dialogWidth: 440
    dialogHeight: 310

    function openAt(dir, name) {
        if (dir && Win95Fs.isDir(dir))
            folder = dir;
        nameField.text = name || "";
        picked = [];
        show();
        nameField.forceActiveFocus();
        nameField.input.selectAll();
    }

    function refocus() {
        nameField.forceActiveFocus();
        nameField.input.selectAll();
    }

    function goUp() {
        var parent = Win95Fs.parentDir(folder);
        if (parent !== folder)
            folder = parent;
    }

    function finish(path) {
        close();
        accepted(path);
        acceptedPaths([path]);
    }

    function accept() {
        var name = nameField.text.trim();
        if (mode === "folder") {
            // the folder named or selected, or the one being looked in
            var dir = name.length ? Win95Fs.resolve(folder, name) : folder;
            if (!Win95Fs.isDir(dir)) {
                messageBox.show(title, dir + "\nThis folder does not exist.", "warning", ["OK"], fd.refocus);
                return;
            }
            finish(dir);
            return;
        }
        // several files: "a.txt" "b.txt"
        if (multiple && name.charAt(0) === '"') {
            var names = (name.match(/"[^"]+"/g) || []).map(function (q) { return q.slice(1, -1); });
            var paths = names.map(function (n) { return Win95Fs.resolve(fd.folder, n); });
            var missing = paths.filter(function (p) { return !Win95Fs.isFile(p); });
            if (missing.length) {
                messageBox.show(title, Win95Fs.fileName(missing[0]) + "\nFile not found.\nPlease verify the correct file name was given.",
                                "warning", ["OK"], fd.refocus);
                return;
            }
            if (paths.length) {
                close();
                accepted(paths[0]);
                acceptedPaths(paths);
            }
            return;
        }
        if (name.length === 0)
            return;
        var path = Win95Fs.resolve(folder, name);

        if (Win95Fs.isDir(path)) {
            folder = path;
            nameField.text = "";
            return;
        }

        if (mode === "open") {
            if (!Win95Fs.isFile(path) && Win95Fs.suffix(path) === "" && defaultSuffix
                    && Win95Fs.isFile(path + "." + defaultSuffix))
                path += "." + defaultSuffix;
            if (!Win95Fs.isFile(path)) {
                messageBox.show(title, Win95Fs.fileName(path)
                                + "\nFile not found.\nPlease verify the correct file name was given.",
                                "warning", ["OK"], fd.refocus);
                return;
            }
            finish(path);
            return;
        }

        // save
        if (Win95Fs.suffix(path) === "" && defaultSuffix && filters[filterIndex].patterns[0] !== "*")
            path += "." + defaultSuffix;
        if (!Win95Fs.isDir(Win95Fs.parentDir(path))) {
            messageBox.show(title, Win95Fs.parentDir(path) + "\nThis folder does not exist.",
                            "warning", ["OK"], fd.refocus);
            return;
        }
        if (Win95Fs.exists(path)) {
            messageBox.show(title, path + " already exists.\nDo you want to replace it?",
                            "warning", ["Yes", "No"], function (i) {
                                if (i === 0)
                                    fd.finish(path);
                                else
                                    fd.refocus();
                            });
            return;
        }
        finish(path);
    }

    function activateRow(row) {
        if (row < 0 || row >= folderModel.count)
            return;
        if (folderModel.get(row, "fileIsDir")) {
            folder = folderModel.get(row, "filePath");
            nameField.text = "";
            picked = [];
        } else {
            nameField.text = folderModel.get(row, "fileName");
            accept();
        }
    }

    FolderListModel {
        id: folderModel
        folder: Win95Fs.toUrl(fd.folder)
        nameFilters: fd.filters[fd.filterIndex].patterns
        showDirsFirst: true
        showFiles: fd.mode !== "folder"
        showDotAndDotDot: false
        showHidden: false
        caseSensitive: false
        sortCaseSensitive: false
    }

    // ---- Look in ----------------------------------------------------------
    Text {
        id: lookInLabel
        x: 10
        y: 12
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        text: "Look <u>i</u>n:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    Bevel {
        id: lookIn
        x: 80
        y: 12
        width: parent.width - x - 44
        height: 21
        style: "sunken"
        face: Theme.window
        Row {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            SmallIcon { isDir: true; anchors.verticalCenter: parent.verticalCenter }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: Win95Fs.displayName(fd.folder)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }
    ToolButton95 {
        anchors.left: lookIn.right
        anchors.leftMargin: 6
        y: 12
        width: 28
        height: 21
        glyph: "↑"
        enabled: Win95Fs.parentDir(fd.folder) !== fd.folder
        onClicked: fd.goUp()
    }

    // ---- file list ---------------------------------------------------------
    Bevel {
        id: listBox
        x: 10
        y: 42
        width: parent.width - 20
        height: parent.height - y - 74
        style: "sunken"
        face: Theme.window

        ListView {
            id: list
            x: 2
            y: 2
            width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            model: folderModel
            currentIndex: -1
            activeFocusOnTab: true
            KeyNavigation.tab: nameField

            onCurrentIndexChanged: {
                if (currentIndex < 0)
                    return;
                const isDir = folderModel.get(currentIndex, "fileIsDir");
                if (fd.mode === "folder" ? isDir : !isDir)
                    nameField.text = folderModel.get(currentIndex, "fileName");
            }
            Keys.onReturnPressed: fd.activateRow(currentIndex)
            Keys.onEnterPressed: fd.activateRow(currentIndex)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Backspace) {
                    fd.goUp();
                    event.accepted = true;
                }
            }

            delegate: Item {
                id: entry
                required property int index
                required property string fileName
                required property bool fileIsDir
                readonly property bool current: list.currentIndex === index || fd.picked.indexOf(fileName) >= 0
                width: list.width
                height: 18

                SmallIcon {
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    isDir: entry.fileIsDir
                }
                Rectangle {
                    x: 22
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(entryText.implicitWidth + 4, entry.width - x - 2)
                    height: 15
                    color: entry.current ? Theme.selection : "transparent"
                    Text {
                        id: entryText
                        x: 2
                        width: parent.width - 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.fileName
                        elide: Text.ElideRight
                        color: entry.current ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    onPressed: (mouse) => {
                        list.forceActiveFocus();
                        // several files: Ctrl+click adds or removes one,
                        // Shift+click takes a run of them
                        if (fd.multiple && !entry.fileIsDir && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                            var p = fd.picked.slice();
                            if (p.length === 0 && list.currentIndex >= 0 && !folderModel.get(list.currentIndex, "fileIsDir"))
                                p.push(folderModel.get(list.currentIndex, "fileName"));
                            if (mouse.modifiers & Qt.ShiftModifier) {
                                var from = Math.min(list.currentIndex < 0 ? entry.index : list.currentIndex, entry.index);
                                var to = Math.max(list.currentIndex < 0 ? entry.index : list.currentIndex, entry.index);
                                p = [];
                                for (var r = from; r <= to; ++r)
                                    if (!folderModel.get(r, "fileIsDir"))
                                        p.push(folderModel.get(r, "fileName"));
                            } else {
                                var at = p.indexOf(entry.fileName);
                                if (at >= 0) p.splice(at, 1); else p.push(entry.fileName);
                            }
                            fd.picked = p;
                            nameField.text = p.length === 1 ? p[0] : p.map(function (n) { return '"' + n + '"'; }).join(" ");
                            return;
                        }
                        fd.picked = [];
                        list.currentIndex = entry.index;
                    }
                    onDoubleClicked: fd.activateRow(entry.index)
                }
            }

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => listBar.scrollBy(-event.angleDelta.y / 120 * 54)
            }
        }

        ScrollBar95 {
            id: listBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: list
            stepSize: 18
        }
    }

    // ---- File name / Files of type ----------------------------------------
    Text {
        x: 10
        y: nameField.y
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        text: fd.mode === "folder" ? "F<u>o</u>lder:" : "File <u>n</u>ame:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    TextField95 {
        id: nameField
        x: 100
        y: listBox.y + listBox.height + 10
        width: parent.width - x - (fd.mode === "folder" ? 120 : 100)
        focus: true
        onAccepted: fd.accept()
        onEscaped: fd.close()
        KeyNavigation.tab: typeCombo
    }
    Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: nameField.y - 1
        text: fd.acceptLabel || (fd.mode === "save" ? "Save" : fd.mode === "folder" ? "Select Folder" : "Open")
        isDefault: true
        onClicked: fd.accept()
    }

    Text {
        visible: fd.mode !== "folder"
        x: 10
        y: typeCombo.y
        height: 21
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.StyledText
        text: fd.mode === "save" ? "Save as <u>t</u>ype:" : "Files of <u>t</u>ype:"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    ComboBox95 {
        id: typeCombo
        visible: fd.mode !== "folder"
        x: 100
        y: nameField.y + 30
        width: nameField.width
        popupMenu: fd.popupMenu
        model: fd.filters.map(function (f) { return f.label; })
        currentIndex: fd.filterIndex
        onActivated: (i) => fd.filterIndex = i
        KeyNavigation.tab: list
    }
    Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: typeCombo.y - 1
        text: "Cancel"
        onClicked: fd.close()
    }

    component SmallIcon: Item {
        property bool isDir: false
        width: 16
        height: 16
        // folder
        Rectangle { visible: parent.isDir; x: 1; y: 3; width: 7; height: 3; color: "#e8c048"; border.color: "#806000" }
        Rectangle { visible: parent.isDir; x: 1; y: 5; width: 14; height: 10; color: "#f8d878"; border.color: "#806000" }
        // document
        Rectangle {
            visible: !parent.isDir
            x: 3; y: 1; width: 11; height: 14
            color: "#ffffff"
            border.color: "#000000"
            Column {
                x: 2; y: 3; spacing: 2
                Repeater { model: 4; Rectangle { width: 6; height: 1; color: "#000080" } }
            }
        }
    }
}
