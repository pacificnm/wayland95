import QtQuick
import Win95Ui
import Win95Control

// MySQL's (MariaDB's) settings: who may connect (mariadb.conf.d/
// 99-wayland95.cnf, through wayland95-services), and your own account and
// databases (the mariadb command as you, through its socket: no password on
// this computer; making the account and a network password need an
// administrator).
PropertySheet {
    id: sheet

    title: "MySQL Properties"
    titles: ["General", "Databases"]

    readonly property var server: Services.services.filter(function (s) { return s.id === "mysql"; })[0] || ({})
    property var config: ({})

    property bool applying: false
    property bool closeWhenDone: false
    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    function load() {
        config = Services.mysqlConfig();
        portBox.value = config.port || 3306;
        maxBox.value = config.max_connections || 151;
        allRadio.checked = config.listen === "all";
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
        ];
        applying = true;
        Services.admin(["my-config"], lines.join("\n"), function (ok, output) {
            sheet.applying = false;
            Services.refresh();
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("MySQL", output || "The settings couldn't be changed.", "error", ["OK"],
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
        AppletIcon { x: 4; y: 2; kind: "mysql" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "MySQL (MariaDB" + (sheet.config.version ? " " + sheet.config.version : "") + ") is <b>"
                  + (sheet.server.active ? "running" : sheet.server.enabled ? "stopped" : "off") + "</b>."
                  + (sheet.applying ? " Restarting with the new settings..." : "")
        }

        GroupBox95 {
            id: accessBox
            y: 40
            width: parent.width
            height: 96
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
                    to: 100000
                    onValueModified: sheet.dirty = true
                }
            }
        }

        Label {
            y: serverBox.y + serverBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: "MySQL restarts when you click Apply, which disconnects any program using it. "
                  + "Programs on this computer log on as you without a password; other computers "
                  + "use the network password you set on the Databases page."
        }
    }

    // ---------------------------------------------------------- Databases
    Item {
        id: dbPage
        // "loading", "none" (no account), "ok", or "error"
        property string view: "loading"
        property string error: ""
        property var databases: []
        property bool admin: false

        // no account yet
        Item {
            anchors.fill: parent
            visible: dbPage.view === "none"
            AppletIcon { x: 4; y: 2; kind: "passwords" }
            Label {
                x: 46
                y: 2
                width: parent.width - x
                text: "You don't have a MySQL account yet. With one, you can make your own databases, "
                      + "and programs you run can use them without a password."
            }
            CheckBox95 {
                id: adminBox
                x: 46
                y: 70
                text: "Make me a database <u>a</u>dministrator (all databases)"
            }
            Button95 {
                x: 46
                y: 100
                width: 160
                text: "<u>C</u>reate My Account"
                onClicked: {
                    var args = ["my-role", Services.userName];
                    if (adminBox.checked)
                        args.push("admin");
                    Services.admin(args, "", function (ok, output) {
                        if (!ok)
                            sheet.messageBox.show("MySQL", output, "error", ["OK"]);
                        sheet.loadDatabases();
                    });
                }
            }
        }

        Label {
            visible: dbPage.view === "loading" || dbPage.view === "error"
            width: parent.width
            text: dbPage.view === "loading" ? "Looking at the databases..."
                  : "MySQL didn't answer" + (sheet.server.active ? "" : " (it isn't running)") + ":\n\n" + dbPage.error
        }

        // the databases
        Item {
            anchors.fill: parent
            visible: dbPage.view === "ok"
            Label {
                width: parent.width
                text: "Databases you can use, as <b>" + Services.userName + "</b>"
                      + (dbPage.admin ? " (an administrator):" : ":")
            }
            ProgramList {
                id: dbList
                y: 20
                width: parent.width
                height: 180
                model: dbPage.databases
                iconComponent: AppletIcon { size: parent.size; kind: "database" }
                noteFor: function (item) { return item.size; }
                onActivated: (item) => Services.openTerminal(Services.homePath, ["mariadb", item.key])
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
                    enabled: !!dbList.selected
                    onClicked: {
                        var name = dbList.selected.key;
                        sheet.messageBox.show("MySQL", "Delete the database \"" + name + "\" and everything in it?\n\nThis can't be undone.",
                                              "warning", ["Yes", "No"], function (b) {
                            if (b !== 0)
                                return;
                            sheet.sql("DROP DATABASE " + sheet.ident(name) + ";", function (code, out, err) {
                                if (code !== 0)
                                    sheet.messageBox.show("MySQL", sheet.errorText(err) || "The database couldn't be deleted.", "error", ["OK"]);
                                sheet.loadDatabases();
                            });
                        });
                    }
                }
                Button95 {
                    width: 110
                    text: "Open in <u>m</u>ariadb"
                    enabled: !!dbList.selected
                    onClicked: Services.openTerminal(Services.homePath, ["mariadb", dbList.selected.key])
                }
            }
            Button95 {
                anchors.left: parent.left
                y: dbButtons.y + 32
                width: 160
                text: "<u>S</u>et Network Password..."
                onClicked: passwordDialog.open()
            }
            Label {
                anchors.bottom: parent.bottom
                width: parent.width
                color: Theme.shadow
                text: dbPage.admin ? "The network password is for other computers (when MySQL accepts them, on the General page)."
                      : "Your databases' names start with " + Services.userName + "_ (or are just " + Services.userName + ")."
            }
        }
    }

    // as you, through the socket: done(code, out, err)
    function sql(query, done) {
        Services.run("mariadb", ["-N", "-B"], query + "\n", done);
    }
    function ident(name) { return "`" + name.replace(/`/g, "``") + "`"; }
    function errorText(err) { return err.trim().replace(/^ERROR \d+ \([0-9A-Z]+\)( at line \d+)?: /, ""); }
    function loadDatabases() {
        if (!sheet.server.installed) {
            dbPage.view = "error";
            dbPage.error = "MySQL isn't installed.";
            return;
        }
        dbPage.view = "loading";
        sql("SELECT COUNT(*) FROM information_schema.USER_PRIVILEGES WHERE PRIVILEGE_TYPE = 'CREATE USER';\n"
            + "SELECT s.schema_name, IFNULL(SUM(t.data_length + t.index_length), 0) FROM information_schema.schemata s "
            + "LEFT JOIN information_schema.tables t ON t.table_schema = s.schema_name "
            + "WHERE s.schema_name NOT IN ('information_schema', 'performance_schema', 'mysql', 'sys') "
            + "GROUP BY s.schema_name ORDER BY 1;", function (code, out, err) {
            if (code !== 0) {
                if (/ERROR 1698|ERROR 1045|Access denied for user/.test(err)) {
                    dbPage.view = "none";
                } else {
                    dbPage.view = "error";
                    dbPage.error = sheet.errorText(err) || "The mariadb command couldn't be run.";
                }
                return;
            }
            var lines = out.split("\n").filter(function (l) { return l.length > 0; });
            dbPage.admin = lines.length > 0 && lines[0] !== "0";
            dbPage.databases = lines.slice(1).map(function (l) {
                var f = l.split("\t");
                return { key: f[0], name: f[0], size: sheet.sizeText(Number(f[1])) };
            });
            dbPage.view = "ok";
        });
    }
    function sizeText(n) {
        if (!(n > 0)) return "empty";
        if (n < 1024 * 1024) return Math.max(1, Math.round(n / 1024)) + " KB";
        if (n < 1024 * 1024 * 1024) return (n / 1048576).toFixed(1) + " MB";
        return (n / 1073741824).toFixed(2) + " GB";
    }

    extras: [
        Dialog95 {
            id: newDbDialog
            title: "New Database"
            dialogWidth: 300
            dialogHeight: 112

            function open() {
                nameField.text = dbPage.admin ? "" : Services.userName + "_";
                show();
                nameField.forceActiveFocus();
                nameField.input.cursorPosition = nameField.text.length;
            }
            function ok() {
                var name = nameField.text.trim();
                if (!/^[A-Za-z0-9_]+$/.test(name) || name.length > 64) {
                    sheet.messageBox.show("MySQL", "Use letters, digits and _ for the name (64 at most).", "error", ["OK"],
                                          function () { nameField.forceActiveFocus(); });
                    return;
                }
                newDbDialog.close();
                sheet.sql("CREATE DATABASE " + sheet.ident(name) + ";", function (code, out, err) {
                    if (code !== 0)
                        sheet.messageBox.show("MySQL", (sheet.errorText(err) || "The database couldn't be made.")
                                              + (/denied/i.test(err) && !dbPage.admin ? "\n\nIts name needs to start with " + Services.userName + "_." : ""),
                                              "error", ["OK"]);
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
            title: "Set MySQL Network Password"
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
                    sheet.messageBox.show("MySQL", "The passwords don't match. Type them again.", "error", ["OK"], function () {
                        pw1.text = ""; pw2.text = ""; pw1.forceActiveFocus();
                    });
                    return;
                }
                var pw = pw1.text;
                passwordDialog.close();
                pw1.text = "";
                pw2.text = "";
                Services.admin(["my-password", Services.userName], pw, function (ok, output) {
                    sheet.messageBox.show("MySQL", !ok ? (output || "The password couldn't be set.")
                                          : pw ? "Your MySQL network password has been set. Other computers log on as "
                                                 + Services.userName + " with it."
                                          : "Network logons are off: only this computer can use your account.",
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
                Text { text: "<u>P</u>assword:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw1; onAccepted: pw2.forceActiveFocus() }
                Text { text: "<u>C</u>onfirm password:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
                PasswordField { id: pw2; onAccepted: passwordDialog.ok() }
            }
            Text {
                x: 12
                y: 80
                width: parent.width - 24
                wrapMode: Text.Wrap
                text: "Leave both empty to stop logons from other computers."
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
        }
    ]
}
