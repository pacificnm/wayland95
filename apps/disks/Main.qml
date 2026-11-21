import QtQuick
import QtQuick.Window
import Win95Ui

// Disk Manager: the computer's disks, each drawn as a bar of its partitions
// (like Windows' Disk Management), and the partitions listed below. Mount,
// unmount, format, make and delete partitions, give a disk a new partition
// table, change labels, check and repair; and every disk's S.M.A.R.T.
// health. Disks (C++) does the work through UDisks2, which asks for the
// administrator password; S.M.A.R.T. comes from smartctl.
Window {
    id: window

    title: "Disk Manager"
    width: 720
    height: 540
    minimumWidth: 560
    minimumHeight: 420
    visible: true
    color: Theme.face

    property int driveIndex: -1
    property int partIndex: -1
    readonly property var drive: driveIndex >= 0 && driveIndex < Disks.drives.length ? Disks.drives[driveIndex] : null
    readonly property var parts: { Disks.revision; return drive ? Disks.layout(drive.path) : []; }
    readonly property var part: partIndex >= 0 && partIndex < parts.length ? parts[partIndex] : null
    readonly property bool modalOpen: msgBox.visible || partDialog.visible || aboutDialog.visible

    // what can be done to the chosen partition
    readonly property bool isPartition: !!part && (part.kind === "partition" || part.kind === "logical" || part.kind === "extended")
    readonly property bool blocked: !!part && (part.system || (part.inUse && !part.mounted))
    readonly property bool canMount: !!part && part.canMount && !part.mounted && !Disks.busy
    readonly property bool canUnmount: !!part && part.mounted && !part.system && !Disks.busy
    readonly property bool canFormat: !!part && part.kind !== "free" && part.kind !== "extended" && !blocked && !Disks.busy
    readonly property bool canDelete: isPartition && !blocked && !Disks.busy
    readonly property bool canCreate: !!part && part.kind === "free" && !Disks.busy
    readonly property bool canLabel: !!part && part.canMount && part.fsType !== "swap" && !Disks.busy
    readonly property bool canCheck: !!part && part.canMount && !part.system && !Disks.busy
    readonly property bool canNewTable: !!drive && !drive.system && !Disks.busy
    readonly property bool canPowerOff: !!drive && drive.canPowerOff && !drive.system && !Disks.busy

    function bytes(n) {
        if (n >= 1e12) return (n / 1e12).toFixed(2) + " TB";
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " GB";
        if (n >= 1e6) return (n / 1e6).toFixed(0) + " MB";
        if (n >= 1e3) return Math.round(n / 1e3) + " KB";
        return n + " bytes";
    }
    function kindName(d) {
        switch (d.kind) {
        case "nvme": return "NVMe SSD";
        case "ssd": return "SSD";
        case "usb": return "USB drive";
        case "card": return "Memory card";
        case "virtual": return "Virtual disk";
        default: return d.rpm > 0 ? "Hard disk (" + d.rpm + " rpm)" : "Hard disk";
        }
    }
    function healthText(d) {
        var t = d.health === "ok" ? "OK" : d.health === "warning" ? "Warning" : d.health === "failing" ? "FAILING" : "Not checked";
        return d.temperature > 0 ? t + ", " + d.temperature + " °C" : t;
    }
    function partStatus(p) {
        if (p.kind === "free") return "Free space";
        if (p.kind === "empty") return "Nothing on it";
        var s = [];
        if (p.system) s.push("System");
        if (p.mounted) s.push("Mounted");
        else if (p.inUse) s.push("In use");
        if (p.bootable) s.push("Boot");
        return s.length ? s.join(", ") : "Healthy";
    }

    // keep the chosen disk when the list changes (a USB drive plugged in)
    property string chosenDrive: ""
    property real chosenOffset: -1
    // (a note from the last job goes when something else is chosen)
    function pickDrive(i) {
        statusNote = "";
        driveIndex = i;
        chosenDrive = drive ? drive.path : "";
        partIndex = -1;
        chosenOffset = -1;
        if (parts.length)
            pickPart(0);
    }
    function pickPart(i) {
        statusNote = "";
        partIndex = i;
        chosenOffset = part ? part.offset : -1;
    }
    Connections {
        target: Disks
        function onDrivesChanged() {
            var list = Disks.drives;
            for (var i = 0; i < list.length; ++i) {
                if (list[i].path === window.chosenDrive) {
                    window.driveIndex = i;
                    return;
                }
            }
            // (a drive switched off and gone: its "unplug it now" stays)
            var note = window.statusNote;
            window.pickDrive(list.length ? 0 : -1);
            window.statusNote = note;
        }
        function onRevisionChanged() { Qt.callLater(window.reselect); }
        function onFinished(ok, message) {
            if (!message)
                return;
            if (ok)
                window.statusNote = message;
            else
                msgBox.show("Disk Manager", message, "error", ["OK"], null);
        }
    }
    property string statusNote: ""
    // the same partition (by where it starts) after the disk changed, once
    // `parts` has caught up
    function reselect() {
        for (var i = 0; i < parts.length; ++i) {
            if (parts[i].offset === chosenOffset) {
                partIndex = i;
                return;
            }
        }
        partIndex = parts.length ? Math.min(partIndex, parts.length - 1) : -1;
    }

    Component.onCompleted: {
        var start = 0;
        for (var i = 0; i < Disks.drives.length; ++i)
            if (Disks.drives[i].device === initialDevice)
                start = i;
        pickDrive(Disks.drives.length ? start : -1);
        if (initialSmart && drive)
            smartWindow.open(drive);
    }

    // ---- the jobs, with "are you sure?" where something is erased
    function partName(p) { return (p.label ? "\"" + p.label + "\" (" + p.device + ")" : p.device); }
    function doMount() { if (canMount) Disks.mount(part.path); }
    function doUnmount() { if (canUnmount) Disks.unmount(part.path); }
    function doOpen() {
        if (part && part.mounted)
            Disks.openFolder(part.mountPoints[0]);
        else if (canMount)
            Disks.mount(part.path);
    }
    function doNew() { if (canCreate) partDialog.ask("new", part, drive); }
    function doFormat() { if (canFormat) partDialog.ask("format", part, drive); }
    function doLabel() { if (canLabel) partDialog.ask("label", part, drive); }
    function doNewTable() { if (canNewTable) partDialog.ask("table", null, drive); }
    function doDelete() {
        if (!canDelete)
            return;
        var p = part;
        msgBox.show("Delete Partition",
                    "Everything on " + partName(p) + " (" + bytes(p.size) + ") will be lost.\n\nDelete this partition?",
                    "warning", ["Yes", "No"], function (i) { if (i === 0) Disks.deletePartition(p.path); });
    }
    function doCheck() { if (canCheck) Disks.check(part.path); }
    function doRepair() {
        if (!canCheck)
            return;
        var p = part;
        msgBox.show("Repair",
                    "Repairing fixes what's wrong with " + partName(p) + "'s filesystem. It's unmounted first.\n\n"
                    + "Repairs can lose damaged files, so back up what you can first. Repair it now?",
                    "question", ["Yes", "No"], function (i) { if (i === 0) Disks.repair(p.path); });
    }
    function doPowerOff() { if (canPowerOff) Disks.powerOff(drive.path); }
    function doSmart() { if (drive) smartWindow.open(drive); }

    Connections {
        target: partDialog
        function onDone(v) {
            var p = partDialog.part, d = partDialog.drive;
            switch (partDialog.mode) {
            case "new":
                Disks.createPartition(d.path, p.offset, v.size, v.fsType, v.label);
                break;
            case "format":
                msgBox.show("Format", "Everything on " + partName(p) + " will be erased.\n\nFormat it now?",
                            "warning", ["Yes", "No"], function (i) { if (i === 0) Disks.format(p.path, v.fsType, v.label); });
                break;
            case "label":
                Disks.setLabel(p.path, v.label);
                break;
            case "table":
                msgBox.show("New Partition Table",
                            "EVERYTHING on " + d.name + " (" + bytes(d.size) + ", " + d.device + ") will be erased: "
                            + "all its partitions and files.\n\nAre you sure?",
                            "warning", ["Yes", "No"], function (i) { if (i === 0) Disks.newPartitionTable(d.path, v.table); });
                break;
            }
        }
    }

    // ---- menus
    function diskMenu() {
        return [
            { text: "S.M.A.R.T. &Status...", enabled: !!drive, action: doSmart },
            { separator: true },
            { text: "New Partition &Table...", enabled: canNewTable, action: doNewTable },
            { text: "S&witch Off", enabled: canPowerOff, action: doPowerOff },
            { separator: true },
            { text: "&Refresh", shortcut: "F5", action: function () { Disks.refresh(); } },
            { separator: true },
            { text: "E&xit", action: function () { window.close(); } },
        ];
    }
    function partitionMenu() {
        return [
            { text: "&Open", enabled: !!part && (part.mounted || canMount), action: doOpen },
            { text: "&Mount", enabled: canMount, action: doMount },
            { text: "&Unmount", enabled: canUnmount, action: doUnmount },
            { separator: true },
            { text: "&New Partition...", enabled: canCreate, action: doNew },
            { text: "&Format...", enabled: canFormat, action: doFormat },
            { text: "&Delete", shortcut: "Del", enabled: canDelete, action: doDelete },
            { text: "Change &Label...", enabled: canLabel, action: doLabel },
            { separator: true },
            { text: "&Check for Problems", enabled: canCheck, action: doCheck },
            { text: "&Repair...", enabled: canCheck, action: doRepair },
        ];
    }
    function helpMenu() {
        return [{ text: "&About Disk Manager", action: function () { aboutDialog.show(); } }];
    }
    function driveRowMenu() {
        return diskMenu().slice(0, 4);
    }

    Shortcut { sequence: "F5"; enabled: !window.modalOpen; onActivated: Disks.refresh() }
    Shortcut { sequence: "Delete"; enabled: !window.modalOpen; onActivated: window.doDelete() }

    // ---- the window
    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "Disk", items: window.diskMenu },
            { title: "Partition", items: window.partitionMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // the disks
    Bevel {
        id: driveWell
        x: 4
        y: menuBar.y + menuBar.height + 4
        width: parent.width - 8
        height: Math.min(18 * Math.max(3, Disks.drives.length) + 24, 150)
        style: "sunken"
        face: Theme.window

        readonly property var columns: [
            { title: "Disk", w: Math.max(150, driveList.width - 70 - 120 - 70 - 60 - 120) },
            { title: "Device", w: 70 }, { title: "Type", w: 120 }, { title: "Size", w: 70, right: true },
            { title: "Table", w: 60 }, { title: "Health", w: 120 },
        ]
        Row {
            id: driveHeader
            x: 2
            y: 2
            height: 18
            clip: true
            width: parent.width - 4
            Repeater {
                model: driveWell.columns
                Item {
                    required property var modelData
                    width: modelData.w
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 4
                        width: parent.width - 8
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                        elide: Text.ElideRight
                        text: parent.modelData.title
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
        ListView {
            id: driveList
            x: 2
            y: driveHeader.y + driveHeader.height
            width: parent.width - 4 - (driveBar.visible ? driveBar.width : 0)
            height: parent.height - y - 2
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: Disks.drives
            currentIndex: window.driveIndex
            activeFocusOnTab: true
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Down && window.driveIndex < Disks.drives.length - 1)
                    window.pickDrive(window.driveIndex + 1);
                else if (event.key === Qt.Key_Up && window.driveIndex > 0)
                    window.pickDrive(window.driveIndex - 1);
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    window.doSmart();
                else
                    return;
                event.accepted = true;
            }
            delegate: Item {
                id: driveRow
                required property var modelData
                required property int index
                readonly property bool selected: index === window.driveIndex
                width: driveList.width
                height: 18
                Rectangle {
                    anchors.fill: parent
                    color: driveRow.selected ? Theme.selection : "transparent"
                }
                Loader {
                    anchors.fill: parent
                    active: driveRow.selected && driveList.activeFocus
                    sourceComponent: FocusRect95 { color: "#ffff80" }
                }
                Row {
                    Repeater {
                        model: driveWell.columns
                        Item {
                            id: driveCell
                            required property var modelData
                            required property int index
                            width: modelData.w
                            height: 18
                            DriveIcon {
                                visible: driveCell.index === 0
                                x: 3
                                y: 1
                                size: 16
                                name: driveRow.modelData.kind
                            }
                            Text {
                                x: driveCell.index === 0 ? 23 : 4
                                width: parent.width - x - 4
                                height: 18
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: driveCell.modelData.right ? Text.AlignRight : Text.AlignLeft
                                elide: Text.ElideRight
                                readonly property var d: driveRow.modelData
                                readonly property bool alarm: driveCell.index === 5 && (d.health === "failing" || d.health === "warning")
                                text: [d.name, d.device.replace("/dev/", ""), window.kindName(d), window.bytes(d.size),
                                       d.table === "gpt" ? "GPT" : d.table === "dos" ? "MBR" : "-", window.healthText(d)][driveCell.index]
                                color: driveRow.selected ? Theme.selectionText : alarm ? "#c00000" : Theme.text
                                font.bold: alarm && d.health === "failing"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: {
                        driveList.forceActiveFocus();
                        if (driveRow.index !== window.driveIndex)
                            window.pickDrive(driveRow.index);
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            var p = mapToItem(popup, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, window.driveRowMenu());
                        }
                    }
                    onDoubleClicked: window.doSmart()
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    driveList.contentY = Math.max(0, Math.min(driveList.contentHeight - driveList.height,
                                                              driveList.contentY - event.angleDelta.y / 120 * 54));
                }
            }
        }
        ScrollBar95 {
            id: driveBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: driveList.y
            height: driveList.height
            flickable: driveList
            stepSize: 18
        }
        Text {
            visible: Disks.drives.length === 0
            anchors.centerIn: parent
            text: "No disks were found. (Is UDisks2 installed? sudo apt install udisks2)"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // the chosen disk, as a bar
    DiskMap {
        id: diskMap
        x: 4
        y: driveWell.y + driveWell.height + 6
        width: parent.width - 8
        visible: !!window.drive
        drive: window.drive
        number: window.driveIndex
        parts: window.parts
        selectedIndex: window.partIndex
        onPicked: (i) => { window.pickPart(i); partList.forceActiveFocus(); }
        onMenuRequested: (i, x, y) => {
            var p = diskMap.mapToItem(popup, x, y);
            popup.popup(p.x, p.y, window.partitionMenu());
        }
    }

    // its partitions
    Bevel {
        id: partWell
        x: 4
        y: diskMap.y + diskMap.height + 6
        width: parent.width - 8
        height: statusBar.y - y - 4
        style: "sunken"
        face: Theme.window

        readonly property var columns: [
            { title: "Partition", w: 96 }, { title: "Label", w: Math.max(70, partList.width - 96 - 80 - 120 - 70 - 64 - 100 - 90) },
            { title: "File System", w: 80 }, { title: "Type", w: 120 }, { title: "Size", w: 70, right: true },
            { title: "Used", w: 64, right: true }, { title: "Mounted At", w: 100 }, { title: "Status", w: 90 },
        ]
        Row {
            id: partHeader
            x: 2
            y: 2
            height: 18
            clip: true
            width: parent.width - 4
            Repeater {
                model: partWell.columns
                Item {
                    required property var modelData
                    width: modelData.w
                    height: 18
                    Bevel { anchors.fill: parent; style: "raised" }
                    Text {
                        x: 4
                        width: parent.width - 8
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: parent.modelData.right ? Text.AlignRight : Text.AlignLeft
                        elide: Text.ElideRight
                        text: parent.modelData.title
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
        ListView {
            id: partList
            x: 2
            y: partHeader.y + partHeader.height
            width: parent.width - 4 - (partBar.visible ? partBar.width : 0)
            height: parent.height - y - 2
            clip: true
            focus: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: window.parts
            currentIndex: window.partIndex
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            Keys.onPressed: (event) => {
                switch (event.key) {
                case Qt.Key_Down: if (window.partIndex < window.parts.length - 1) window.pickPart(window.partIndex + 1); break;
                case Qt.Key_Up: if (window.partIndex > 0) window.pickPart(window.partIndex - 1); break;
                case Qt.Key_Home: if (window.parts.length) window.pickPart(0); break;
                case Qt.Key_End: if (window.parts.length) window.pickPart(window.parts.length - 1); break;
                case Qt.Key_Return:
                case Qt.Key_Enter: window.part && window.part.kind === "free" ? window.doNew() : window.doOpen(); break;
                case Qt.Key_Menu: {
                    var p = partList.mapToItem(popup, 20, 20);
                    popup.popup(p.x, p.y, window.partitionMenu());
                    break;
                }
                default: return;
                }
                event.accepted = true;
            }
            delegate: Item {
                id: partRow
                required property var modelData
                required property int index
                readonly property bool selected: index === window.partIndex
                width: partList.width
                height: 18
                Rectangle {
                    anchors.fill: parent
                    color: partRow.selected ? Theme.selection : "transparent"
                }
                Loader {
                    anchors.fill: parent
                    active: partRow.selected && partList.activeFocus
                    sourceComponent: FocusRect95 { color: "#ffff80" }
                }
                Row {
                    Repeater {
                        model: partWell.columns
                        Text {
                            id: partCell
                            required property var modelData
                            required property int index
                            readonly property var p: partRow.modelData
                            width: modelData.w
                            height: 18
                            leftPadding: index === 0 && (p.kind === "logical" || (p.kind === "free" && p.logical)) ? 16 : 4
                            rightPadding: 4
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: modelData.right ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                            text: {
                                var p = partCell.p;
                                switch (index) {
                                case 0: return p.kind === "free" ? "(free space)" : p.device.replace("/dev/", "");
                                case 1: return p.label || p.partName || "";
                                case 2: return p.kind === "extended" ? "" : p.fsName || "";
                                case 3: return p.kind === "extended" ? "Extended" : p.kind === "free" ? "" : p.partTypeName || "";
                                case 4: return window.bytes(p.size);
                                case 5: return p.used >= 0 ? window.bytes(p.used) : "";
                                case 6: return p.mountPoints ? p.mountPoints.join(", ") : "";
                                default: return window.partStatus(p);
                                }
                            }
                            color: partRow.selected ? Theme.selectionText : partCell.p.kind === "free" ? Theme.shadow : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: {
                        partList.forceActiveFocus();
                        window.pickPart(partRow.index);
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            var p = mapToItem(popup, mouse.x, mouse.y);
                            popup.popup(p.x, p.y, window.partitionMenu());
                        }
                    }
                    onDoubleClicked: (mouse) => {
                        if (mouse.button !== Qt.LeftButton)
                            return;
                        if (partRow.modelData.kind === "free")
                            window.doNew();
                        else
                            window.doOpen();
                    }
                }
            }
            WheelHandler {
                onWheel: (event) => {
                    partList.contentY = Math.max(0, Math.min(partList.contentHeight - partList.height,
                                                             partList.contentY - event.angleDelta.y / 120 * 54));
                }
            }
        }
        ScrollBar95 {
            id: partBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: partList.y
            height: partList.height
            flickable: partList
            stepSize: 18
        }
    }

    // the status bar: what's happening, or about the chosen partition
    Item {
        id: statusBar
        x: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 2
        width: parent.width - 4
        height: 20
        Bevel { anchors.fill: parent; style: "thinSunken" }
        Text {
            x: 4
            width: parent.width - 8
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: Disks.busy ? Disks.status + " (an administrator password may be asked for)"
                : window.statusNote ? window.statusNote
                : window.part && window.part.system ? window.part.device + " holds the system this computer is running: it can't be erased or unmounted."
                : window.part && window.part.inUse && !window.part.mounted ? window.part.device + " is in use (encryption, LVM, RAID or swap)."
                : window.drive ? window.drive.name + (window.drive.serial ? ", serial " + window.drive.serial : "")
                : ""
        }
    }


    // a job running when the window closes would leave the disk half-done
    onClosing: (close) => {
        if (Disks.busy) {
            close.accepted = false;
            msgBox.show("Disk Manager", "Disk Manager is still working on the disk. Wait until it's done.",
                        "warning", ["OK"], null);
        }
    }

    PartitionDialog {
        id: partDialog
        popupMenu: popup
        onClosed: partList.forceActiveFocus()
    }

    Dialog95 {
        id: aboutDialog
        title: "About Disk Manager"
        dialogWidth: 340
        dialogHeight: 160
        DriveIcon { x: 14; y: 14; size: 32; name: "disks" }
        Text {
            x: 60
            y: 14
            width: parent.width - 74
            wrapMode: Text.Wrap
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "<b>Disk Manager</b><br>Wayland 95<br><br>Disks and partitions through UDisks2; "
                  + "S.M.A.R.T. health from smartctl (smartmontools"
                  + (Disks.smartctlInstalled ? ")." : ", not installed).")
        }
        Button95 {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 10
            text: "OK"
            isDefault: true
            onClicked: aboutDialog.close()
        }
    }

    SmartWindow {
        id: smartWindow
        transientParent: window
    }

    Menu95 {
        id: popup
        onClosed: if (!window.modalOpen) partList.forceActiveFocus()
    }
    MessageBox95 { id: msgBox }

    // W95_DISKS_GRAB: a picture of the window, for the tests
    Timer {
        running: testGrab !== ""
        interval: 2500
        onTriggered: window.contentItem.grabToImage(function (r) { r.saveToFile(testGrab); console.log("grabbed " + testGrab); })
    }
}
