import QtQuick
import Win95Ui
import Win95Control

// Start Up: what starts when you log in (Programs: XDG autostart), the
// computer's and your own services (systemd), and how the computer starts
// (Boot). Everything but Boot's "Start the computer to" happens at once.
PropertySheet {
    id: sheet

    title: "Start Up Properties"
    readonly property PropertySheet theSheet: sheet
    titles: ["Programs", "Services", "My Services", "Boot"]
    width: 600
    height: 480

    property var boot: ({})
    property string target: ""   // the choice on Boot (applied by Apply/OK)

    onCurrentTabChanged: {
        if (currentTab === 1) systemPage.load();
        if (currentTab === 2) userPage.load();
        if (currentTab === 3 && !boot.summary) loadBoot();
    }
    Component.onCompleted: {
        Startup.refreshPrograms();
        if (currentTab > 0) currentTabChanged();
    }

    function loadBoot() {
        boot = { summary: "Looking..." };
        Startup.bootInfo(function (info) {
            sheet.boot = info;
            if (!sheet.dirty)
                sheet.target = info.target;
        });
    }

    onApplyRequested: {
        if (!target || target === boot.target)
            return;
        setDefault(target);
    }

    function setDefault(to) {
        Startup.setDefaultTarget(to, function (ok, msg) {
            if (ok)
                sheet.boot = Object.assign({}, sheet.boot, { target: to });
            else {
                sheet.target = sheet.boot.target;
                messageBox.show("Start Up", "Couldn't change how the computer starts.\n\n" + msg, "error", ["OK"]);
            }
        });
    }

    component Label: Text {
        wrapMode: Text.Wrap
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        color: Theme.text
    }

    function error(msg) {
        if (msg)
            messageBox.show("Start Up", msg, "error", ["OK"]);
    }

    // ------------------------------------------------------------ Programs
    Item {
        readonly property var current: programList.selected

        AppletIcon { x: 4; y: 2; kind: "startup" }
        Label {
            x: 46
            y: 2
            width: parent.width - x
            text: "These programs start when you log in to Wayland 95. Untick one to stop it starting; "
                  + "the system's own stay listed, turned off just for you."
        }
        ProgramList {
            id: programList
            y: 40
            width: parent.width
            height: parent.height - y - 64
            model: Startup.programs
            checkable: true
            checked: {
                var m = {};
                for (var i = 0; i < Startup.programs.length; ++i)
                    if (Startup.programs[i].enabled)
                        m[Startup.programs[i].key] = true;
                return m;
            }
            noteFor: function (item) {
                return !item.here ? "Not for this desktop" : item.system ? (item.own ? "System (changed)" : "System") : "Yours";
            }
            emptyText: "Nothing starts when you log in. Add... chooses a program to."
            onCheckToggled: (key, on) => sheet.error(Startup.setProgramEnabled(key, on))
            onActivated: (item) => editDialog.open(item)
        }
        Label {
            y: programList.y + programList.height + 6
            width: parent.width
            height: 28
            elide: Text.ElideRight
            maximumLineCount: 2
            text: parent.current ? "<b>" + parent.current.name + "</b>: " + parent.current.command : ""
        }
        Row {
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            spacing: 6
            Button95 {
                width: 70
                text: "<u>A</u>dd..."
                onClicked: editDialog.open(null)
            }
            Button95 {
                width: 70
                text: "<u>E</u>dit..."
                enabled: !!programList.selected
                onClicked: editDialog.open(programList.selected)
            }
            Button95 {
                width: 70
                text: programList.selected && programList.selected.system ? "Re<u>s</u>et" : "<u>R</u>emove"
                enabled: !!programList.selected && programList.selected.own
                onClicked: {
                    var p = programList.selected;
                    var q = p.system ? "Put " + p.name + " back as the system has it?"
                                     : "Stop starting " + p.name + " and remove it from this list?";
                    messageBox.show("Start Up", q, "question", ["Yes", "No"], function (b) {
                        if (b === 0)
                            sheet.error(Startup.removeProgram(p.key));
                    });
                }
            }
        }
    }

    // ------------------------------------------------------------ Services
    UnitPage {
        id: systemPage
        sheet: theSheet
        user: false
    }

    // --------------------------------------------------------- My Services
    UnitPage {
        id: userPage
        sheet: theSheet
        user: true
    }

    // ---------------------------------------------------------------- Boot
    Item {
        GroupBox95 {
            id: timeBox
            title: "Starting up"
            width: parent.width
            height: 50
            Label {
                width: parent.width
                text: sheet.boot.summary || ""
            }
        }
        GroupBox95 {
            id: blameBox
            title: "What took longest last time"
            y: timeBox.height + 6
            width: parent.width
            height: parent.height - y - targetBox.height - 6
            Bevel {
                width: parent.width
                height: parent.height
                style: "sunken"
                face: Theme.window
                ListView {
                    id: blameView
                    x: 2
                    y: 2
                    width: parent.width - 4
                    height: parent.height - 4
                    clip: true
                    interactive: false
                    model: sheet.boot.blame || []
                    delegate: Item {
                        required property var modelData
                        width: blameView.width
                        height: 17
                        Label { x: 4; width: 90; text: modelData.time; horizontalAlignment: Text.AlignRight }
                        Label { x: 104; width: parent.width - 108; elide: Text.ElideRight; wrapMode: Text.NoWrap; text: modelData.unit }
                    }
                    WheelHandler { onWheel: (event) => blameView.contentY = Math.max(0, Math.min(blameView.contentHeight - blameView.height, blameView.contentY - event.angleDelta.y / 120 * 51)) }
                }
            }
        }
        GroupBox95 {
            id: targetBox
            title: "Start the computer to"
            anchors.bottom: parent.bottom
            width: parent.width
            height: 74
            Column {
                spacing: 4
                RadioButton95 {
                    text: "The <u>d</u>esktop (the login screen, then Wayland 95)"
                    checked: sheet.target === "graphical.target"
                    onClicked: { sheet.target = "graphical.target"; sheet.dirty = sheet.target !== sheet.boot.target; }
                }
                RadioButton95 {
                    text: "A <u>t</u>ext console only (no desktop: for servers and repairs)"
                    checked: sheet.target === "multi-user.target"
                    onClicked: {
                        if (sheet.target === "multi-user.target")
                            return;
                        sheet.target = "multi-user.target";
                        sheet.messageBox.show("Start Up",
                                              "The computer will start to a text console, with no login screen or desktop.\n\n"
                                              + "To get the desktop back from there: log in and type\n"
                                              + "sudo systemctl set-default graphical.target\n\nStart that way from now on?",
                                              "warning", ["Yes", "No"], function (b) {
                                                  if (b !== 0)
                                                      sheet.target = sheet.boot.target;
                                                  sheet.dirty = sheet.target !== sheet.boot.target;
                                              });
                    }
                }
            }
        }
    }

    // ------------------------------------------------- Add / Edit a program
    extras: Dialog95 {
        id: editDialog
        title: editing ? "Edit Program" : "Add Program"
        dialogWidth: 420
        dialogHeight: editing ? 130 : 330

        property var editing: null
        property string pickedId: ""

        function open(item) {
            editing = item;
            pickedId = "";
            nameField.text = item ? item.name : "";
            commandField.text = item ? item.command : "";
            show();
            if (item) nameField.forceActiveFocus();
            else installed.forceActiveFocus();
            if (!item && installed.model.length === 0)
                installed.model = Startup.installedPrograms();
        }
        function accept() {
            var err;
            if (editing)
                err = Startup.editProgram(editing.key, nameField.text, commandField.text);
            else if (pickedId && installed.selected && nameField.text === installed.selected.name
                     && commandField.text === installed.selected.command)
                err = Startup.addInstalled(pickedId);
            else
                err = Startup.addProgram(nameField.text, commandField.text);
            if (err) {
                sheet.messageBox.show("Start Up", err, "error", ["OK"]);
                return;
            }
            close();
        }

        Label {
            visible: !editDialog.editing
            x: 10; y: 10
            text: "Choose a program, or type its name and command:"
        }
        ProgramList {
            id: installed
            visible: !editDialog.editing
            x: 10; y: 28
            width: parent.width - 20
            height: 190
            onSelectedChanged: if (selected) {
                editDialog.pickedId = selected.key;
                nameField.text = selected.name;
                commandField.text = selected.command;
            }
            onActivated: editDialog.accept()
        }
        Label { x: 10; y: editDialog.editing ? 14 : 228; text: "Name:" }
        TextField95 {
            id: nameField
            x: 80; y: editDialog.editing ? 10 : 224
            width: parent.width - 90
            onAccepted: editDialog.accept()
            onEscaped: editDialog.close()
        }
        Label { x: 10; y: editDialog.editing ? 42 : 256; text: "Command:" }
        TextField95 {
            id: commandField
            x: 80; y: editDialog.editing ? 38 : 252
            width: parent.width - 90
            onAccepted: editDialog.accept()
            onEscaped: editDialog.close()
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: editDialog.accept() }
            Button95 { text: "Cancel"; onClicked: editDialog.close() }
        }
    }
}
