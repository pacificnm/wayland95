import QtQuick
import Win95Ui
import Win95Control

// Backup: which folders to back up, to which drive (one you plug in), how
// many earlier backups to keep, and when (cron, and when the drive is
// plugged in). Back Up Now runs it here, with its progress. The work is
// wayland95-backup's (session/wayland95-backup): rsync, a dated folder per
// backup sharing unchanged files with the one before.
PropertySheet {
    id: sheet

    title: "Backup Properties"
    titles: ["Backup", "Schedule"]
    width: 480
    height: 500

    // the settings being edited (Apply/OK saves them)
    property var s: ({})
    readonly property var days: ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]

    Component.onCompleted: {
        Backup.reload();
        s = JSON.parse(JSON.stringify(Backup.settings));
        // (the text boxes edit s from here on)
        timeField.text = s.time || "21:00";
        destField.text = s.destDir || Backup.defaultDestDir;
        excludeField.text = (s.excludes || []).join("; ");
        // the first time: the drive plugged in, if there's just one
        if (!s.driveUuid && removable.length === 1)
            setDrive(removable[0]);
        dirty = !Backup.settings.configured;
    }

    function change(key, value) {
        var copy = Object.assign({}, s);
        copy[key] = value;
        s = copy;
        dirty = true;
    }
    function setDrive(d) {
        var copy = Object.assign({}, s);
        copy.driveUuid = d.uuid;
        copy.driveName = d.name;
        s = copy;
        dirty = true;
    }

    onApplyRequested: {
        var err = Backup.save(s);
        if (err)
            messageBox.show("Backup", err, "error", ["OK"]);
    }

    // the drives it can back up to (ones you plug in)
    readonly property var removable: Drives.drives.filter(function (d) { return d.removable && !d.encrypted && d.uuid; })
    // (the drives arrive a moment after the window opens)
    onRemovableChanged: if (!s.driveUuid && removable.length === 1) setDrive(removable[0])
    readonly property var chosenDrive: {
        for (var i = 0; i < removable.length; ++i)
            if (removable[i].uuid === s.driveUuid)
                return removable[i];
        return null;
    }
    readonly property var driveChoices: {
        var list = removable.map(function (d) { return d.name + (d.sizeText ? " (" + d.sizeText + ")" : ""); });
        if (s.driveUuid && !chosenDrive)
            list.push((s.driveName || "The backup drive") + " (not plugged in)");
        return list;
    }

    function when(ms) {
        var d = new Date(ms), today = new Date();
        var day = d.toDateString() === today.toDateString() ? "Today"
                : d.toLocaleDateString(Qt.locale(), "M/d/yyyy");
        return day + " at " + d.toLocaleTimeString(Qt.locale(), "h:mm AP");
    }
    function sizeText(b) {
        if (!b) return "";
        var u = ["bytes", "KB", "MB", "GB", "TB"], i = 0;
        while (b >= 1000 && i < u.length - 1) { b /= 1000; ++i; }
        return (i === 0 ? b : b.toFixed(1)) + " " + u[i];
    }
    readonly property string statusText: {
        var st = Backup.status;
        if (!st.time)
            return Backup.settings.configured ? "It hasn't backed up yet." : "Choose the folders and a drive, then click Back Up Now.";
        var t = "<b>Last backup:</b> " + when(st.time) + ": " + st.message;
        if (st.result === "ok" && st.files !== undefined)
            t += " " + st.files + (st.files === 1 ? " file" : " files") + " copied" + (st.bytes ? " (" + sizeText(st.bytes) + ")" : "") + ".";
        if (st.result !== "ok" && st.lastOk)
            t += "<br>The last good one: " + when(st.lastOk) + ".";
        return t;
    }

    function backUpNow() {
        if (dirty)
            apply();
        if (!s.driveUuid) {
            messageBox.show("Backup", "Choose a drive to back up to first.", "info", ["OK"]);
            return;
        }
        if (!chosenDrive) {
            messageBox.show("Backup", (s.driveName || "The backup drive") + " isn't plugged in. Plug it in, then try again.", "info", ["OK"]);
            return;
        }
        if (chosenDrive.mounted) {
            Backup.backUpNow();
            return;
        }
        // open it first (as you: the desktop can, a scheduled backup may not)
        Drives.mount(chosenDrive.id, function (ok, result) {
            if (ok)
                Backup.backUpNow();
            else
                messageBox.show("Backup", result, "error", ["OK"]);
        });
    }

    Connections {
        target: Backup
        function onSetUpFinished(ok, message) {
            if (ok)
                sheet.backUpNow(); // and now, the backup it couldn't do
            else
                sheet.messageBox.show("Backup", message, "error", ["OK"]);
        }
        function onFinished(ok, message) {
            if (!ok && message)
                sheet.messageBox.show("Backup", "The backup didn't finish.\n\n" + message, "error", ["OK"]);
        }
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }

    // ---------------------------------------------------------------- Backup
    Item {
        AppletIcon { x: 4; y: 2; kind: "backup" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "Copies these folders to a drive you plug in. Each backup is kept as a folder of its own, "
                  + "so you can get back a file as it was then."
        }
        Label { y: 40; text: "Back up these folders:" }
        ProgramList {
            id: folderList
            y: 56
            width: parent.width - 86
            height: 110
            model: (sheet.s.folders || []).map(function (f) {
                var home = Backup.homePath();
                return { key: f, name: f.indexOf(home + "/") === 0 ? "~" + f.slice(home.length) : f };
            })
            iconComponent: AppletIcon { size: parent.size; kind: "folder" }
            noteFor: function (item) { return Backup.isDir(item.key) ? "" : "Not there"; }
            emptyText: "No folders. Add... chooses one."
        }
        Column {
            anchors.right: parent.right
            y: 56
            spacing: 6
            Button95 {
                width: 80
                text: "<u>A</u>dd..."
                onClicked: { folderDialog.mode = "folder"; folderDialog.openAt(Backup.homePath(), ""); }
            }
            Button95 {
                width: 80
                text: "<u>R</u>emove"
                enabled: !!folderList.selected
                onClicked: sheet.change("folders", sheet.s.folders.filter(function (f) { return f !== folderList.selectedKey; }))
            }
        }

        Label { y: folderList.y + folderList.height + 12; text: "To:" }
        ComboBox95 {
            id: driveBox
            x: 40
            y: folderList.y + folderList.height + 8
            width: parent.width - 40
            model: sheet.driveChoices.length ? sheet.driveChoices : ["(plug in a drive)"]
            popupMenu: sheet.popupMenu
            enabled: sheet.driveChoices.length > 0
            currentIndex: {
                for (var i = 0; i < sheet.removable.length; ++i)
                    if (sheet.removable[i].uuid === sheet.s.driveUuid)
                        return i;
                return sheet.s.driveUuid ? sheet.removable.length : 0;
            }
            onActivated: (i) => { if (i < sheet.removable.length) sheet.setDrive(sheet.removable[i]); }
        }
        // where on the drive: a folder on it (made if it isn't there)
        Label { y: driveBox.y + driveBox.height + 10; text: "Folder:" }
        TextField95 {
            id: destField
            objectName: "destField"
            x: 40
            y: driveBox.y + driveBox.height + 6
            width: parent.width - 40 - 86
            onTextChanged: {
                var t = text.replace(/^\/+|\/+$/g, "");
                if (t && t !== sheet.s.destDir)
                    sheet.change("destDir", t);
            }
        }
        Button95 {
            anchors.right: parent.right
            y: destField.y - 1
            width: 80
            text: "<u>B</u>rowse..."
            enabled: !!sheet.chosenDrive
            onClicked: {
                var d = sheet.chosenDrive;
                var open = function (mnt) {
                    destDialog.root = mnt;
                    destDialog.mode = "folder";
                    destDialog.openAt(mnt, "");
                };
                if (d.mounted)
                    open(d.mountPath);
                else
                    Drives.mount(d.id, function (ok, result) {
                        if (ok) open(result);
                        else sheet.messageBox.show("Backup", result, "error", ["OK"]);
                    });
            }
        }
        Label {
            id: whereLabel
            x: 40
            y: destField.y + destField.height + 3
            width: parent.width - 40
            color: Theme.shadow
            text: "on the drive (made if it isn't there)."
        }
        Label { y: whereLabel.y + whereLabel.height + 12; text: "Keep up to" }
        SpinBox95 {
            id: keepSpin
            x: 70
            y: whereLabel.y + whereLabel.height + 8
            width: 54
            from: 1
            to: 999
            value: sheet.s.keep || 10
            onValueModified: (v) => sheet.change("keep", v)
        }
        Label {
            x: 132
            y: keepSpin.y + 4
            width: parent.width - x
            text: "backups (older ones are removed)"
        }

        // the last one, and Back Up Now
        Bevel {
            id: statusBox
            y: keepSpin.y + keepSpin.height + 10
            width: parent.width
            height: parent.height - y - 34
            style: "thinSunken"
            Label {
                x: 8
                y: 6
                width: parent.width - 16
                visible: !Backup.running
                text: sheet.statusText
                color: Backup.status.result === "failed" ? "#a00000" : Theme.text
            }
            Button95 {
                id: setUpButton
                x: 8
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                width: 110
                visible: !Backup.running && Backup.status.result === "failed" && !!Backup.status.fix
                text: "Set Up <u>D</u>rive..."
                onClicked: sheet.messageBox.show("Backup",
                    "The drive belongs to the administrator (root), so your backups can't be saved on it.\n\n"
                    + "Set Up Drive makes the folder " + Backup.status.fix + " yours (it asks for an "
                    + "administrator's password). Nothing else on the drive changes.\n\nSet it up now?",
                    "question", ["Yes", "No"], function (b) { if (b === 0) Backup.setUpDrive(); })
            }
            Column {
                x: 8
                y: 6
                width: parent.width - 16
                spacing: 6
                visible: Backup.running
                Label { width: parent.width; text: Backup.progressText }
                ProgressBar95 { width: parent.width; value: Backup.percent }
            }
        }
        Row {
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            spacing: 6
            Button95 {
                width: 100
                text: Backup.running ? "<u>S</u>top" : "Back <u>U</u>p Now"
                enabled: Backup.running || (sheet.s.folders || []).length > 0
                onClicked: Backup.running ? Backup.stop() : sheet.backUpNow()
            }
            Button95 {
                width: 100
                text: "<u>O</u>pen Backups"
                enabled: !!Backup.status.lastOkFolder && Backup.isDir(Backup.status.lastOkFolder)
                onClicked: Backup.openFolder(Backup.status.lastOkFolder)
            }
            Button95 {
                width: 70
                text: "<u>V</u>iew Log"
                enabled: !!Backup.status.time
                onClicked: Backup.openLog()
            }
        }
    }

    // -------------------------------------------------------------- Schedule
    Item {
        GroupBox95 {
            id: whenBox
            title: "Back up automatically"
            width: parent.width
            height: 132
            Column {
                spacing: 6
                RadioButton95 {
                    text: "<u>N</u>ever (only Back Up Now)"
                    checked: sheet.s.schedule === "off"
                    onClicked: sheet.change("schedule", "off")
                }
                RadioButton95 {
                    text: "Every <u>h</u>our"
                    checked: sheet.s.schedule === "hourly"
                    onClicked: sheet.change("schedule", "hourly")
                }
                Row {
                    spacing: 6
                    RadioButton95 {
                        width: 120
                        text: "Every <u>d</u>ay at"
                        checked: sheet.s.schedule === "daily"
                        onClicked: sheet.change("schedule", "daily")
                    }
                }
                Row {
                    spacing: 6
                    RadioButton95 {
                        width: 120
                        text: "Every <u>w</u>eek on"
                        checked: sheet.s.schedule === "weekly"
                        onClicked: sheet.change("schedule", "weekly")
                    }
                    ComboBox95 {
                        width: 100
                        model: sheet.days
                        popupMenu: sheet.popupMenu
                        currentIndex: sheet.s.day || 0
                        onActivated: (i) => sheet.change("day", i)
                    }
                }
            }
            // the time, beside "Every day at" and "Every week on ... at"
            TextField95 {
                id: timeField
                objectName: "timeField"
                x: sheet.s.schedule === "weekly" ? 236 : 126
                y: sheet.s.schedule === "weekly" ? 72 : 48
                width: 56
                visible: sheet.s.schedule === "daily" || sheet.s.schedule === "weekly"
                onTextChanged: if (/^([01]?\d|2[0-3]):[0-5]\d$/.test(text) && text !== sheet.s.time) sheet.change("time", text)
            }
            Label {
                x: 216
                y: 76
                visible: sheet.s.schedule === "weekly"
                text: "at"
            }
        }
        CheckBox95 {
            id: pluggedBox
            y: whenBox.height + 10
            text: "Also back up when the drive is <u>p</u>lugged in, if one is due"
            checked: !!sheet.s.whenPlugged
            onToggled: (on) => sheet.change("whenPlugged", on)
        }
        Label {
            y: pluggedBox.y + pluggedBox.height + 4
            x: 20
            width: parent.width - 20
            color: Theme.shadow
            text: "If the computer is off, or the drive isn't plugged in, when a backup is due, it backs up "
                  + "the next time you plug the drive in."
        }
        GroupBox95 {
            id: leaveOut
            title: "Leave out"
            y: pluggedBox.y + 70
            width: parent.width
            height: 76
            Label { width: parent.width; text: "Files and folders matching (; between them, * as a wildcard, / for folders):" }
            TextField95 {
                id: excludeField
                y: 22
                width: parent.width
                onTextChanged: {
                    var list = text.split(";").map(function (p) { return p.trim(); }).filter(function (p) { return p.length > 0; });
                    if (JSON.stringify(list) !== JSON.stringify(sheet.s.excludes || []))
                        sheet.change("excludes", list);
                }
            }
        }
        Label {
            anchors.bottom: parent.bottom
            width: parent.width
            color: Theme.shadow
            text: Backup.cronLine(sheet.s) ? "cron starts it: " + Backup.cronLine(sheet.s).replace(/ '.*/, "") + " (minute hour day month weekday)"
                                           : "No scheduled backups."
        }
    }

    extras: [
    // a folder on the drive to back up to (kept as a path on the drive, so
    // it's found wherever the drive is mounted)
    FileDialog95 {
        id: destDialog
        property string root: ""
        messageBox: sheet.messageBox
        popupMenu: sheet.popupMenu
        acceptLabel: "Choose"
        onAccepted: (path) => {
            if (path !== root && path.indexOf(root + "/") !== 0) {
                sheet.messageBox.show("Backup", "Choose a folder on " + (sheet.chosenDrive ? sheet.chosenDrive.name : "the drive") + ".",
                                      "info", ["OK"]);
                return;
            }
            destField.text = path === root ? "" : path.slice(root.length + 1);
            if (path === root)
                sheet.messageBox.show("Backup", "Backups need a folder of their own on the drive: choose or make one in it.", "info", ["OK"]);
        }
    },

    FileDialog95 {
        id: folderDialog
        messageBox: sheet.messageBox
        popupMenu: sheet.popupMenu
        onAccepted: (path) => {
            var list = (sheet.s.folders || []).slice();
            if (list.indexOf(path) < 0)
                list.push(path);
            sheet.change("folders", list);
        }
    }
    ]
}
