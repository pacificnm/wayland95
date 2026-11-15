import QtQuick
import Win95Ui
import Win95Control

// SSH Server's settings: how people may log on (written to
// /etc/ssh/sshd_config.d/50-wayland95.conf by wayland95-services), the keys
// that may log on as you (~/.ssh/authorized_keys), and how to connect.
PropertySheet {
    id: sheet

    title: "SSH Server Properties"
    titles: ["General", "Keys", "Connecting"]

    readonly property var server: Services.services.filter(function (s) { return s.id === "ssh"; })[0] || ({})
    property var config: Services.sshConfig()
    readonly property var rootChoices: ["no", "prohibit-password", "yes"]

    property bool applying: false
    property bool closeWhenDone: false
    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    function load() {
        config = Services.sshConfig();
        portBox.value = config.Port;
        passwordBox.checked = config.PasswordAuthentication;
        keyBox.checked = config.PubkeyAuthentication;
        x11Box.checked = config.X11Forwarding;
        rootBox.currentIndex = Math.max(0, rootChoices.indexOf(config.PermitRootLogin));
        usersField.text = config.AllowUsers;
        dirty = false;
    }
    Component.onCompleted: {
        load();
        loadKeys();
        loadHostKeys();
    }

    onApplyRequested: {
        if (!passwordBox.checked && !keyBox.checked) {
            messageBox.show("SSH Server", "With both passwords and keys turned off, nobody could log on. Turn one of them back on.",
                            "warning", ["OK"]);
            dirty = true;
            return;
        }
        var users = usersField.text.trim().replace(/\s+/g, " ");
        var lines = [
            "Port=" + portBox.value,
            "PasswordAuthentication=" + (passwordBox.checked ? "yes" : "no"),
            "PubkeyAuthentication=" + (keyBox.checked ? "yes" : "no"),
            "PermitRootLogin=" + rootChoices[rootBox.currentIndex],
            "X11Forwarding=" + (x11Box.checked ? "yes" : "no"),
            "AllowUsers=" + users,
        ];
        applying = true;
        Services.admin(["ssh-config"], lines.join("\n"), function (ok, output) {
            sheet.applying = false;
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("SSH Server", output || "The settings couldn't be changed.", "error", ["OK"],
                                      function () { sheet.load(); });
                return;
            }
            sheet.config = Services.sshConfig();
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
        AppletIcon { x: 4; y: 2; kind: "ssh" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "SSH Server is <b>" + (sheet.server.active ? "running" : sheet.server.enabled ? "stopped" : "off")
                  + "</b>." + (sheet.applying ? " Changing settings..." : "")
        }
        Label {
            x: 46
            y: 20
            width: parent.width - x
            color: Theme.shadow
            text: sheet.server.version ? "OpenSSH " + sheet.server.version : ""
        }

        GroupBox95 {
            id: networkBox
            y: 44
            width: parent.width
            height: 54
            title: "Network"
            Label { y: 3; text: "Listen on <u>p</u>ort:" }
            SpinBox95 {
                id: portBox
                x: 110
                width: 70
                from: 1
                to: 65535
                onValueModified: sheet.dirty = true
            }
            Label {
                x: 190
                y: 3
                color: Theme.shadow
                text: "(usually 22)"
            }
        }

        GroupBox95 {
            id: logonBox
            y: networkBox.y + networkBox.height + 6
            width: parent.width
            height: 150
            title: "Logging on"
            Column {
                width: parent.width
                spacing: 6
                CheckBox95 {
                    id: passwordBox
                    text: "Allow logging on with a pass<u>w</u>ord"
                    onToggled: sheet.dirty = true
                }
                CheckBox95 {
                    id: keyBox
                    text: "Allow logging on with a <u>k</u>ey (see the Keys tab)"
                    onToggled: sheet.dirty = true
                }
                Row {
                    spacing: 8
                    Label { anchors.verticalCenter: parent.verticalCenter; text: "<u>A</u>dministrator (root):" }
                    ComboBox95 {
                        id: rootBox
                        width: 190
                        popupMenu: sheet.popupMenu
                        model: ["Can't log on", "Can log on with a key only", "Can log on"]
                        onActivated: sheet.dirty = true
                    }
                }
                Label { text: "Only these <u>u</u>sers may log on (blank for everyone):" }
                TextField95 {
                    id: usersField
                    width: parent.width
                    onTextChanged: if (activeFocus) sheet.dirty = true
                }
            }
        }

        CheckBox95 {
            id: x11Box
            y: logonBox.y + logonBox.height + 8
            text: "Let graphical programs show on the other computer (<u>X</u>11 forwarding)"
            onToggled: sheet.dirty = true
        }
    }

    // --------------------------------------------------------------- Keys
    Item {
        id: keysPage
        property var keys: []

        Label {
            width: parent.width
            text: "Anyone with one of these keys can log on as <b>" + Services.userName + "</b> without typing a password:"
        }
        ProgramList {
            id: keyList
            y: 36
            width: parent.width
            height: 180
            model: keysPage.keys
            iconComponent: AppletIcon { size: parent.size; kind: "passwords" }
            noteFor: function (item) { return item.type; }
            emptyText: "No keys yet. Click Add, and paste the contents of a public key file (such as id_ed25519.pub) from the computer you'll connect from."
        }
        Row {
            y: keyList.y + keyList.height + 8
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>A</u>dd..."
                onClicked: addKeyDialog.open()
            }
            Button95 {
                text: "<u>R</u>emove"
                enabled: !!keyList.selected
                onClicked: {
                    var k = keyList.selected;
                    sheet.messageBox.show("SSH Server", "Remove the key \"" + k.name + "\"?\n\nWhoever has it won't be able to log on with it any more.",
                                          "question", ["Yes", "No"], function (b) {
                        if (b === 0)
                            sheet.removeKey(k.key);
                    });
                }
            }
        }
        Label {
            anchors.bottom: parent.bottom
            width: parent.width
            color: Theme.shadow
            text: "Keys are kept in ~/.ssh/authorized_keys. Changes here happen straight away."
        }
    }

    // -------------------------------------------------------- Connecting
    Item {
        id: connectPage
        property var hostKeys: []

        Label {
            width: parent.width
            text: "From another computer on the network, connect with one of these commands:"
        }
        Bevel {
            id: commands
            y: 34
            width: parent.width
            height: 110
            style: "sunken"
            face: Theme.window
            Column {
                x: 6
                y: 4
                width: parent.width - 12
                spacing: 2
                Repeater {
                    // NAME.local needs mDNS (Avahi) on this computer
                    model: (Services.exists("/usr/sbin/avahi-daemon") ? [Services.hostName() + ".local"] : []).concat(Services.addresses())
                    TextEdit {
                        required property string modelData
                        width: parent.width
                        readOnly: true
                        selectByMouse: true
                        text: "ssh " + (sheet.config.Port !== 22 ? "-p " + sheet.config.Port + " " : "")
                              + Services.userName + "@" + modelData
                        font.family: "monospace"
                        font.pixelSize: Theme.fontSize
                        color: Theme.text
                    }
                }
            }
        }
        Label {
            y: commands.y + commands.height + 10
            width: parent.width
            text: "The first time, ssh shows this computer's key fingerprint. It should be one of these:"
        }
        Bevel {
            y: commands.y + commands.height + 44
            width: parent.width
            height: parent.height - y
            style: "sunken"
            face: Theme.window
            Column {
                x: 6
                y: 4
                width: parent.width - 12
                spacing: 2
                Repeater {
                    model: connectPage.hostKeys
                    TextEdit {
                        required property string modelData
                        width: parent.width
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        text: modelData
                        font.family: "monospace"
                        font.pixelSize: Theme.fontSize - 1
                        color: Theme.text
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------- helpers
    function parseKeys(text) {
        var out = [];
        var lines = text.split("\n");
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i].trim();
            if (!line || line.charAt(0) === "#")
                continue;
            // [options] type base64 [comment]
            var m = line.match(/(?:^|\s)((?:ssh|ecdsa|sk)-[A-Za-z0-9@.-]+)\s+([A-Za-z0-9+\/=]+)(?:\s+(.*))?$/);
            var type = m ? m[1].replace(/^ssh-/, "").replace(/@openssh\.com$/, "") : "?";
            var comment = m && m[3] ? m[3] : (m ? m[2].slice(-16) : line.slice(0, 30));
            out.push({ key: String(i), line: lines[i], name: comment, type: type });
        }
        return out;
    }
    function loadKeys() {
        keysPage.keys = parseKeys(Services.readFile("~/.ssh/authorized_keys"));
    }
    function saveKeys(lines) {
        if (!Services.writeFile("~/.ssh/authorized_keys", lines.join("\n") + (lines.length ? "\n" : ""), true))
            messageBox.show("SSH Server", "~/.ssh/authorized_keys couldn't be written.", "error", ["OK"]);
        loadKeys();
    }
    function removeKey(index) {
        var lines = Services.readFile("~/.ssh/authorized_keys").split("\n");
        lines.splice(Number(index), 1);
        saveKeys(lines.filter(function (l) { return l.length > 0; }));
    }
    function addKey(text) {
        var key = text.trim().replace(/\s+/g, " ");
        if (!/^((ssh|ecdsa|sk)-[A-Za-z0-9@.-]+) [A-Za-z0-9+\/=]{40,}( .*)?$/.test(key)) {
            messageBox.show("SSH Server", "That doesn't look like a public key. It should be one line starting with ssh-ed25519, ssh-rsa or ecdsa-..., "
                            + "as found in a .pub file.", "error", ["OK"], function () { addKeyDialog.open(text); });
            return;
        }
        var lines = Services.readFile("~/.ssh/authorized_keys").split("\n").filter(function (l) { return l.length > 0; });
        if (lines.some(function (l) { return l.indexOf(key.split(" ")[1]) >= 0; })) {
            messageBox.show("SSH Server", "That key is already in the list.", "info", ["OK"]);
            return;
        }
        lines.push(key);
        saveKeys(lines);
    }
    function loadHostKeys() {
        var files = ["ed25519", "ecdsa", "rsa"].map(function (t) { return "/etc/ssh/ssh_host_" + t + "_key.pub"; })
                                                 .filter(function (f) { return Services.exists(f); });
        connectPage.hostKeys = [];
        files.forEach(function (f) {
            Services.run("ssh-keygen", ["-l", "-f", f], "", function (code, out) {
                if (code === 0) {
                    var p = out.trim().split(" "); // bits SHA256:... comment (TYPE)
                    connectPage.hostKeys = connectPage.hostKeys.concat([p[p.length - 1].replace(/[()]/g, "") + "  " + p[1]]);
                }
            });
        });
    }

    extras: [
        Dialog95 {
            id: addKeyDialog
            title: "Add Key"
            dialogWidth: 370
            dialogHeight: 130

            function open(text) {
                keyField.text = text || "";
                show();
                keyField.forceActiveFocus();
            }
            Text {
                x: 12
                y: 12
                width: 346
                wrapMode: Text.Wrap
                text: "Paste the public key (one line from a .pub file):"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: keyField
                x: 12
                y: 34
                width: 346
                onAccepted: { addKeyDialog.close(); sheet.addKey(keyField.text); }
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
                    enabled: keyField.text.trim().length > 0
                    onClicked: { addKeyDialog.close(); sheet.addKey(keyField.text); }
                }
                Button95 {
                    text: "Cancel"
                    onClicked: addKeyDialog.close()
                }
            }
        }
    ]
}
