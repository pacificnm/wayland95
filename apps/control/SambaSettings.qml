import QtQuick
import Win95Ui
import Win95Control

// Samba's settings: the computer's workgroup and description, and your
// Samba password (General), and the folders you share (Shared Folders).
// Written by wayland95-services (smb-config, smb-password) to
// /etc/samba/wayland95.conf, which smb.conf includes. Each share acts as
// its owner, so it gives others only what you could reach yourself.
PropertySheet {
    id: sheet

    title: "Samba Properties"
    titles: ["General", "Shared Folders"]

    readonly property var server: Services.services.filter(function (s) { return s.id === "samba"; })[0] || ({})
    property var config: ({})
    // yours, as they'll be after Apply; others' shares are shown, not changed
    property var myShares: []
    readonly property var otherShares: (config.shares || []).filter(function (s) { return !s.mine; })
    readonly property string host: Services.hostName()

    property bool applying: false
    property bool closeWhenDone: false
    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    function load() {
        config = Services.sambaConfig();
        myShares = (config.shares || []).filter(function (s) { return s.mine; });
        workgroupField.text = config.workgroup || "WORKGROUP";
        descriptionField.text = config.description || "";
        dirty = false;
    }
    Component.onCompleted: load()

    function shareNote(s) {
        return (s.access === "full" ? "Full" : "Read-only") + ", "
               + (s.who === "anyone" ? "anyone" : s.mine ? "you" : s.owner)
               + (s.mine ? "" : " (" + s.owner + "'s)");
    }
    function uncPath(name) { return "\\\\" + host + "\\" + name; }

    onApplyRequested: {
        var lines = [
            "workgroup=" + workgroupField.text.trim(),
            "description=" + descriptionField.text.trim(),
        ];
        for (var i = 0; i < myShares.length; ++i) {
            var s = myShares[i];
            lines.push("share=" + [s.name, s.path, s.access, s.who, s.comment || ""].join("\t"));
        }
        applying = true;
        Services.admin(["smb-config"], lines.join("\n"), function (ok, output) {
            sheet.applying = false;
            Services.refresh();
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("Samba", output || "The settings couldn't be changed.", "error", ["OK"],
                                      function () { sheet.load(); });
                return;
            }
            sheet.load();
            if (sheet.closeWhenDone)
                sheet.close();
        });
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "samba" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "Samba" + (sheet.config.version ? " " + sheet.config.version : "") + " is <b>"
                  + (sheet.server.active ? "running" : sheet.server.enabled ? "stopped" : "off") + "</b>."
                  + (sheet.applying ? " Changing its settings..." : "")
                  + "<br>Other computers find this one as <b>" + sheet.uncPath("").replace(/\\$/, "") + "</b>"
        }

        GroupBox95 {
            id: identityBox
            y: 46
            width: parent.width
            height: 120
            title: "Identification"
            Grid {
                width: parent.width
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                Label { width: 124; text: "Computer name:" }
                Label { text: sheet.host }
                Label { width: 124; text: "<u>W</u>orkgroup:" }
                TextField95 {
                    id: workgroupField
                    objectName: "sambaWorkgroup"
                    width: 160
                    input.maximumLength: 15
                    input.onTextEdited: sheet.dirty = true
                }
                Label { width: 124; text: "Computer <u>D</u>escription:" }
                TextField95 {
                    id: descriptionField
                    objectName: "sambaDescription"
                    width: 220
                    input.maximumLength: 80
                    input.onTextEdited: sheet.dirty = true
                }
            }
        }
        Label {
            y: identityBox.y + identityBox.height + 4
            width: parent.width
            color: Theme.shadow
            text: "Windows computers in the same workgroup see this one in their network. "
                  + "(%h in the description is the computer's name.)"
        }

        GroupBox95 {
            id: passwordBox
            y: identityBox.y + identityBox.height + 44
            width: parent.width
            height: 104
            title: "Your Samba password"
            Label {
                width: parent.width
                text: "Folders shared with only you need a password: other computers log on as <b>"
                      + Services.userName + "</b> with your Samba password, which can differ from the "
                      + "one you log on here with."
            }
            Button95 {
                y: 50
                width: 120
                text: "<u>S</u>et Password..."
                onClicked: passwordDialog.open()
            }
        }
    }

    // ------------------------------------------------------------ Shared Folders
    Item {
        Label {
            width: parent.width
            text: "Folders shared with other computers:"
        }
        ProgramList {
            id: shareList
            objectName: "sambaShares"
            y: 18
            width: parent.width
            height: 150
            model: sheet.myShares.concat(sheet.otherShares).map(function (s) {
                return Object.assign({ key: s.name }, s);
            })
            iconComponent: AppletIcon { size: parent.size; kind: "samba" }
            noteFor: function (item) { return sheet.shareNote(item); }
            emptyText: "No folders are shared. Click Add to share one."
            onActivated: (item) => { if (item.mine) shareDialog.edit(item); }
        }
        Row {
            id: shareButtons
            y: shareList.y + shareList.height + 8
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>A</u>dd..."
                onClicked: shareDialog.edit(null)
            }
            Button95 {
                text: "<u>R</u>emove"
                enabled: !!shareList.selected && shareList.selected.mine
                onClicked: {
                    var name = shareList.selected.name;
                    sheet.myShares = sheet.myShares.filter(function (s) { return s.name !== name; });
                    sheet.dirty = true;
                }
            }
            Button95 {
                width: 90
                text: "<u>P</u>roperties..."
                enabled: !!shareList.selected && shareList.selected.mine
                onClicked: shareDialog.edit(shareList.selected)
            }
        }
        GroupBox95 {
            y: shareButtons.y + 32
            width: parent.width
            height: parent.height - y
            title: "Connecting"
            Label {
                width: parent.width
                text: {
                    var s = shareList.selected;
                    if (!s)
                        return "Folders are shared when you click Apply, while Samba is running.";
                    var addresses = Services.addresses();
                    return "From Windows: <b>" + sheet.uncPath(s.name) + "</b><br>"
                           + "From a Mac or Linux: <b>smb://" + (addresses.length ? addresses[0] : sheet.host) + "/"
                           + encodeURIComponent(s.name).replace(/%20/g, " ") + "</b>"
                           + (s.who === "anyone"
                              ? "<br>No password. (Newer Windows won't connect to shares without one: "
                                + "share it with only you there.)"
                              : "<br>Log on as " + s.owner + ", with " + (s.mine ? "your" : "their") + " Samba password.");
                }
            }
        }
    }

    extras: [
        // a share's properties, like Windows' Sharing tab
        Dialog95 {
            id: shareDialog
            title: editing ? "Shared Folder Properties" : "Share a Folder"
            dialogWidth: 360
            dialogHeight: 306
            // the share being changed (its name before), or "" for a new one
            property string editing: ""

            function edit(share) {
                editing = share ? share.name : "";
                nameField.text = share ? share.name : "";
                pathField.text = share ? share.path : "";
                commentField.text = share ? share.comment || "" : "";
                fullRadio.checked = !!share && share.access === "full";
                anyoneRadio.checked = !!share && share.who === "anyone";
                show();
                (share ? nameField : pathField).forceActiveFocus();
            }
            function problem() {
                var name = nameField.text.trim();
                if (!/^[A-Za-z0-9_.-][A-Za-z0-9 _.-]*$/.test(name) || name.length > 80)
                    return "A share's name can have letters, digits, spaces, - _ and . in it (80 at most).";
                if (["global", "homes", "printers"].indexOf(name.toLowerCase()) >= 0)
                    return "\"" + name + "\" is one of Samba's own names.";
                var all = sheet.myShares.concat(sheet.otherShares);
                for (var i = 0; i < all.length; ++i)
                    if (all[i].name.toLowerCase() === name.toLowerCase() && all[i].name !== editing)
                        return "There's already a shared folder called \"" + all[i].name + "\".";
                var path = pathField.text.trim();
                if (path.charAt(0) !== "/" || !Services.exists(path))
                    return "Choose a folder to share.";
                if (path.replace(/\/+$/, "") === Services.homePath)
                    return "Your whole home folder can't be shared: choose a folder in it.";
                if (/["\\%]/.test(commentField.text))
                    return "The comment can't have \" \\ or % in it.";
                return "";
            }
            function ok() {
                var why = problem();
                if (why) {
                    sheet.messageBox.show("Samba", why, "error", ["OK"]);
                    return;
                }
                var share = {
                    name: nameField.text.trim(),
                    path: pathField.text.trim().replace(/\/+$/, ""),
                    comment: commentField.text.trim(),
                    access: fullRadio.checked ? "full" : "read",
                    who: anyoneRadio.checked ? "anyone" : "me",
                    owner: Services.userName,
                    mine: true,
                };
                var list = sheet.myShares.filter(function (s) { return s.name !== shareDialog.editing; });
                list.push(share);
                list.sort(function (a, b) { return a.name.toLowerCase() < b.name.toLowerCase() ? -1 : 1; });
                sheet.myShares = list;
                shareList.selectedKey = share.name;
                sheet.dirty = true;
                close();
            }

            Grid {
                x: 12
                y: 14
                columns: 2
                columnSpacing: 8
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                Label { width: 76; text: "<u>F</u>older:" }
                Row {
                    spacing: 4
                    TextField95 {
                        id: pathField
                        objectName: "sambaSharePath"
                        width: 180
                        onAccepted: nameField.forceActiveFocus()
                        input.onTextEdited: {
                            // a new share is named after its folder
                            if (!shareDialog.editing)
                                nameField.text = text.replace(/\/+$/, "").split("/").pop().replace(/[^A-Za-z0-9 _.-]/g, "").slice(0, 80);
                        }
                    }
                    Button95 {
                        width: 66
                        text: "<u>B</u>rowse..."
                        onClicked: folderDialog.openAt(pathField.text || Services.homePath, "")
                    }
                }
                Label { width: 76; text: "Share <u>N</u>ame:" }
                TextField95 {
                    id: nameField
                    objectName: "sambaShareName"
                    width: 160
                    input.maximumLength: 80
                    onAccepted: commentField.forceActiveFocus()
                }
                Label { width: 76; text: "<u>C</u>omment:" }
                TextField95 {
                    id: commentField
                    width: 250
                    input.maximumLength: 80
                    onAccepted: shareDialog.ok()
                }
            }
            Text {
                x: 12
                y: 104
                text: "Access Type:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Column {
                x: 20
                y: 122
                spacing: 4
                RadioButton95 {
                    text: "<u>R</u>ead-only"
                    checked: !fullRadio.checked
                    onClicked: fullRadio.checked = false
                }
                RadioButton95 {
                    id: fullRadio
                    text: "Fu<u>l</u>l: others can change, add and delete files"
                    onClicked: checked = true
                }
            }
            Text {
                x: 12
                y: 170
                text: "Who may connect:"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Column {
                x: 20
                y: 188
                spacing: 4
                RadioButton95 {
                    text: "Only <u>m</u>e, with my Samba password"
                    checked: !anyoneRadio.checked
                    onClicked: anyoneRadio.checked = false
                }
                RadioButton95 {
                    id: anyoneRadio
                    text: "An<u>y</u>one on the network, without a password"
                    onClicked: checked = true
                }
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
                    enabled: nameField.text.trim().length > 0 && pathField.text.trim().length > 0
                    onClicked: shareDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: shareDialog.close()
                }
            }
        },
        Dialog95 {
            id: passwordDialog
            title: "Set Samba Password"
            dialogWidth: 340
            dialogHeight: 168

            function open() {
                pw1.text = "";
                pw2.text = "";
                show();
                pw1.forceActiveFocus();
            }
            function ok() {
                if (pw1.text !== pw2.text) {
                    sheet.messageBox.show("Samba", "The passwords don't match. Type them again.", "error", ["OK"], function () {
                        pw1.text = ""; pw2.text = ""; pw1.forceActiveFocus();
                    });
                    return;
                }
                var pw = pw1.text;
                passwordDialog.close();
                pw1.text = "";
                pw2.text = "";
                Services.admin(["smb-password"], pw, function (ok, output) {
                    sheet.messageBox.show("Samba", !ok ? (output || "The password couldn't be set.")
                                          : pw ? "Your Samba password has been set. Other computers log on as "
                                                 + Services.userName + " with it."
                                          : "Your Samba password has been taken away: folders shared with only you "
                                            + "can't be opened from other computers.",
                                          ok ? "info" : "error", ["OK"]);
                });
            }
            Grid {
                x: 12
                y: 14
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                verticalItemAlignment: Grid.AlignVCenter
                component PasswordField: TextField95 {
                    width: 170
                    Component.onCompleted: {
                        input.echoMode = TextInput.Password;
                        input.passwordCharacter = "*";
                    }
                }
                Text { text: "<u>P</u>assword:"; textFormat: Text.StyledText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw1; onAccepted: pw2.forceActiveFocus() }
                Text { text: "<u>C</u>onfirm password:"; textFormat: Text.StyledText; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw2; onAccepted: passwordDialog.ok() }
            }
            Text {
                x: 12
                y: 80
                width: parent.width - 24
                wrapMode: Text.Wrap
                text: "Leave both empty to take your Samba password away."
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
                    onClicked: passwordDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: passwordDialog.close()
                }
            }
        },
        FileDialog95 {
            id: folderDialog
            mode: "folder"
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            onAccepted: (path) => {
                pathField.text = path;
                if (!shareDialog.editing)
                    nameField.text = path.replace(/\/+$/, "").split("/").pop().replace(/[^A-Za-z0-9 _.-]/g, "").slice(0, 80);
            }
        }
    ]
}
