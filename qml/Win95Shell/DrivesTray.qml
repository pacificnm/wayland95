import QtQuick
import Win95Ui as Ui
import Win95Shell

// A removable drive that's plugged in (Ui.Drives: UDisks2), one icon each.
// Hover: what it is. Click: a pop-up with the drive, its filesystems and
// Open / Eject. Right-click: Open and Eject. Double-click: Open.
// `group` is { drive, name, sizeText, driveName, partitions: [Drives entries] }.
TrayIcon {
    id: driveIcon

    property var group: ({})
    readonly property var parts: group.partitions || []
    readonly property bool anyOpen: parts.some(function (p) { return p.mounted; })

    toolTip: group.name + (group.sizeText ? " (" + group.sizeText + ")" : "")
             + (anyOpen ? "" : ": not opened") + ". Right-click to open or eject it."
    popupWidth: 250
    popupHeight: 74 + parts.length * 32

    // (the tests find each drive's icon by this)
    objectName: "drive:" + (group.drive || "")

    icon: Component {
        Canvas {
            property bool open: driveIcon.anyOpen
            onOpenChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                // a USB stick: blue body, silver plug, a light when it's open
                ctx.fillStyle = "#c0c0c0"; ctx.fillRect(11, 5, 5, 6);
                ctx.strokeStyle = "#000000"; ctx.lineWidth = 1; ctx.strokeRect(11.5, 5.5, 4, 5);
                ctx.fillStyle = "#000000"; ctx.fillRect(13, 6, 1, 1); ctx.fillRect(13, 9, 1, 1);
                ctx.fillStyle = "#2050b0"; ctx.fillRect(1, 3, 11, 10);
                ctx.strokeRect(1.5, 3.5, 10, 9);
                ctx.fillStyle = "#5080e0"; ctx.fillRect(2, 4, 9, 2);
                ctx.fillStyle = open ? "#00e000" : "#606060"; ctx.fillRect(8, 9, 2, 2);
            }
        }
    }

    function open(part) {
        tray.closePopups();
        Ui.Drives.mount(part.id, function (ok, result) {
            if (ok)
                Launcher.launch("win95files", [result]);
            else
                Notifications.notify("Drives", part.name + " couldn't be opened", result, "", 1);
        });
    }
    function eject() {
        tray.closePopups();
        if (parts.length === 0)
            return;
        var name = group.name;
        Ui.Drives.eject(parts[0].id, function (ok, err) {
            if (ok)
                Notifications.notify("Drives", name + " can now be safely removed", "", "", 0);
            else
                Notifications.notify("Drives", name + " couldn't be ejected", err, "", 1);
        });
    }
    function menuItems() {
        var items = [];
        if (parts.length === 1) {
            items.push({ text: "&Open", action: function () { driveIcon.open(driveIcon.parts[0]); } });
        } else {
            parts.forEach(function (p) {
                items.push({ text: "Open " + p.name, action: function () { driveIcon.open(p); } });
            });
        }
        items.push({ separator: true });
        items.push({ text: "&Eject " + group.name, action: driveIcon.eject });
        return items;
    }

    onDoubleClicked: if (parts.length) open(parts[0])
    onRightClicked: tray.showMenu(driveIcon, menuItems())

    // --- the pop-up: the drive, its filesystems, Open / Eject
    Column {
        x: 8
        y: 6
        width: driveIcon.popupWidth - 16
        spacing: 2
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: driveIcon.group.name + (driveIcon.group.sizeText ? " (" + driveIcon.group.sizeText + ")" : "")
            font.family: Ui.Theme.fontFamily
            font.pixelSize: Ui.Theme.fontSize
            font.bold: true
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            visible: !!driveIcon.group.driveName
            text: driveIcon.group.driveName || ""
            color: Ui.Theme.shadow
            font.family: Ui.Theme.fontFamily
            font.pixelSize: Ui.Theme.fontSize
        }
        Repeater {
            model: driveIcon.parts
            delegate: Item {
                required property var modelData
                width: parent.width
                height: 30
                Text {
                    y: 4
                    width: parent.width - 60
                    elide: Text.ElideMiddle
                    text: modelData.name + (modelData.fsType ? " (" + modelData.fsType + ")" : "")
                          + "\n" + (modelData.mounted ? "Open at " + modelData.mountPath : "Not opened")
                    font.family: Ui.Theme.fontFamily
                    font.pixelSize: Ui.Theme.fontSize
                }
                Ui.Button95 {
                    anchors.right: parent.right
                    y: 4
                    width: 54
                    text: "Open"
                    onClicked: driveIcon.open(modelData)
                }
            }
        }
    }
    Ui.Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        width: 70
        text: "Eject"
        onClicked: driveIcon.eject()
    }
}
