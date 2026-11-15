import QtQuick
import QtQuick.Window
import Win95Ui

// Image Writer: puts a disk image (a Linux .iso, an .img) on a USB drive so
// a computer can start from it, then reads it back to check. Writer (C++)
// does the work through UDisks2, which asks for the password.
Window {
    id: window

    title: "Image Writer"
    width: 460
    height: 380
    minimumWidth: 400
    minimumHeight: 380
    visible: true
    color: Theme.face

    property string image: initialImage
    readonly property var info: image ? Writer.imageInfo(image) : ({})
    readonly property bool imageOk: !!info.ok
    readonly property var drive: driveBox.currentIndex >= 0 && driveBox.currentIndex < Writer.drives.length
                                 ? Writer.drives[driveBox.currentIndex] : null
    readonly property bool fits: !drive || !imageOk || info.size <= drive.size
    readonly property bool canWrite: imageOk && !!drive && fits && !Writer.busy

    function bytes(n) {
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " GB";
        if (n >= 1e6) return (n / 1e6).toFixed(0) + " MB";
        if (n >= 1e3) return (n / 1e3).toFixed(0) + " KB";
        return n + " bytes";
    }
    function fileName(path) { return path.substring(path.lastIndexOf("/") + 1); }
    function driveLabel(d) { return d.name + " - " + bytes(d.size) + " (" + d.device + ")"; }
    function timeLeft(seconds) {
        if (seconds < 60) return "less than a minute";
        var m = Math.round(seconds / 60);
        return "about " + m + (m === 1 ? " minute" : " minutes");
    }

    // keep the chosen drive when the list changes (another one plugged in)
    property string chosenPath: ""
    Connections {
        target: Writer
        function onDrivesChanged() {
            var list = Writer.drives;
            for (var i = 0; i < list.length; ++i) {
                if (list[i].path === window.chosenPath) {
                    driveBox.currentIndex = i;
                    return;
                }
            }
            driveBox.currentIndex = 0;
        }
        function onStateChanged() {
            if (Writer.state === "done") {
                Wayland95Settings.playSound("asterisk");
                msgBox.show("Image Writer", fileName(window.lastImage) + " was written to " + window.lastDrive
                            + (verifyBox.checked ? " and checked." : ".")
                            + "\n\nYou can unplug the drive now.", "info", ["OK"], function () { Writer.reset(); });
            } else if (Writer.state === "failed") {
                msgBox.show("Image Writer", Writer.error, "error", ["OK"], function () { Writer.reset(); });
            }
        }
    }
    Component.onCompleted: driveBox.currentIndex = 0

    property string lastImage
    property string lastDrive
    function startWrite() {
        var d = drive;
        msgBox.show("Image Writer",
                    "Everything on " + d.name + " (" + bytes(d.size) + ", " + d.device + ") will be erased "
                    + "and replaced with " + fileName(image) + ".\n\nDo you want to continue?",
                    "warning", ["Yes", "No"], function (index) {
            if (index !== 0)
                return;
            window.lastImage = window.image;
            window.lastDrive = d.name;
            Writer.write(window.image, d.path, verifyBox.checked);
        });
    }

    // a USB stick with a disc: the program's picture
    Canvas {
        id: picture
        x: 14
        y: 14
        width: 32
        height: 32
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
            // the disc
            ctx.fillStyle = "#c0c0c0"; ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.arc(11, 11, 10, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.fillStyle = "#80c0ff"; ctx.beginPath(); ctx.arc(11, 11, 6, -2.2, -0.6); ctx.lineTo(11, 11); ctx.fill();
            ctx.fillStyle = "#ffffff"; ctx.beginPath(); ctx.arc(11, 11, 2, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            // the arrow
            rect(19, 18, 2, 5, "#008000");
            ctx.fillStyle = "#008000"; ctx.beginPath(); ctx.moveTo(16, 22); ctx.lineTo(24, 22); ctx.lineTo(20, 26); ctx.fill();
            // the stick
            rect(22, 14, 9, 17, "#000000"); rect(23, 15, 7, 15, "#000080");
            rect(24, 9, 5, 5, "#000000"); rect(25, 10, 3, 4, "#c0c0c0");
        }
    }
    Text {
        x: picture.x + picture.width + 12
        y: 14
        width: parent.width - x - 14
        wrapMode: Text.WordWrap
        text: "Image Writer puts a disk image (such as a Linux .iso file) on a USB drive "
              + "or memory card, so a computer can start from it."
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    GroupBox95 {
        id: imageGroup
        title: "Image"
        x: 10
        y: 60
        width: parent.width - 20
        height: 86

        TextField95 {
            id: imageField
            x: 0
            y: 0
            width: parent.width - browse.width - 6
            text: window.image ? window.image : ""
            enabled: !Writer.busy
            onAccepted: window.image = text
        }
        Button95 {
            id: browse
            anchors.right: parent.right
            anchors.rightMargin: 0
            y: 0
            width: 82
            text: "B<u>r</u>owse..."
            enabled: !Writer.busy
            onClicked: fileDialog.openAt(window.image ? window.image.substring(0, window.image.lastIndexOf("/"))
                                                      : Win95Fs.homePath() + "/Downloads", "")
        }
        Text {
            x: 0
            y: 28
            width: parent.width
            wrapMode: Text.WordWrap
            color: window.image && !window.imageOk ? "#c00000" : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: !window.image ? "Choose the image to write."
                : !window.imageOk ? "This file can't be read."
                : !window.info.bootable ? bytes(window.info.size) + ". This image has no PC boot record, so a computer may not start from it."
                : bytes(window.info.size) + ", ready to write."
        }
    }

    GroupBox95 {
        id: driveGroup
        title: "Drive"
        x: 10
        y: imageGroup.y + imageGroup.height + 8
        width: parent.width - 20
        height: 94

        ComboBox95 {
            id: driveBox
            x: 0
            y: 0
            width: parent.width
            popupMenu: popup
            enabled: !Writer.busy && Writer.drives.length > 0
            model: Writer.drives.length ? Writer.drives.map(window.driveLabel) : ["(No USB drives)"]
            onActivated: (index) => { if (index < Writer.drives.length) window.chosenPath = Writer.drives[index].path; }
        }
        Text {
            x: 0
            y: 28
            width: parent.width
            wrapMode: Text.WordWrap
            color: !window.fits ? "#c00000" : Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: !Writer.drives.length ? "Plug in a USB drive or memory card. It appears here."
                : !window.fits ? "The image is bigger than this drive."
                : "Everything on the drive is erased."
        }
        CheckBox95 {
            id: verifyBox
            x: 0
            y: 50
            text: "Chec<u>k</u> the drive after writing"
            checked: true
            enabled: !Writer.busy
        }
    }

    GroupBox95 {
        id: statusGroup
        title: "Status"
        x: 10
        y: driveGroup.y + driveGroup.height + 8
        width: parent.width - 20
        height: 66

        Text {
            x: 0
            y: 0
            width: parent.width
            elide: Text.ElideRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: {
                switch (Writer.state) {
                case "preparing": return "Getting the drive ready...";
                case "writing": return "Writing: " + bytes(Writer.done) + " of " + bytes(Writer.total);
                case "syncing": return "Finishing writing (the drive is catching up)...";
                case "verifying": return "Checking: " + bytes(Writer.done) + " of " + bytes(Writer.total);
                case "done": return "Done.";
                case "failed": return "It didn't work.";
                case "cancelled": return "Cancelled. The drive may not be usable until it's written again.";
                default: return "Ready.";
                }
            }
        }
        ProgressBar95 {
            x: 0
            y: 18
            width: parent.width - rateText.width - 8
            height: 20
            value: Writer.state === "syncing" || Writer.state === "preparing" ? -1
                 : Writer.total > 0 && (Writer.state === "writing" || Writer.state === "verifying")
                   ? Math.floor(Writer.done * 100 / Writer.total)
                 : Writer.state === "done" ? 100 : 0
        }
        Text {
            id: rateText
            anchors.right: parent.right
            anchors.rightMargin: 0
            y: 21
            width: 120
            horizontalAlignment: Text.AlignRight
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: (Writer.state === "writing" || Writer.state === "verifying") && Writer.speed > 0
                  ? bytes(Writer.speed) + "/s, " + timeLeft((Writer.total - Writer.done) / Writer.speed)
                  : ""
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            width: 75
            text: "<u>W</u>rite"
            isDefault: true
            enabled: window.canWrite
            onClicked: window.startWrite()
        }
        Button95 {
            width: 75
            text: Writer.busy ? "Cancel" : "Close"
            enabled: Writer.state !== "syncing"
            onClicked: Writer.busy ? Writer.cancel() : window.close()
        }
    }

    // closing while writing would leave a half-written drive
    onClosing: (close) => {
        if (Writer.busy) {
            close.accepted = false;
            msgBox.show("Image Writer", "The drive is still being written. Cancel first, or wait until it's done.",
                        "warning", ["OK"], null);
        }
    }

    FileDialog95 {
        id: fileDialog
        acceptLabel: "Open"
        messageBox: msgBox
        popupMenu: popup
        filters: [
            { label: "Disk Images (*.iso, *.img, *.raw)", patterns: ["*.iso", "*.img", "*.raw", "*.ISO", "*.IMG"] },
            { label: "All Files (*.*)", patterns: ["*"] },
        ]
        onAccepted: (path) => window.image = path
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
