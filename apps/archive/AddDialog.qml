import QtQuick
import Win95Ui

// Add: the files going in, and how - which archive (when it isn't the one
// open), its format (a new one's), how hard to compress, and a password
// (zip and 7z). Accepted with the archive's path, format, level 0-9 and
// password ("" for none).
Dialog95 {
    id: dlg

    property var files: []
    // show the Archive box (and its format): adding to a new or other archive
    property bool askArchive: false
    // the archive is new: its format can be chosen
    property bool newArchive: true
    property string existingFormat: ""
    property FileDialog95 saveDialog
    property MessageBox95 messageBox
    property Menu95 popupMenu
    signal accepted(string archive, string format, int level, string password)

    title: "Add"
    dialogWidth: 440
    dialogHeight: askArchive ? 268 : 214

    readonly property var formats: [
        { key: "zip", label: "ZIP (.zip)" },
        { key: "7z", label: "7-Zip (.7z)" },
        { key: "tar.gz", label: "Tar, gzip (.tar.gz)" },
        { key: "tar.bz2", label: "Tar, bzip2 (.tar.bz2)" },
        { key: "tar.xz", label: "Tar, xz (.tar.xz)" },
        { key: "tar.zst", label: "Tar, Zstandard (.tar.zst)" },
        { key: "tar", label: "Tar, not compressed (.tar)" },
        { key: "gz", label: "gzip, one file (.gz)" },
        { key: "bz2", label: "bzip2, one file (.bz2)" },
        { key: "xz", label: "xz, one file (.xz)" },
        { key: "zst", label: "Zstandard, one file (.zst)" },
    ]
    readonly property var levels: [0, 1, 6, 9]
    readonly property string format: newArchive ? formats[formatBox.currentIndex].key : existingFormat
    readonly property bool canEncrypt: format === "zip" || format === "7z"

    // archive: its path (askArchive), or "" for the open one
    function open(fileList, archive) {
        files = fileList;
        var fmt = archive ? (Archives.formatForName(Win95Fs.fileName(archive)) || Archives.setting("format", "zip")) : existingFormat;
        if (newArchive) {
            var i = formats.findIndex(function (f) { return f.key === fmt; });
            formatBox.currentIndex = i >= 0 ? i : 0;
        }
        archiveField.text = archive || "";
        var lv = levels.indexOf(Number(Archives.setting("level", "6")));
        levelBox.currentIndex = lv >= 0 ? lv : 2;
        passwordBox.checked = false;
        passwordField.text = "";
        confirmField.text = "";
        show();
        if (askArchive) {
            archiveField.forceActiveFocus();
            archiveField.input.selectAll();
        } else {
            levelBox.forceActiveFocus();
        }
    }

    // the archive's name with the chosen format's suffix
    function renamed(path, fmt) {
        if (!path)
            return path;
        var dir = Win95Fs.parentDir(path);
        var name = Archives.baseName(Win95Fs.fileName(path));
        return Win95Fs.resolve(dir, name + "." + fmt);
    }

    function fail(msg, focusItem) {
        if (messageBox)
            messageBox.show("Add", msg, "warning", ["OK"], function () { focusItem.forceActiveFocus(); });
    }

    function accept() {
        var archive = archiveField.text.trim();
        if (askArchive && !archive)
            return fail("Type a name for the archive.", archiveField);
        if (Archives.isSingleFile(format) && (files.length !== 1 || !Win95Fs.isFile(files[0])))
            return fail("A ." + format + " file holds just one file. Choose ZIP or a Tar format to put "
                        + "several files or a folder together.", formatBox);
        var pw = "";
        if (canEncrypt && passwordBox.checked) {
            pw = passwordField.text;
            if (!pw)
                return fail("Type a password, or clear \"Protect with a password\".", passwordField);
            if (pw !== confirmField.text)
                return fail("The two passwords are different. Type them again.", passwordField);
        }
        if (newArchive)
            Archives.setSetting("format", format);
        Archives.setSetting("level", String(levels[levelBox.currentIndex]));
        // (said before it closes: a --add-dialog run quits when it closes unanswered)
        accepted(archive, format, levels[levelBox.currentIndex], pw);
        close();
    }

    component Label: Text {
        color: Theme.text
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        height: 21
        verticalAlignment: Text.AlignVCenter
    }

    Column {
        x: 12
        y: 10
        width: parent.width - 24
        spacing: 6

        Label {
            width: parent.width
            elide: Text.ElideRight
            text: dlg.files.length === 1 ? "Adding " + Win95Fs.fileName(dlg.files[0])
                                         : "Adding " + dlg.files.length + " files and folders"
        }

        Row {
            visible: dlg.askArchive
            spacing: 6
            Label { width: 90; text: "A<u>r</u>chive:" }
            TextField95 {
                id: archiveField
                width: dlg.dialogWidth - 24 - 96 - 94
                onAccepted: dlg.accept()
                onEscaped: dlg.close()
            }
            Button95 {
                width: 82
                text: "Browse..."
                onClicked: if (dlg.saveDialog) dlg.saveDialog.openAt(Win95Fs.parentDir(archiveField.text) || Win95Fs.homePath(),
                                                                     Win95Fs.fileName(archiveField.text))
            }
        }
        Row {
            visible: dlg.askArchive && dlg.newArchive
            spacing: 6
            Label { width: 90; text: "<u>F</u>ormat:" }
            ComboBox95 {
                id: formatBox
                width: 220
                popupMenu: dlg.popupMenu
                model: dlg.formats.map(function (f) { return f.label; })
                onActivated: (i) => { archiveField.text = dlg.renamed(archiveField.text, dlg.formats[i].key); }
            }
        }
        Row {
            spacing: 6
            Label { width: 90; text: "<u>C</u>ompression:" }
            ComboBox95 {
                id: levelBox
                width: 220
                popupMenu: dlg.popupMenu
                model: ["None (store only)", "Fastest", "Normal", "Maximum (slowest)"]
                currentIndex: 2
            }
        }
        CheckBox95 {
            id: passwordBox
            enabled: dlg.canEncrypt
            text: dlg.canEncrypt ? "<u>P</u>rotect with a password" : "Protect with a password (ZIP and 7-Zip only)"
        }
        Row {
            spacing: 6
            enabled: dlg.canEncrypt && passwordBox.checked
            opacity: enabled ? 1 : 0.5
            Label { width: 90; text: "Password:" }
            TextField95 {
                id: passwordField
                width: 130
                input.echoMode: TextInput.Password
                onAccepted: dlg.accept()
            }
            Label { text: "Again:" }
            TextField95 {
                id: confirmField
                width: 110
                input.echoMode: TextInput.Password
                onAccepted: dlg.accept()
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "Add"; isDefault: true; onClicked: dlg.accept() }
        Button95 { text: "Cancel"; onClicked: dlg.close() }
    }
    Keys.onReturnPressed: dlg.accept()
    Keys.onEnterPressed: dlg.accept()

    Connections {
        target: dlg.saveDialog
        enabled: dlg.visible
        function onAccepted(path) {
            archiveField.text = path;
            var f = Archives.formatForName(Win95Fs.fileName(path));
            var i = dlg.formats.findIndex(function (x) { return x.key === f; });
            if (i >= 0)
                formatBox.currentIndex = i;
            archiveField.forceActiveFocus();
        }
    }
}
