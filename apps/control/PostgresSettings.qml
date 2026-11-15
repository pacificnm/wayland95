import QtQuick
import Win95Ui
import Win95Control

// PostgreSQL's settings: who may connect (conf.d/50-wayland95.conf and
// pg_hba.conf, through wayland95-services), and your own account and
// databases (psql, createdb and dropdb as you; only creating the account
// needs an administrator).
PropertySheet {
    id: sheet

    title: "PostgreSQL Properties"
    titles: ["General", "Databases"]

    readonly property var server: Services.services.filter(function (s) { return s.id === "postgresql"; })[0] || ({})
    property var config: ({})
    readonly property string port: String(config.port || 5432)

    property bool applying: false
    property bool closeWhenDone: false
    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    function load() {
        config = Services.postgresConfig();
        portBox.value = config.port || 5432;
        maxBox.value = config.max_connections || 100;
        allRadio.checked = config.listen === "all";
        remoteBox.checked = !!config.remote;
        dirty = false;
    }
    Component.onCompleted: {
        load();
        loadDatabases();
    }

    onApplyRequested: {
        var lines = [
            "listen=" + (allRadio.checked ? "all" : "localhost"),
            "port=" + portBox.value,
            "max_connections=" + maxBox.value,
            "remote=" + (allRadio.checked && remoteBox.checked ? "yes" : "no"),
        ];
        applying = true;
        Services.admin(["pg-config"], lines.join("\n"), function (ok, output) {
            sheet.applying = false;
            Services.refresh();
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("PostgreSQL", output || "The settings couldn't be changed.", "error", ["OK"],
                                      function () { sheet.load(); });
                return;
            }
            sheet.load();
            sheet.loadDatabases();
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
        AppletIcon { x: 4; y: 2; kind: "postgresql" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "PostgreSQL " + (sheet.config.version || "") + " is <b>"
                  + (sheet.server.active ? "running" : sheet.server.enabled ? "stopped" : "off") + "</b>."
                  + (sheet.applying ? " Restarting with the new settings..." : "")
        }

        GroupBox95 {
            id: accessBox
            y: 40
            width: parent.width
            height: 132
            title: "Connections"
            Column {
                width: parent.width
                spacing: 6
                Label { text: "Accept connections from:" }
                RadioButton95 {
                    id: localRadio
                    x: 8
                    text: "<u>T</u>his computer only"
                    checked: !allRadio.checked
                    onClicked: { allRadio.checked = false; sheet.dirty = true; }
                }
                RadioButton95 {
                    id: allRadio
                    x: 8
                    text: "This computer and <u>o</u>ther computers on the network"
                    onClicked: { checked = true; sheet.dirty = true; }
                }
                CheckBox95 {
                    id: remoteBox
                    x: 26
                    enabled: allRadio.checked
                    opacity: enabled ? 1 : 0.5
                    text: "Other computers log on with a <u>p</u>assword"
                    onToggled: sheet.dirty = true
                }
            }
        }

        GroupBox95 {
            id: serverBox
            y: accessBox.y + accessBox.height + 6
            width: parent.width
            height: 80
            title: "Server"
            Grid {
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                verticalItemAlignment: Grid.AlignVCenter
                Label { text: "Port:" }
                SpinBox95 {
                    id: portBox
                    width: 70
                    from: 1
                    to: 65535
                    onValueModified: sheet.dirty = true
                }
                Label { text: "<u>M</u>ost connections at once:" }
                SpinBox95 {
                    id: maxBox
                    width: 70
                    from: 5
                    to: 10000
                    onValueModified: sheet.dirty = true
                }
            }
        }

        Label {
            y: serverBox.y + serverBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: "PostgreSQL restarts when you click Apply, which disconnects any program using it. "
                  + "Programs on this computer log on as you without a password."
        }
    }

    // ---------------------------------------------------------- Databases
    Item {
        id: dbPage
        // "loading", "none" (no account), "ok", or "error"
        property string view: "loading"
        property string error: ""
        property var databases: []

        // no account yet
        Item {
            anchors.fill: parent
            visible: dbPage.view === "none"
            AppletIcon { x: 4; y: 2; kind: "passwords" }
            Label {
                x: 46
                y: 2
                width: parent.width - x
                text: "You don't have a PostgreSQL account yet. With one, you can make your own databases, "
                      + "and programs you run can use them without a password."
            }
            CheckBox95 {
                id: superBox
                x: 46
                y: 70
                text: "Make me a database <u>a</u>dministrator (superuser)"
            }
            Button95 {
                x: 46
                y: 100
                width: 160
                text: "<u>C</u>reate My Account"
                onClicked: {
                    var args = ["pg-role", Services.userName];
                    if (superBox.checked)
                        args.push("superuser");
                    Services.admin(args, "", function (ok, output) {
                        if (!ok)
                            sheet.messageBox.show("PostgreSQL", output, "error", ["OK"]);
                        sheet.loadDatabases();
                    });
                }
            }
        }

        Label {
            visible: dbPage.view === "loading" || dbPage.view === "error"
            width: parent.width
            text: dbPage.view === "loading" ? "Looking at the databases..."
                  : "PostgreSQL didn't answer" + (sheet.server.active ? "" : " (it isn't running)") + ":\n\n" + dbPage.error
        }

        // the databases
        Item {
            anchors.fill: parent
            visible: dbPage.view === "ok"
            Label {
                width: parent.width
                text: "Databases you can use, as <b>" + Services.userName + "</b>:"
            }
            ProgramList {
                id: dbList
                y: 20
                width: parent.width
                height: 190
                model: dbPage.databases
                iconComponent: AppletIcon { size: parent.size; kind: "database" }
                noteFor: function (item) { return item.size + (item.owner !== Services.userName ? "  (" + item.owner + ")" : ""); }
                onActivated: (item) => Services.openTerminal(Services.homePath, ["psql", "-p", sheet.port, item.key])
            }
            Row {
                id: dbButtons
                y: dbList.y + dbList.height + 8
                anchors.right: parent.right
                spacing: 6
                Button95 {
                    text: "<u>N</u>ew..."
                    onClicked: newDbDialog.open()
                }
                Button95 {
                    text: "<u>D</u>elete"
                    enabled: !!dbList.selected && dbList.selected.owner === Services.userName
                    onClicked: {
                        var name = dbList.selected.key;
                        sheet.messageBox.show("PostgreSQL", "Delete the database \"" + name + "\" and everything in it?\n\nThis can't be undone.",
                                              "warning", ["Yes", "No"], function (b) {
                            if (b !== 0)
                                return;
                            Services.run("dropdb", ["-p", sheet.port, "--", name], "", function (code, out, err) {
                                if (code !== 0)
                                    sheet.messageBox.show("PostgreSQL", err.trim() || "The database couldn't be deleted.", "error", ["OK"]);
                                sheet.loadDatabases();
                            });
                        });
                    }
                }
                Button95 {
                    width: 100
                    text: "Open in <u>p</u>sql"
                    enabled: !!dbList.selected
                    onClicked: Services.openTerminal(Services.homePath, ["psql", "-p", sheet.port, dbList.selected.key])
                }
            }
            Button95 {
                anchors.left: parent.left
                y: dbButtons.y
                width: 120
                text: "<u>S</u>et Password..."
                onClicked: passwordDialog.open()
            }
            Label {
                anchors.bottom: parent.bottom
                width: parent.width
                color: Theme.shadow
                text: "Your password is only for connections over the network (Set Password); on this computer you log on as yourself."
            }
        }
    }

    function sql(query, done) {
        Services.run("psql", ["-X", "-q", "-At", "-F", "|", "-p", port, "-d", "postgres", "-v", "ON_ERROR_STOP=1"], query + "\n", done);
    }
    function loadDatabases() {
        if (!sheet.server.installed) {
            dbPage.view = "error";
            dbPage.error = "PostgreSQL isn't installed.";
            return;
        }
        dbPage.view = "loading";
        sql("SELECT datname, pg_get_userbyid(datdba), "
            + "CASE WHEN has_database_privilege(datname, 'CONNECT') THEN pg_size_pretty(pg_database_size(datname)) ELSE '' END "
            + "FROM pg_database WHERE NOT datistemplate ORDER BY 1;", function (code, out, err) {
            if (code !== 0) {
                if (/role ".*" does not exist/.test(err)) {
                    dbPage.view = "none";
                } else {
                    dbPage.view = "error";
                    dbPage.error = err.trim() || "psql couldn't be run.";
                }
                return;
            }
            dbPage.databases = out.split("\n").filter(function (l) { return l.length > 0; }).map(function (l) {
                var f = l.split("|");
                return { key: f[0], name: f[0], owner: f[1], size: f[2] };
            });
            dbPage.view = "ok";
        });
    }
    // a value for psql's \set: single quotes, '' and \\ escaped
    function psqlQuote(s) {
        return "'" + s.replace(/\\/g, "\\\\").replace(/'/g, "''") + "'";
    }

    extras: [
        Dialog95 {
            id: newDbDialog
            title: "New Database"
            dialogWidth: 300
            dialogHeight: 112

            function open() {
                nameField.text = "";
                show();
                nameField.forceActiveFocus();
            }
            function ok() {
                var name = nameField.text.trim();
                if (!/^[A-Za-z_][A-Za-z0-9_$-]*$/.test(name)) {
                    sheet.messageBox.show("PostgreSQL", "Use letters, digits and _ for the name, starting with a letter.", "error", ["OK"],
                                          function () { nameField.forceActiveFocus(); });
                    return;
                }
                newDbDialog.close();
                Services.run("createdb", ["-p", sheet.port, "--", name], "", function (code, out, err) {
                    if (code !== 0)
                        sheet.messageBox.show("PostgreSQL", err.trim() || "The database couldn't be made.", "error", ["OK"]);
                    sheet.loadDatabases();
                });
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
                x: 60
                y: 12
                width: 226
                onAccepted: newDbDialog.ok()
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
                    onClicked: newDbDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: newDbDialog.close()
                }
            }
        },
        Dialog95 {
            id: passwordDialog
            title: "Set PostgreSQL Password"
            dialogWidth: 320
            dialogHeight: 140

            function open() {
                pw1.text = "";
                pw2.text = "";
                show();
                pw1.forceActiveFocus();
            }
            function ok() {
                if (pw1.text !== pw2.text) {
                    sheet.messageBox.show("PostgreSQL", "The passwords don't match. Type them again.", "error", ["OK"], function () {
                        pw1.text = ""; pw2.text = ""; pw1.forceActiveFocus();
                    });
                    return;
                }
                passwordDialog.close();
                sheet.sql("\\set pw " + sheet.psqlQuote(pw1.text) + "\nALTER ROLE CURRENT_USER PASSWORD :'pw';", function (code, out, err) {
                    sheet.messageBox.show("PostgreSQL", code === 0 ? "Your PostgreSQL password has been set." : (err.trim() || "The password couldn't be set."),
                                          code === 0 ? "info" : "error", ["OK"]);
                });
                pw1.text = "";
                pw2.text = "";
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
                Text { text: "<u>P</u>assword:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw1; onAccepted: pw2.forceActiveFocus() }
                Text { text: "<u>C</u>onfirm password:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw2; onAccepted: passwordDialog.ok() }
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
                    enabled: pw1.text.length > 0
                    onClicked: passwordDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: passwordDialog.close()
                }
            }
        }
    ]
}
