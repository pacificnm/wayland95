import QtQuick
import Win95Ui
import Win95Control

// Node.js's settings: where npm puts global packages (~/.npmrc's prefix,
// so they can go in your home folder without a password), and the global
// packages themselves (npm install -g, as you).
PropertySheet {
    id: sheet

    title: "Node.js Properties"
    titles: ["General", "Global Packages"]

    readonly property var lang: Services.services.filter(function (s) { return s.id === "nodejs"; })[0] || ({})
    readonly property string npm: Services.exists("/usr/bin/npm") ? "/usr/bin/npm" : "npm"
    readonly property string homePrefix: Services.homePath + "/.npm-global"
    property string npmVersion: ""
    property string prefix: ""

    function load() {
        Services.run(npm, ["--version"], "", function (code, out) { sheet.npmVersion = code === 0 ? out.trim() : ""; });
        Services.run(npm, ["config", "get", "prefix"], "", function (code, out) {
            sheet.prefix = code === 0 ? out.trim() : "";
            homeRadio.checked = sheet.prefix === sheet.homePrefix;
            sheet.dirty = false;
            sheet.loadPackages();
        });
    }
    Component.onCompleted: load()

    onApplyRequested: {
        // ~/.npmrc: our prefix line, the user's other settings kept
        var lines = Services.readFile("~/.npmrc").split("\n").filter(function (l) {
            return l.length > 0 && !/^\s*prefix\s*=/.test(l);
        });
        if (homeRadio.checked)
            lines.push("prefix=" + homePrefix);
        var ok = Services.writeFile("~/.npmrc", lines.length ? lines.join("\n") + "\n" : "");
        if (homeRadio.checked) {
            // its programs on the PATH, from the next logon
            var profile = Services.readFile("~/.profile");
            if (profile.indexOf("# npm global packages (Wayland 95)") < 0)
                ok = Services.writeFile("~/.profile", profile + (profile.length && !profile.endsWith("\n") ? "\n" : "")
                                        + "\n# npm global packages (Wayland 95)\n"
                                        + "[ -d \"$HOME/.npm-global/bin\" ] && PATH=\"$HOME/.npm-global/bin:$PATH\"\n") && ok;
        }
        if (!ok)
            messageBox.show("Node.js", "Your npm settings couldn't be saved.", "error", ["OK"]);
        load();
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "nodejs" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "Node.js " + (sheet.lang.version || "") + " is installed, as <b>node</b>."
        }
        Label {
            x: 46
            y: 20
            width: parent.width - x
            color: Theme.shadow
            text: sheet.npmVersion ? "npm " + sheet.npmVersion : "npm isn't installed."
        }

        GroupBox95 {
            id: prefixBox
            y: 50
            width: parent.width
            height: 118
            title: "Global packages (npm install -g)"
            Column {
                width: parent.width
                spacing: 6
                RadioButton95 {
                    id: systemRadio
                    text: "For everyone, in the <u>s</u>ystem's folders (needs sudo)"
                    checked: !homeRadio.checked
                    onClicked: { homeRadio.checked = false; sheet.dirty = true; }
                }
                RadioButton95 {
                    id: homeRadio
                    text: "Just for me, in my <u>h</u>ome folder (no password needed)"
                    onClicked: { checked = true; sheet.dirty = true; }
                }
                Label {
                    x: 18
                    width: parent.width - x
                    color: Theme.shadow
                    text: "Now: " + (sheet.prefix || "?")
                }
            }
        }
        Label {
            y: prefixBox.y + prefixBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: "Choosing your home folder sets npm's prefix in ~/.npmrc to ~/.npm-global, and adds its programs to your "
                  + "PATH from the next time you log on."
        }
    }

    // ---------------------------------------------------- Global Packages
    Item {
        id: pkgPage
        property var packages: []
        property string state95: ""   // "", "loading" or an error
        property string working: ""

        Label {
            width: parent.width
            text: "Packages installed with npm install -g, in " + (sheet.prefix || "?") + ":"
        }
        ProgramList {
            id: pkgList
            y: 34
            width: parent.width
            height: 200
            model: pkgPage.packages
            iconComponent: AppletIcon { size: parent.size; kind: "nodejs" }
            noteFor: function (item) { return item.version; }
            emptyText: pkgPage.state95 === "loading" ? "Looking..." : pkgPage.state95 || "No global packages."
        }
        Label {
            y: pkgList.y + pkgList.height + 10
            width: parent.width - pkgButtons.width - 8
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            text: pkgPage.working
        }
        Row {
            id: pkgButtons
            y: pkgList.y + pkgList.height + 6
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>I</u>nstall..."
                enabled: !pkgPage.working && !!sheet.npmVersion
                onClicked: installDialog.open()
            }
            Button95 {
                text: "<u>R</u>emove"
                enabled: !pkgPage.working && !!pkgList.selected && pkgList.selected.name !== "npm"
                onClicked: sheet.npmGlobal(["uninstall", "-g", pkgList.selected.name], "Removing " + pkgList.selected.name + "...")
            }
        }
    }

    function loadPackages() {
        pkgPage.state95 = "loading";
        Services.run(npm, ["ls", "-g", "--depth=0", "--json"], "", function (code, out, err) {
            try {
                var deps = JSON.parse(out).dependencies || {};
                pkgPage.packages = Object.keys(deps).sort().map(function (n) {
                    return { key: n, name: n, version: deps[n].version || "" };
                });
                pkgPage.state95 = "";
            } catch (e) {
                pkgPage.packages = [];
                pkgPage.state95 = err.trim() || "npm didn't answer.";
            }
        });
    }
    function npmGlobal(args, doing) {
        pkgPage.working = doing;
        Services.run(npm, args, "", function (code, out, err) {
            pkgPage.working = "";
            if (code !== 0) {
                var msg = /EACCES|permission denied/i.test(err)
                        ? "npm isn't allowed to write to " + sheet.prefix + ". On the General tab, choose \"Just for me, in my home folder\", then try again."
                        : err.trim().split("\n").filter(function (l) { return /ERR!/.test(l); }).slice(0, 4).join("\n") || err.trim();
                sheet.messageBox.show("Node.js", msg || "npm couldn't do it.", "error", ["OK"]);
            }
            sheet.loadPackages();
        });
    }

    extras: [
        Dialog95 {
            id: installDialog
            title: "Install Global Package"
            dialogWidth: 330
            dialogHeight: 120

            function open() {
                pkgName.text = "";
                show();
                pkgName.forceActiveFocus();
            }
            function ok() {
                var specs = pkgName.text.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
                if (!specs.length || specs.some(function (s) { return s.charAt(0) === "-"; })) {
                    pkgName.forceActiveFocus();
                    return;
                }
                installDialog.close();
                sheet.npmGlobal(["install", "-g"].concat(specs), "Installing " + specs.join(", ") + "...");
            }
            Text {
                x: 12
                y: 12
                width: 306
                wrapMode: Text.Wrap
                text: "Package names from npm, separated by spaces (for example: typescript nodemon):"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            TextField95 {
                id: pkgName
                x: 12
                y: 46
                width: 306
                onAccepted: installDialog.ok()
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
                    enabled: pkgName.text.trim().length > 0
                    onClicked: installDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: installDialog.close()
                }
            }
        }
    ]
}
