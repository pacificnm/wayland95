import QtQuick
import Win95Ui
import Win95Control

// Wayland 95 Development's settings: where Wayland 95's source is (getting
// it with git if it isn't there), building it in a Terminal window, and the
// packages building takes (debian/control's Build-Depends), with the
// missing ones installed in one go.
PropertySheet {
    id: sheet

    title: "Wayland 95 Development Properties"
    titles: ["General", "Packages"]

    readonly property var tool: Services.services.filter(function (s) { return s.id === "wayland95dev"; })[0] || ({})
    readonly property var packages: tool.packages || []
    readonly property string repository: "https://github.com/pacificnm/wayland95.git"
    readonly property string configFile: "~/.config/wayland95/development.conf"

    property var installed: ({})
    property bool checked: false
    readonly property var missing: packages.filter(function (p) { return !installed[p]; })
    property bool startedHere: false

    // the source folder: the one chosen before, else ~/wayland95
    property string sourceDir: ""
    property bool haveSource: false
    property string branch: ""
    property string working: ""

    function expand(path) { return path.indexOf("~/") === 0 ? Services.homePath + path.substring(1) : path; }
    function loadPackages() {
        Services.installedPackages(packages, function (names) {
            var m = {};
            names.forEach(function (n) { m[n] = true; });
            sheet.installed = m;
            sheet.checked = true;
        });
    }
    function loadSource() {
        var dir = expand(folderField.text.trim());
        sheet.sourceDir = dir;
        sheet.haveSource = dir.length > 0 && Services.exists(dir + "/CMakeLists.txt") && Services.exists(dir + "/debian/control");
        sheet.branch = "";
        if (sheet.haveSource && Services.exists(dir + "/.git"))
            Services.run("git", ["-C", dir, "rev-parse", "--abbrev-ref", "HEAD"], "", function (code, out) {
                sheet.branch = code === 0 ? out.trim() : "";
            });
    }
    Component.onCompleted: {
        var saved = "";
        Services.readFile(configFile).split("\n").forEach(function (l) {
            if (l.indexOf("source=") === 0)
                saved = l.substring(7).trim();
        });
        folderField.text = saved || "~/wayland95";
        loadSource();
        loadPackages();
        dirty = false;
    }

    onApplyRequested: {
        if (!Services.writeFile(configFile, "source=" + folderField.text.trim() + "\n"))
            messageBox.show("Wayland 95 Development", "The source folder couldn't be saved.", "error", ["OK"]);
        loadSource();
    }

    Connections {
        target: Services
        function onTaskFinished(ok, message) {
            if (sheet.startedHere && !ok)
                sheet.messageBox.show("Wayland 95 Development", message, "error", ["OK"]);
            else if (sheet.startedHere)
                sheet.messageBox.show("Wayland 95 Development", "Everything needed to build Wayland 95 is installed.", "info", ["OK"]);
            sheet.startedHere = false;
            sheet.loadPackages();
        }
    }

    function installMissing() {
        if (!missing.length || Services.busy)
            return;
        messageBox.show("Wayland 95 Development",
                        missing.length + (missing.length === 1 ? " package is" : " packages are")
                        + " downloaded from your software sources and installed:\n\n" + missing.join(", ")
                        + "\n\nInstall " + (missing.length === 1 ? "it" : "them") + " now?",
                        "question", ["Yes", "No"], function (b) {
            if (b !== 0)
                return;
            sheet.startedHere = true;
            Services.changePackages("Installing Wayland 95's build tools", sheet.missing, []);
        });
    }
    function getSource() {
        var dir = expand(folderField.text.trim());
        if (!dir || Services.exists(dir)) {
            messageBox.show("Wayland 95 Development", dir + " is already there. Choose an empty or new folder to put the source in.",
                            "warning", ["OK"]);
            return;
        }
        if (dirty)
            apply();
        sheet.working = "Getting the source (git clone)...";
        Services.run("git", ["clone", repository, dir], "", function (code, out, err) {
            sheet.working = "";
            if (code !== 0)
                sheet.messageBox.show("Wayland 95 Development",
                                      code === -1 ? "git isn't installed. Install the packages on the Packages tab first."
                                                  : "The source couldn't be fetched:\n\n" + err.trim().split("\n").slice(-3).join("\n"),
                                      "error", ["OK"]);
            sheet.loadSource();
        });
    }
    function build() {
        if (!haveSource)
            return;
        if (missing.length) {
            messageBox.show("Wayland 95 Development",
                            "Some of the packages building needs aren't installed yet (" + missing.length
                            + "). Build anyway? CMake will say what it can't find.",
                            "question", ["Yes", "No"], function (b) { if (b === 0) sheet.startBuild(); });
            return;
        }
        startBuild();
    }
    function startBuild() {
        Services.openTerminal(sourceDir, ["sh", "-c",
            "cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr"
            + " && cmake --build build -j\"$(nproc)\""
            + " && echo && echo 'Built. The programs are in build/; sudo cmake --install build puts them in place.'"
            + "; echo; printf 'Press Enter to close this window. '; read x"]);
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // ------------------------------------------------------------ General
    Item {
        AppletIcon { x: 4; y: 2; kind: "wayland95dev" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: !sheet.checked ? "Looking at what's installed..."
                : !sheet.missing.length ? "Everything needed to build Wayland 95 is installed (" + sheet.packages.length + " packages)."
                : (sheet.packages.length - sheet.missing.length) + " of the " + sheet.packages.length
                  + " packages building needs are installed. The Packages tab installs the rest."
        }

        GroupBox95 {
            id: sourceBox
            y: 54
            width: parent.width
            height: 132
            title: "Source"
            Label {
                width: parent.width
                text: "<u>F</u>older with Wayland 95's source:"
            }
            TextField95 {
                id: folderField
                y: 16
                width: parent.width
                onTextChanged: sheet.dirty = true
                onAccepted: { sheet.apply(); sheet.loadSource(); }
            }
            Label {
                y: 44
                width: parent.width
                color: Theme.shadow
                text: sheet.working ? sheet.working
                    : sheet.haveSource ? "The source is there" + (sheet.branch ? " (git branch " + sheet.branch + ")." : ".")
                    : "There's no Wayland 95 source in this folder yet. Get Source fetches it with git."
            }
            Row {
                y: 74
                anchors.right: parent.right
                spacing: 6
                Button95 {
                    width: 90
                    text: "<u>G</u>et Source"
                    enabled: !sheet.haveSource && !sheet.working
                    onClicked: sheet.getSource()
                }
                Button95 {
                    width: 75
                    text: "<u>B</u>uild"
                    enabled: sheet.haveSource && !sheet.working
                    onClicked: sheet.build()
                }
                Button95 {
                    width: 75
                    text: "<u>T</u>erminal"
                    enabled: sheet.haveSource
                    onClicked: Services.openTerminal(sheet.sourceDir)
                }
            }
        }
        Label {
            y: sourceBox.y + sourceBox.height + 8
            width: parent.width
            color: Theme.shadow
            text: "Build runs CMake in a Terminal window, putting the programs in the folder's build/ folder. "
                  + "Run them from there to try them; <b>sudo cmake --install build</b> puts them in place."
        }
    }

    // ---------------------------------------------------------- Packages
    Item {
        Label {
            width: parent.width
            text: "What building Wayland 95 needs (its debian/control's Build-Depends):"
        }
        ProgramList {
            id: pkgList
            y: 34
            width: parent.width
            height: 210
            model: sheet.packages.map(function (p) { return { key: p, name: p }; })
            iconComponent: AppletIcon { size: parent.size; kind: "wayland95dev" }
            noteFor: function (item) { return !sheet.checked ? "" : sheet.installed[item.key] ? "Installed" : "Missing"; }
            emptyText: "Looking..."
        }
        Item {
            y: pkgList.y + pkgList.height + 8
            width: parent.width
            height: 60
            visible: Services.busy
            Label { id: taskText; width: parent.width; text: Services.task + ": " + Services.status }
            ProgressBar95 { y: taskText.height + 4; width: parent.width; value: Services.percent }
        }
        Label {
            y: pkgList.y + pkgList.height + 12
            width: parent.width - installButton.width - 8
            visible: !Services.busy
            color: Theme.shadow
            text: !sheet.checked ? "" : sheet.missing.length ? sheet.missing.length + " missing." : "All installed."
        }
        Button95 {
            id: installButton
            y: pkgList.y + pkgList.height + 8
            anchors.right: parent.right
            width: 110
            visible: !Services.busy
            text: "<u>I</u>nstall Missing"
            enabled: sheet.checked && sheet.missing.length > 0 && !Services.busy
            onClicked: sheet.installMissing()
        }
    }
}
