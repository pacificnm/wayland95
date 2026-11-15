import QtQuick
import Win95Ui
import Win95Control

// Python's settings: which extra parts are installed (packages), and
// virtual environments in ~/.virtualenvs with their packages (pip, as you:
// Debian keeps pip away from the system's own Python, so packages for your
// projects go in an environment).
PropertySheet {
    id: sheet

    title: "Python Properties"
    titles: ["General", "Environments"]

    readonly property var lang: Services.services.filter(function (s) { return s.id === "python"; })[0] || ({})
    readonly property var components: [
        { key: "python3-pip", name: "pip, to install packages" },
        { key: "python3-venv", name: "Virtual environments (venv)" },
        { key: "python3-dev", name: "Headers, to build packages written in C" },
        { key: "python3-tk", name: "Tkinter, for programs with windows" },
        { key: "idle3", name: "IDLE, Python's own editor" },
    ]
    property var installed: ({})
    property bool startedHere: false
    property var wanted: ({})
    readonly property string envRoot: Services.homePath + "/.virtualenvs"

    function loadComponents() {
        Services.installedPackages(components.map(function (c) { return c.key; }), function (names) {
            var m = {};
            names.forEach(function (n) { m[n] = true; });
            sheet.installed = m;
            sheet.wanted = Object.assign({}, m);
            sheet.dirty = false;
        });
    }
    Component.onCompleted: {
        loadComponents();
        loadEnvs();
    }

    onApplyRequested: {
        var add = [], remove = [];
        components.forEach(function (c) {
            if (wanted[c.key] && !installed[c.key]) add.push(c.key);
            if (!wanted[c.key] && installed[c.key]) remove.push(c.key);
        });
        if (add.length || remove.length) {
            startedHere = true;
            Services.changePackages("Changing Python's components", add, remove);
        }
    }
    Connections {
        target: Services
        function onTaskFinished(ok, message) {
            if (!ok && sheet.startedHere)
                sheet.messageBox.show("Python", message, "error", ["OK"]);
            sheet.startedHere = false;
            sheet.loadComponents();
        }
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "python" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "Python " + (sheet.lang.version || "") + " is installed, as <b>python3</b>."
        }
        Label {
            x: 46
            y: 20
            width: parent.width - x
            color: Theme.shadow
            text: "/usr/bin/python3"
        }

        Label {
            y: 50
            text: "<u>C</u>omponents (check the ones you want):"
        }
        ProgramList {
            id: compList
            y: 66
            width: parent.width
            height: 112
            model: sheet.components
            checkable: true
            checked: sheet.wanted
            iconComponent: AppletIcon { size: parent.size; kind: "python" }
            noteFor: function (item) { return item.key; }
            onCheckToggled: (key, on) => {
                var w = Object.assign({}, sheet.wanted);
                if (on) w[key] = true; else delete w[key];
                sheet.wanted = w;
                sheet.dirty = true;
            }
        }

        Item {
            y: compList.y + compList.height + 10
            width: parent.width
            height: 60
            visible: Services.busy
            Label { id: taskText; width: parent.width; text: Services.task + ": " + Services.status }
            ProgressBar95 { y: taskText.height + 4; width: parent.width; value: Services.percent }
        }
        Label {
            y: compList.y + compList.height + 10
            width: parent.width
            visible: !Services.busy
            color: Theme.shadow
            text: "Click Apply to install or remove the components you changed."
        }
    }

    // ------------------------------------------------------- Environments
    Item {
        id: envPage
        property var envs: []
        property var packages: []
        property string packagesState: ""  // "", "loading", or an error
        property string working: ""        // what pip is doing now

        Label {
            width: parent.width
            text: "Virtual <u>e</u>nvironments, in ~/.virtualenvs:"
        }
        ProgramList {
            id: envList
            y: 16
            width: parent.width
            height: 86
            model: envPage.envs
            iconComponent: AppletIcon { size: parent.size; kind: "folder" }
            noteFor: function (item) { return item.version; }
            emptyText: "None yet. Click New to make one."
            onSelectedKeyChanged: sheet.loadPackages()
            onActivated: (item) => sheet.openTerminal(item)
        }
        Row {
            id: envButtons
            y: envList.y + envList.height + 6
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>N</u>ew..."
                onClicked: newEnvDialog.open()
            }
            Button95 {
                text: "<u>D</u>elete"
                enabled: !!envList.selected && !envPage.working
                onClicked: {
                    var env = envList.selected;
                    sheet.messageBox.show("Python", "Delete the environment \"" + env.name + "\" and the packages in it?",
                                          "question", ["Yes", "No"], function (b) {
                        if (b !== 0)
                            return;
                        if (!Services.removeDir(sheet.envRoot + "/" + env.name))
                            sheet.messageBox.show("Python", "The environment couldn't be deleted.", "error", ["OK"]);
                        sheet.loadEnvs();
                    });
                }
            }
            Button95 {
                width: 100
                text: "Open <u>T</u>erminal"
                enabled: !!envList.selected
                onClicked: sheet.openTerminal(envList.selected)
            }
        }

        Label {
            y: envButtons.y + 32
            text: envList.selected ? "<u>P</u>ackages in " + envList.selected.name + ":" : "Packages:"
        }
        ProgramList {
            id: pkgList
            y: envButtons.y + 48
            width: parent.width
            height: 96
            model: envPage.packages
            iconComponent: AppletIcon { size: parent.size; kind: "programs" }
            noteFor: function (item) { return item.version; }
            emptyText: !envList.selected ? "" : envPage.packagesState === "loading" ? "Looking..." : envPage.packagesState
        }
        Label {
            y: pkgList.y + pkgList.height + 10
            width: parent.width - pkgButtons.width - 8
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            text: envPage.working
        }
        Row {
            id: pkgButtons
            y: pkgList.y + pkgList.height + 6
            anchors.right: parent.right
            spacing: 6
            Button95 {
                text: "<u>I</u>nstall..."
                enabled: !!envList.selected && !envPage.working
                onClicked: installDialog.open()
            }
            Button95 {
                text: "<u>R</u>emove"
                enabled: !!pkgList.selected && !envPage.working
                onClicked: sheet.pip(["uninstall", "-y", pkgList.selected.name], "Removing " + pkgList.selected.name + "...")
            }
        }
    }

    function loadEnvs() {
        var names = Services.listDirs(envRoot).filter(function (n) { return Services.exists(envRoot + "/" + n + "/bin/python"); });
        var keep = envList.selectedKey;
        envPage.envs = names.map(function (n) {
            // pyvenv.cfg: "version = 3.13.5" (older: version_info)
            var cfg = Services.readFile(envRoot + "/" + n + "/pyvenv.cfg");
            var m = cfg.match(/^version(?:_info)?\s*=\s*(\S+)/m);
            return { key: n, name: n, version: m ? "Python " + m[1] : "" };
        });
        envList.selectedKey = names.indexOf(keep) >= 0 ? keep : (names.length ? names[0] : "");
        loadPackages();
    }
    function loadPackages() {
        envPage.packages = [];
        var env = envList.selected;
        if (!env)
            return;
        envPage.packagesState = "loading";
        Services.run(envRoot + "/" + env.name + "/bin/python", ["-m", "pip", "list", "--format=json", "--disable-pip-version-check"], "",
                     function (code, out, err) {
            if (!envList.selected || envList.selected.name !== env.name)
                return;
            if (code !== 0) {
                envPage.packagesState = /No module named pip/.test(err) ? "This environment has no pip." : (err.trim() || "pip didn't answer.");
                return;
            }
            try {
                envPage.packages = JSON.parse(out).map(function (p) { return { key: p.name, name: p.name, version: p.version }; });
                envPage.packagesState = envPage.packages.length ? "" : "No packages.";
            } catch (e) {
                envPage.packagesState = "pip's answer couldn't be read.";
            }
        });
    }
    function pip(args, doing) {
        var env = envList.selected;
        envPage.working = doing;
        Services.run(envRoot + "/" + env.name + "/bin/python", ["-m", "pip"].concat(args).concat(["--disable-pip-version-check"]), "",
                     function (code, out, err) {
            envPage.working = "";
            if (code !== 0) {
                var lines = err.trim().split("\n").filter(function (l) { return /ERROR|error:/.test(l); });
                sheet.messageBox.show("Python", (lines.length ? lines.slice(-3).join("\n") : err.trim().split("\n").slice(-3).join("\n"))
                                      || "pip couldn't do it.", "error", ["OK"]);
            }
            sheet.loadPackages();
        });
    }
    function openTerminal(env) {
        // a shell with the environment active (PATH and VIRTUAL_ENV)
        var dir = envRoot + "/" + env.name;
        Services.openTerminal(Services.homePath, ["sh", "-c", ". \"$1/bin/activate\" && exec \"${SHELL:-/bin/bash}\" -i", "sh", dir]);
    }

    extras: [
        Dialog95 {
            id: newEnvDialog
            title: "New Environment"
            dialogWidth: 320
            dialogHeight: 136

            function open() {
                envName.text = "";
                systemBox.checked = false;
                show();
                envName.forceActiveFocus();
            }
            function ok() {
                var name = envName.text.trim();
                if (!/^[A-Za-z0-9_][A-Za-z0-9._-]*$/.test(name)) {
                    sheet.messageBox.show("Python", "Use letters, digits, '.', '-' and '_' for the name.", "error", ["OK"],
                                          function () { envName.forceActiveFocus(); });
                    return;
                }
                if (Services.exists(sheet.envRoot + "/" + name)) {
                    sheet.messageBox.show("Python", "There's already an environment called " + name + ".", "error", ["OK"],
                                          function () { envName.forceActiveFocus(); });
                    return;
                }
                newEnvDialog.close();
                envPage.working = "Making " + name + "...";
                var args = ["-m", "venv"];
                if (systemBox.checked)
                    args.push("--system-site-packages");
                Services.run("/usr/bin/python3", args.concat([sheet.envRoot + "/" + name]), "", function (code, out, err) {
                    envPage.working = "";
                    if (code !== 0)
                        sheet.messageBox.show("Python", /ensurepip|venv/.test(err)
                                              ? "Virtual environments aren't installed. Check \"Virtual environments (venv)\" on the General tab and click Apply."
                                              : err.trim(), "error", ["OK"]);
                    sheet.loadEnvs();
                    envList.selectedKey = name;
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
                id: envName
                x: 60
                y: 12
                width: 246
                onAccepted: newEnvDialog.ok()
            }
            CheckBox95 {
                id: systemBox
                x: 12
                y: 44
                text: "Can also use the system's Python <u>p</u>ackages"
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
                    enabled: envName.text.trim().length > 0
                    onClicked: newEnvDialog.ok()
                }
                Button95 {
                    text: "Cancel"
                    onClicked: newEnvDialog.close()
                }
            }
        },
        Dialog95 {
            id: installDialog
            title: "Install Package"
            dialogWidth: 330
            dialogHeight: 120

            function open() {
                pkgName.text = "";
                show();
                pkgName.forceActiveFocus();
            }
            function ok() {
                var specs = pkgName.text.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
                // package names and versions ("requests", "flask==3.0"), not pip's options
                if (!specs.length || specs.some(function (s) { return s.charAt(0) === "-"; })) {
                    pkgName.forceActiveFocus();
                    return;
                }
                installDialog.close();
                sheet.pip(["install"].concat(specs), "Installing " + specs.join(", ") + "...");
            }
            Text {
                x: 12
                y: 12
                width: 306
                wrapMode: Text.Wrap
                text: "Package names from PyPI, separated by spaces (for example: requests flask):"
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
