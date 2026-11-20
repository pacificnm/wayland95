import QtQuick
import Win95Ui
import Win95Control

// Add/Remove Programs: install new programs (Install... opens Add New
// Programs), remove installed ones, and install updates. Programs come from
// the system's packages (PackageKit: apt on Debian) and from Flatpak.
PropertySheet {
    id: sheet

    title: "Add/Remove Programs Properties"
    titles: ["Install/Uninstall", "Updates"]
    canApply: false

    readonly property bool anything: Software.packageKit || Software.flatpak
    function sourceName(item) {
        return item.source === "flatpak" ? "Flatpak" : Software.packageSource;
    }

    property var addWindow: null
    function openAddPrograms() {
        if (!addWindow)
            addWindow = addComponent.createObject(null);
        addWindow.show();
        addWindow.raise();
        addWindow.requestActivate();
    }
    Component {
        id: addComponent
        AddProgramsWindow {}
    }

    // Start menu > (right-click a program) > Uninstall... (win95control
    // --uninstall ID): the installed program that menu entry belongs to,
    // once the list is read; tasks.remove() asks before removing it
    property string pendingUninstall: ""
    function uninstall(desktopId) {
        pendingUninstall = desktopId;
        tryUninstall();
    }
    function tryUninstall() {
        if (!pendingUninstall || Software.loading)
            return;
        var id = pendingUninstall;
        pendingUninstall = "";
        var program = Software.programFor(id);
        if (!program.key) {
            sheet.messageBox.show("Add/Remove Programs", "\"" + Software.programName(id) + "\" can't be removed here: it isn't "
                                  + "part of an installed " + Software.packageSource + " package or Flatpak app.",
                                  "info", ["OK"]);
            return;
        }
        if (program.source === "deb" && program.id === "wayland95") {
            sheet.messageBox.show("Add/Remove Programs", "\"" + Software.programName(id) + "\" comes with Wayland 95 itself, "
                                  + "so it can't be removed on its own.", "info", ["OK"]);
            return;
        }
        var i = Software.programs.findIndex(function (p) { return p.key === program.key; });
        if (i >= 0)
            installed.select(i);
        tasks.remove(program);
    }
    Connections {
        target: Software
        function onLoadingChanged() { sheet.tryUninstall(); }
    }

    // updates are looked up (from the lists already downloaded) when the
    // tab is first opened
    onCurrentTabChanged: if (currentTab === 1 && !Software.updatesChecked && anything) Software.checkUpdates(false)

    component Label: Text {
        wrapMode: Text.Wrap
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    component Etched: Item {
        height: 2
        Rectangle { width: parent.width; height: 1; color: Theme.shadow }
        Rectangle { y: 1; width: parent.width; height: 1; color: Theme.highlight }
    }

    // ---------------------------------------------------- Install/Uninstall
    Item {
        AppletIcon { id: installIcon; x: 4; y: 4; kind: "install" }
        Label {
            x: 46
            y: 4
            width: parent.width - x - installButton.width - 12
            text: !sheet.anything
                  ? "This computer has no way to install programs (neither PackageKit nor Flatpak is available)."
                  : "To install a new program, click Install and choose one from "
                    + [Software.packageKit ? Software.packageSource + "'s software" : "",
                       Software.flatpak ? "Flatpak apps" : ""].filter(function (s) { return s; }).join(" or ") + "."
        }
        Button95 {
            id: installButton
            anchors.right: parent.right
            y: 8
            text: "<u>I</u>nstall..."
            enabled: sheet.anything
            onClicked: sheet.openAddPrograms()
        }

        Etched { y: 54; width: parent.width }

        AppletIcon { x: 4; y: 64; kind: "programs" }
        Label {
            x: 46
            y: 64
            width: parent.width - x
            text: "The following programs can be removed. To remove one, select it from the list and click Remove."
        }

        ProgramList {
            id: installed
            y: 104
            width: parent.width
            height: parent.height - y - removeButton.height - 8
            model: Software.programs
            // where it came from, when there's more than one place
            noteFor: function (item) { return Software.packageKit && Software.flatpak ? sheet.sourceName(item) : ""; }
            emptyText: Software.loading ? "Looking for programs..." : "No programs were found."
            onActivated: (item) => tasks.remove(item)
        }

        Label {
            anchors.verticalCenter: removeButton.verticalCenter
            width: parent.width - removeButton.width - 8
            elide: Text.ElideRight
            wrapMode: Text.NoWrap
            color: Theme.shadow
            text: {
                var p = installed.selected;
                if (!p)
                    return Software.programs.length + (Software.programs.length === 1 ? " program" : " programs");
                var parts = [p.version ? "Version " + p.version : "",
                             p.source === "flatpak" ? "Flatpak app" + (p.installation === "system" ? " (for everyone)" : "")
                                                    : Software.packageSource + " package " + p.id,
                             p.programCount > 1 ? p.programCount + " programs" : ""];
                return parts.filter(function (s) { return s; }).join(", ");
            }
        }

        Button95 {
            id: removeButton
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: "<u>R</u>emove..."
            enabled: !!installed.selected && !Software.busy
            onClicked: tasks.remove(installed.selected)
        }
    }

    // -------------------------------------------------------------- Updates
    Item {
        id: updatesPage

        // keys of the updates to install; all of them by default
        property var unchecked: ({})
        readonly property var chosen: Software.updates.filter(function (u) { return !unchecked[u.key]; })
        readonly property int security: Software.updates.filter(function (u) { return u.kind === "security"; }).length

        AppletIcon { x: 4; y: 4; kind: "updates" }
        Label {
            x: 46
            y: 4
            width: parent.width - x - checkButton.width - 12
            text: !sheet.anything ? "There's no way to update software on this computer."
                : Software.checkingUpdates ? "Checking for updates..."
                : !Software.updatesChecked ? "Click Check for Updates to see whether newer versions of your software are available."
                : Software.updates.length === 0 ? "Your software is up to date."
                : (Software.updates.length === 1 ? "1 update is" : Software.updates.length + " updates are") + " available"
                  + (updatesPage.security > 0 ? ", " + updatesPage.security + " of them for security." : ".")
                  + " Clear the ones you don't want, then click Install Updates."
        }
        Button95 {
            id: checkButton
            anchors.right: parent.right
            y: 8
            text: "<u>C</u>heck for Updates"
            enabled: sheet.anything && !Software.checkingUpdates && !Software.busy
            onClicked: Software.checkUpdates(true)
        }

        ProgramList {
            id: updateList
            y: 64
            width: parent.width
            height: parent.height - y - installUpdates.height - 10 - (desktopNote.visible ? desktopNote.height + 8 : 0)
            model: Software.updates
            checkable: true
            checked: {
                var c = {};
                for (var i = 0; i < Software.updates.length; ++i)
                    if (!updatesPage.unchecked[Software.updates[i].key])
                        c[Software.updates[i].key] = true;
                return c;
            }
            noteFor: function (item) {
                return (item.kind === "security" ? "Security  " : item.kind === "important" ? "Important  " : "")
                       + (item.version || "");
            }
            emptyText: Software.checkingUpdates ? "Checking..." : Software.updatesChecked ? "No updates." : ""
            onCheckToggled: (key, on) => {
                var u = Object.assign({}, updatesPage.unchecked);
                if (on) delete u[key]; else u[key] = true;
                updatesPage.unchecked = u;
            }
        }

        // when Wayland 95 itself can't be updated from here, say why
        Item {
            id: desktopNote
            visible: Software.desktopUpdates !== "package"
            y: updateList.y + updateList.height + 6
            width: parent.width
            height: 46
            AppletIcon { size: 16; kind: "updates"; y: 1 }
            Label {
                x: 22
                width: parent.width - x - (setUpButton.visible ? setUpButton.width + 8 : 0)
                text: Software.desktopUpdates === "source"
                      ? "Wayland 95 itself was installed from its source code (install.sh), so it isn't updated here. "
                        + "To update it, get the latest source and run install.sh again."
                      : "This computer doesn't know where Wayland 95's own updates are, so it won't find them. "
                        + "Click Set Up to add the Wayland 95 update source."
            }
            Button95 {
                id: setUpButton
                visible: Software.desktopUpdates === "no-source"
                anchors.right: parent.right
                width: 70
                text: "Set <u>U</u>p"
                onClicked: Software.setUpDesktopUpdates()
            }
            Connections {
                target: Software
                function onDesktopUpdatesSetUp(ok, message) {
                    if (!ok)
                        sheet.messageBox.show("Wayland 95 Update", message || "The update source couldn't be added.", "error", ["OK"]);
                }
            }
        }

        Button95 {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: updatesPage.chosen.length === Software.updates.length ? "Clear <u>A</u>ll" : "Select <u>A</u>ll"
            enabled: Software.updates.length > 0
            onClicked: {
                var u = {};
                if (updatesPage.chosen.length === Software.updates.length)
                    for (var i = 0; i < Software.updates.length; ++i)
                        u[Software.updates[i].key] = true;
                updatesPage.unchecked = u;
            }
        }
        Button95 {
            id: installUpdates
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: 110
            text: "Install <u>U</u>pdates"
            enabled: updatesPage.chosen.length > 0 && !Software.busy
            onClicked: tasks.update(updatesPage.chosen)
        }
    }

    extras: [
        SoftwareTasks {
            id: tasks
            messageBox: sheet.messageBox
        }
    ]
}
