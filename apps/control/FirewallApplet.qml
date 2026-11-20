import QtQuick
import Win95Ui
import Win95Control

// Firewall: stops other computers connecting to this one, except through
// the exceptions chosen here (a program's ports, from anywhere or from the
// local network only). Connections this computer makes aren't affected.
// Apply writes it all through wayland95-services fw-save, as root.
PropertySheet {
    id: sheet

    title: "Firewall"
    titles: ["General", "Exceptions", "Advanced"]

    // the settings being edited: a copy of Firewall.config
    property var cfg: copy(Firewall.config)
    property bool applying: false
    property bool closeWhenDone: false

    readonly property var ssh: Services.services.filter(function (s) { return s.id === "ssh"; })[0] || null
    readonly property bool sshAllowed: cfg.exceptions && cfg.allow.some(function (e) { return e.key === "ssh" && e.on; })
    readonly property bool sshAtRisk: cfg.enabled && !!ssh && !!ssh.active && !sshAllowed

    function copy(o) { return JSON.parse(JSON.stringify(o)); }
    function set(key, value) {
        var c = copy(cfg);
        c[key] = value;
        cfg = c;
        dirty = true;
    }
    function setException(key, changes) {
        var c = copy(cfg);
        for (var i = 0; i < c.allow.length; ++i)
            if (c.allow[i].key === key)
                Object.assign(c.allow[i], changes);
        cfg = c;
        dirty = true;
    }
    function exception(key) {
        for (var i = 0; i < cfg.allow.length; ++i)
            if (cfg.allow[i].key === key)
                return cfg.allow[i];
        return null;
    }
    function portsText(e) {
        var parts = [];
        if (e.tcp) parts.push("TCP " + e.tcp);
        if (e.udp) parts.push("UDP " + e.udp);
        return parts.join(", ");
    }
    function load() {
        cfg = copy(Firewall.config);
        dirty = false;
    }

    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }
    Component.onCompleted: Firewall.refresh()
    Connections {
        target: Firewall
        function onConfigChanged() { if (!sheet.dirty && !sheet.applying) sheet.load(); }
    }

    onApplyRequested: {
        if (!Firewall.available) {
            messageBox.show("Firewall", "nftables isn't installed, so the firewall can't be turned on (sudo apt install nftables).",
                            "error", ["OK"]);
            dirty = true;
            return;
        }
        if (sshAtRisk) {
            messageBox.show("Firewall", "SSH Server is running, but it isn't an exception, so nobody will be able to log on "
                            + "from another computer. If you're connected over SSH now, you'll be cut off.\n\nTurn the firewall on anyway?",
                            "warning", ["Yes", "No"], function (b) {
                if (b === 0)
                    save();
                else
                    sheet.dirty = true;
            });
            return;
        }
        save();
    }

    function save() {
        var c = copy(cfg);
        // SSH's exception follows the port the SSH server listens on
        var sshCfg = Services.sshConfig();
        for (var i = 0; i < c.allow.length; ++i)
            if (c.allow[i].key === "ssh" && sshCfg && sshCfg.Port)
                c.allow[i].tcp = String(sshCfg.Port);
        applying = true;
        Services.admin(["fw-save"], Firewall.toLines(c), function (ok, output) {
            sheet.applying = false;
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.dirty = true;
                sheet.messageBox.show("Firewall", output || "The firewall's settings couldn't be changed.", "error", ["OK"]);
                return;
            }
            Firewall.refresh();
            if (sheet.closeWhenDone)
                sheet.close();
        });
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }

    // ================================================================ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "firewall" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "A firewall stops other computers from connecting to this one, except through the "
                  + "exceptions on the Exceptions tab. What you connect to yourself (web sites, "
                  + "updates) isn't affected."
        }

        Column {
            x: 4
            y: 62
            width: parent.width - 8
            spacing: 4

            RadioButton95 {
                text: "<b>On (recommended)</b>"
                checked: sheet.cfg.enabled
                onClicked: sheet.set("enabled", true)
            }
            Label {
                x: 22
                width: parent.width - x
                text: "Connections from other computers are blocked unless they're exceptions."
            }
            CheckBox95 {
                x: 22
                text: "<u>D</u>on't allow exceptions"
                checked: !sheet.cfg.exceptions
                enabled: sheet.cfg.enabled
                onToggled: (on) => sheet.set("exceptions", !on)
            }
            Label {
                x: 42
                width: parent.width - x
                color: sheet.cfg.enabled ? Theme.text : Theme.disabledText
                text: "For networks you don't trust, like a café's or a hotel's Wi-Fi."
            }
            Item { width: 1; height: 6 }
            RadioButton95 {
                text: "<b>Off (not recommended)</b>"
                checked: !sheet.cfg.enabled
                onClicked: sheet.set("enabled", false)
            }
            Label {
                x: 22
                width: parent.width - x
                text: "Any program listening on the network can be reached by other computers."
            }
        }

        GroupBox95 {
            anchors.bottom: parent.bottom
            width: parent.width
            height: statusCol.height + 30
            title: "Status"
            Column {
                id: statusCol
                width: parent.width
                spacing: 6
                Row {
                    spacing: 6
                    Icon95 { name: Firewall.active ? "ok" : "warning" }
                    Label {
                        text: !Firewall.available ? "nftables isn't installed, so the firewall can't run (sudo apt install nftables)."
                              : Firewall.active ? "The firewall is on."
                              : "The firewall is off."
                        width: statusCol.width - 22
                    }
                }
                Label {
                    visible: Firewall.others.length > 0
                    width: parent.width
                    text: "Another firewall is on too (" + Firewall.others.join(", ") + "). Both apply: a connection "
                          + "gets in only if each of them lets it."
                }
                Label {
                    visible: sheet.sshAtRisk
                    width: parent.width
                    color: "#800000"
                    text: "SSH Server is running but isn't an exception: logging on from other computers will be blocked."
                }
            }
        }
    }

    // ============================================================= Exceptions
    Item {
        Label {
            width: parent.width
            text: sheet.cfg.exceptions
                  ? "Connections to these are let in, from the computers shown. Each one turned on makes this computer "
                    + "a little easier to attack, so turn on only the ones you use."
                  : "<b>Don't allow exceptions</b> is on (General tab), so none of these are let in."
        }

        ProgramList {
            id: list
            y: 46
            width: parent.width
            height: parent.height - y - buttons.height - 10
            checkable: true
            enabled: sheet.cfg.exceptions
            model: sheet.cfg.allow
            checked: {
                var m = {};
                sheet.cfg.allow.forEach(function (e) { if (e.on) m[e.key] = true; });
                return m;
            }
            iconComponent: AppletIcon {
                size: parent.size
                kind: {
                    var k = parent.item.key;
                    return k === "mdns" ? "printers" : k === "web" ? "nginx" : k === "samba" || k === "ssh" || k === "postgresql"
                           || k === "mysql" || k === "wireguard" ? k : "network";
                }
            }
            noteFor: function (item) {
                return sheet.portsText(item) + (item.scope === "local" ? ", local" : "");
            }
            onCheckToggled: (key, on) => sheet.setException(key, { on: on })
            onActivated: (item) => editDialog.open(item)
            Component.onCompleted: if (model.length) selectedKey = model[0].key
        }

        Row {
            id: buttons
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            spacing: 6
            Button95 {
                width: 80
                text: "Add <u>P</u>ort..."
                enabled: sheet.cfg.exceptions
                onClicked: editDialog.open(null)
            }
            Button95 {
                width: 70
                text: "<u>E</u>dit..."
                enabled: sheet.cfg.exceptions && !!list.selected
                onClicked: editDialog.open(list.selected)
            }
            Button95 {
                width: 70
                text: "<u>D</u>elete"
                enabled: sheet.cfg.exceptions && !!list.selected && !list.selected.builtIn
                onClicked: {
                    var key = list.selected.key;
                    sheet.messageBox.show("Firewall", "Delete the exception for " + list.selected.name + "?", "question", ["Yes", "No"],
                                          function (b) {
                        if (b !== 0)
                            return;
                        var c = sheet.copy(sheet.cfg);
                        c.allow = c.allow.filter(function (e) { return e.key !== key; });
                        sheet.cfg = c;
                        sheet.dirty = true;
                    });
                }
            }
        }
    }

    // =============================================================== Advanced
    Item {
        Column {
            width: parent.width
            spacing: 8

            GroupBox95 {
                width: parent.width
                height: 74
                title: "Ping"
                Label {
                    width: parent.width
                    text: "Other computers can check this one is there by pinging it (ICMP echo)."
                }
                CheckBox95 {
                    y: 30
                    text: "<u>A</u>nswer pings"
                    checked: sheet.cfg.ping
                    onToggled: (on) => sheet.set("ping", on)
                }
            }

            GroupBox95 {
                width: parent.width
                height: 104
                title: "Logging"
                Label {
                    width: parent.width
                    text: "Blocked connections can be written in the kernel's log (at most 10 a minute), "
                          + "to see in System Log."
                }
                CheckBox95 {
                    y: 30
                    text: "<u>L</u>og blocked connections"
                    checked: sheet.cfg.log
                    onToggled: (on) => sheet.set("log", on)
                }
                Button95 {
                    y: 52
                    width: 170
                    text: "<u>S</u>how Blocked Connections..."
                    onClicked: Firewall.showBlocked()
                }
            }

            GroupBox95 {
                width: parent.width
                height: 82
                title: "Default settings"
                Label {
                    width: parent.width
                    text: "Puts back the exceptions the firewall started with, and turns ping and logging off."
                }
                Button95 {
                    y: 30
                    width: 110
                    text: "<u>R</u>estore Defaults"
                    onClicked: sheet.messageBox.show("Firewall", "Restore the firewall's default exceptions and settings? "
                                                     + "The ports you added are deleted.", "question", ["Yes", "No"], function (b) {
                        if (b !== 0)
                            return;
                        var d = Firewall.defaults();
                        d.enabled = sheet.cfg.enabled;
                        sheet.cfg = d;
                        sheet.dirty = true;
                    })
                }
            }

            Label {
                width: parent.width
                color: Theme.shadow
                text: "The rules are in /etc/wayland95/firewall.nft (an nftables table of their own, "
                      + "inet wayland95), loaded at start-up by wayland95-firewall.service."
            }
        }
    }

    // ============================================ Add Port / Edit an exception
    extras: Dialog95 {
        id: editDialog
        title: editing ? "Edit Exception" : "Add a Port"
        dialogWidth: 360
        dialogHeight: editing && editing.builtIn ? 190 : 250

        property var editing: null
        property string proto: "tcp"
        property string scope: "any"

        function open(item) {
            editing = item;
            nameField.text = item ? item.name : "";
            portField.text = item ? (item.tcp || item.udp) : "";
            proto = !item ? "tcp" : item.tcp && item.udp ? "both" : item.udp ? "udp" : "tcp";
            scope = item ? item.scope : "any";
            show();
            if (!item || !item.builtIn)
                nameField.forceActiveFocus();
        }
        function accept() {
            var builtIn = editing && editing.builtIn;
            var name = nameField.text.trim();
            var ports = portField.text.replace(/\s+/g, "");
            if (!builtIn) {
                if (!name) {
                    sheet.messageBox.show("Firewall", "Type a name for the exception.", "info", ["OK"]);
                    return;
                }
                var err = Firewall.checkPorts(ports);
                if (err) {
                    sheet.messageBox.show("Firewall", err, "info", ["OK"]);
                    return;
                }
            }
            var changes = builtIn ? { scope: scope }
                                  : { name: name, scope: scope,
                                      tcp: proto !== "udp" ? ports : "", udp: proto !== "tcp" ? ports : "" };
            if (editing) {
                sheet.setException(editing.key, changes);
            } else {
                var c = sheet.copy(sheet.cfg);
                var n = 1;
                var used = c.allow.map(function (e) { return e.key; });
                while (used.indexOf("port-" + n) >= 0)
                    ++n;
                changes.key = "port-" + n;
                changes.on = true;
                changes.builtIn = false;
                c.allow.push(changes);
                sheet.cfg = c;
                sheet.dirty = true;
                list.selectedKey = changes.key;
            }
            close();
        }

        Label { x: 12; y: 14; text: "<u>N</u>ame:" }
        TextField95 {
            id: nameField
            x: 90; y: 10
            width: parent.width - 102
            enabled: !(editDialog.editing && editDialog.editing.builtIn)
            onAccepted: editDialog.accept()
        }
        Label { x: 12; y: 42; text: "<u>P</u>ort:" }
        TextField95 {
            id: portField
            x: 90; y: 38
            width: parent.width - 102
            visible: !(editDialog.editing && editDialog.editing.builtIn)
            onAccepted: editDialog.accept()
        }
        Label {
            x: 90; y: 42
            width: parent.width - 102
            visible: !portField.visible
            text: editDialog.editing ? sheet.portsText(editDialog.editing) + (editDialog.editing.key === "ssh" ? " (follows SSH Server's port)" : "") : ""
        }
        Row {
            x: 90; y: 66
            spacing: 14
            visible: portField.visible
            RadioButton95 { text: "<u>T</u>CP"; checked: editDialog.proto === "tcp"; onClicked: editDialog.proto = "tcp" }
            RadioButton95 { text: "<u>U</u>DP"; checked: editDialog.proto === "udp"; onClicked: editDialog.proto = "udp" }
            RadioButton95 { text: "<u>B</u>oth"; checked: editDialog.proto === "both"; onClicked: editDialog.proto = "both" }
        }
        Label {
            x: 90; y: 88
            width: parent.width - 102
            visible: portField.visible
            color: Theme.shadow
            text: "A number, a range (8000-8010) or several, with commas."
        }

        GroupBox95 {
            x: 12
            y: portField.visible ? 110 : 66
            width: parent.width - 24
            height: 70
            title: "Let in connections from"
            Column {
                spacing: 4
                RadioButton95 { text: "<u>A</u>ny computer, including on the Internet"; checked: editDialog.scope === "any"; onClicked: editDialog.scope = "any" }
                RadioButton95 { text: "Only <u>m</u>y network (local addresses)"; checked: editDialog.scope === "local"; onClicked: editDialog.scope = "local" }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: editDialog.accept() }
            Button95 { text: "Cancel"; onClicked: editDialog.close() }
        }
    }
}
