import QtQuick
import Win95Ui
import Win95Control

// nginx's settings: who may connect and the port (General), and the folder
// its pages come from (Website): /var/www/html (which can be made yours to
// edit) or a folder in your home, which nginx is then let into, read-only.
// Written by wayland95-services (nginx-config, www-own) as its own site,
// in place of Debian's default one.
PropertySheet {
    id: sheet

    title: "nginx Properties"
    titles: ["General", "Website"]

    readonly property var server: Services.services.filter(function (s) { return s.id === "nginx"; })[0] || ({})
    property var config: ({})
    readonly property int port: config.port || 80
    readonly property string url: "http://localhost" + (port === 80 ? "" : ":" + port) + "/"

    property bool applying: false
    property bool closeWhenDone: false
    onClosing: (close) => {
        if (applying) {
            close.accepted = false;
            closeWhenDone = true;
        }
    }

    function load() {
        config = Services.nginxConfig();
        portBox.value = config.port || 80;
        allRadio.checked = config.listen !== "localhost";
        rootField.text = config.root || "/var/www/html";
        listBox.checked = !!config.autoindex;
        dirty = false;
    }
    Component.onCompleted: load()

    onApplyRequested: {
        var root = rootField.text.trim().replace(/\/+$/, "") || "/";
        if (!Services.exists(root)) {
            messageBox.show("nginx", "There's no folder " + root + ".", "error", ["OK"]);
            return;
        }
        var lines = [
            "listen=" + (allRadio.checked ? "all" : "localhost"),
            "port=" + portBox.value,
            "root=" + root,
            "autoindex=" + (listBox.checked ? "yes" : "no"),
        ];
        applying = true;
        Services.admin(["nginx-config"], lines.join("\n"), function (ok, output) {
            sheet.applying = false;
            Services.refresh();
            if (!ok) {
                sheet.closeWhenDone = false;
                sheet.messageBox.show("nginx", output || "The settings couldn't be changed.", "error", ["OK"],
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
        AppletIcon { x: 4; y: 2; kind: "nginx" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "nginx" + (sheet.config.version ? " " + sheet.config.version : "") + " is <b>"
                  + (sheet.server.active ? "running" : sheet.server.enabled ? "stopped" : "off") + "</b>."
                  + (sheet.applying ? " Reloading with the new settings..." : "")
        }
        Label {
            x: 46
            y: 20
            width: parent.width - x
            visible: !!sheet.server.active
            text: "Your website is at <b>" + sheet.url + "</b>"
        }
        Button95 {
            x: 46
            y: 40
            width: 150
            text: "<u>O</u>pen in Web Browser"
            enabled: !!sheet.server.active
            onClicked: Services.openUrl(sheet.url)
        }

        GroupBox95 {
            id: accessBox
            y: 74
            width: parent.width
            height: 96
            title: "Connections"
            Column {
                width: parent.width
                spacing: 6
                Label { text: "Show the website to:" }
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
            height: 54
            title: "Server"
            Row {
                spacing: 10
                Label { anchors.verticalCenter: parent.verticalCenter; text: "<u>P</u>ort:" }
                SpinBox95 {
                    id: portBox
                    width: 70
                    from: 1
                    to: 65535
                    onValueModified: sheet.dirty = true
                }
                Label { anchors.verticalCenter: parent.verticalCenter; color: Theme.shadow; text: "(80 is the web's usual one)" }
            }
        }
        Label {
            y: serverBox.y + serverBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: allRadio.checked && Services.addresses().length
                  ? "Other computers use " + Services.addresses().map(function (a) {
                        return "http://" + (a.indexOf(":") >= 0 ? "[" + a + "]" : a) + (sheet.port === 80 ? "" : ":" + sheet.port) + "/";
                    }).join(" or ") + "."
                  : "nginx reloads when you click Apply; pages being shown carry on."
        }
    }

    // ------------------------------------------------------------ Website
    Item {
        Label { width: parent.width; text: "nginx shows the pages in this <u>f</u>older:" }
        TextField95 {
            id: rootField
            objectName: "nginxRoot"
            y: 20
            width: parent.width - 82
            input.onTextEdited: sheet.dirty = true
        }
        Button95 {
            y: 20
            anchors.right: parent.right
            width: 74
            text: "<u>B</u>rowse..."
            onClicked: folderDialog.openAt(rootField.text || Services.homePath, "")
        }
        Row {
            y: 50
            spacing: 6
            Button95 {
                width: 150
                text: "Use <u>m</u>y Website folder"
                onClicked: { rootField.text = Services.makeWebsite(); sheet.dirty = true; }
            }
            Button95 {
                width: 100
                text: "Open Fol<u>d</u>er"
                enabled: Services.exists(rootField.text)
                onClicked: Services.run("win95files", [rootField.text], "", function () {})
            }
        }
        CheckBox95 {
            id: listBox
            y: 84
            text: "<u>L</u>ist a folder's files when it has no index.html"
            onToggled: sheet.dirty = true
        }
        GroupBox95 {
            y: 112
            width: parent.width
            height: 132
            title: "About the folder"
            Label {
                width: parent.width
                text: {
                    var r = rootField.text;
                    var home = Services.homePath;
                    if (r === sheet.config.root && r === "/var/www/html" && !sheet.config.rootWritable)
                        return "/var/www/html is nginx's own folder, and only an administrator can change "
                               + "what's in it. Make it yours to put your pages there.";
                    if (r.indexOf(home + "/") === 0)
                        return "A folder in your home: nginx is let into it to read it (and only it), when you "
                               + "click Apply. Files whose names start with a dot are never shown.";
                    if (r === home)
                        return "Choose a folder in your home, not all of it.";
                    return "Put index.html in the folder: it's the page people see first. "
                           + "Files whose names start with a dot are never shown.";
                }
            }
            Button95 {
                y: 66
                width: 140
                visible: rootField.text === "/var/www/html" && sheet.config.root === "/var/www/html" && !sheet.config.rootWritable
                text: "Make It <u>Y</u>ours"
                onClicked: Services.admin(["www-own"], "", function (ok, output) {
                    if (!ok)
                        sheet.messageBox.show("nginx", output || "It couldn't be changed.", "error", ["OK"]);
                    sheet.load();
                })
            }
        }
    }

    extras: [
        FileDialog95 {
            id: folderDialog
            mode: "folder"
            messageBox: sheet.messageBox
            popupMenu: sheet.popupMenu
            onAccepted: (path) => { rootField.text = path; sheet.dirty = true; }
        }
    ]
}
