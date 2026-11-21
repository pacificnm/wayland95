import QtQuick
import Win95Ui

// Disk Manager's questions, one dialog for each `mode`:
//   "new"    a new partition in free space: its size, filesystem and label
//   "format" a filesystem and label for a partition (everything on it goes)
//   "label"  a new label
//   "table"  a new partition table for a whole disk: GPT or MBR
// OK sends `done` with { size (bytes), fsType, label, table }; the window
// asks "are you sure?" where something is erased.
Dialog95 {
    id: dlg

    property string mode: "new"
    property var part: null          // the partition, or the free space
    property var drive: null
    property Menu95 popupMenu
    signal done(var values)

    readonly property int maxMB: part ? Math.max(1, Math.floor(part.size / 1048576)) : 1
    // "None" is only for a new partition
    readonly property var fsChoices: {
        var list = Disks.filesystems.slice();
        if (mode === "new")
            list.push({ type: "", label: "None (don't format it)" });
        return list;
    }
    readonly property string fsType: fsChoices.length ? fsChoices[Math.max(0, fsBox.currentIndex)].type : "ext4"
    readonly property string labelProblem: mode === "table" ? "" : Disks.checkLabel(mode === "label" && part ? part.fsType : fsType,
                                                                                  labelField.text)

    title: mode === "new" ? "New Partition" : mode === "format" ? "Format" : mode === "label" ? "Change Label" : "New Partition Table"
    dialogWidth: 380
    dialogHeight: mode === "new" ? 250 : mode === "format" ? 220 : mode === "label" ? 165 : 210

    function bytes(n) {
        if (n >= 1e12) return (n / 1e12).toFixed(2) + " TB";
        if (n >= 1e9) return (n / 1e9).toFixed(1) + " GB";
        if (n >= 1e6) return (n / 1e6).toFixed(0) + " MB";
        return Math.round(n / 1e3) + " KB";
    }

    function ask(m, p, d) {
        mode = m;
        part = p;
        drive = d;
        sizeBox.setValue(maxMB);
        // a USB stick or memory card: FAT32 or exFAT, for any computer
        var preferred = d && (d.kind === "usb" || d.kind === "card") ? (p && p.size > 32e9 ? "exfat" : "vfat") : "ext4";
        if (m === "format" && p && p.fsType)
            preferred = p.fsType;
        fsBox.currentIndex = 0;
        for (var i = 0; i < fsChoices.length; ++i)
            if (fsChoices[i].type === preferred)
                fsBox.currentIndex = i;
        labelField.text = m === "label" && p ? p.label : "";
        gptButton.checked = true;
        show();
        if (m === "new")
            sizeBox.forceActiveFocus();
        else if (m !== "table")
            labelField.input.forceActiveFocus();
    }
    function accept() {
        if (labelProblem)
            return;
        var size = part ? part.size : 0;
        if (mode === "new" && sizeBox.value < maxMB)
            size = sizeBox.value * 1048576;
        close();
        done({ size: size, fsType: fsType, label: labelField.text, table: gptButton.checked ? "gpt" : "dos" });
    }

    readonly property real topMargin: 10

    // what it's about
    Text {
        id: intro
        x: 12
        y: dlg.topMargin
        width: parent.width - 24
        wrapMode: Text.Wrap
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        text: !dlg.part && dlg.mode !== "table" ? ""
            : dlg.mode === "new" ? "A new partition in " + bytes(dlg.part.size) + " of free space on " + (dlg.drive ? dlg.drive.name : "the disk") + "."
            : dlg.mode === "format" ? "Formatting " + dlg.part.device + " (" + bytes(dlg.part.size) + ") erases everything on it."
            : dlg.mode === "label" ? "The name shown for " + dlg.part.device + " in the file manager:"
            : "Everything on " + (dlg.drive ? dlg.drive.name + " (" + dlg.drive.device + ")" : "the disk")
              + " is erased, leaving it empty for new partitions."
    }

    // size (new)
    Text {
        visible: dlg.mode === "new"
        x: 12
        y: intro.y + intro.height + 14
        text: "<u>S</u>ize (MB):"
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    SpinBox95 {
        id: sizeBox
        visible: dlg.mode === "new"
        x: 110
        y: intro.y + intro.height + 10
        width: 100
        from: 1
        to: dlg.maxMB
    }
    Text {
        visible: dlg.mode === "new"
        x: sizeBox.x + sizeBox.width + 8
        y: sizeBox.y + 4
        text: "of " + dlg.maxMB + " MB"
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    // filesystem (new, format)
    Text {
        visible: dlg.mode === "new" || dlg.mode === "format"
        x: 12
        y: fsBox.y + 4
        text: "<u>F</u>ile system:"
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    ComboBox95 {
        id: fsBox
        visible: dlg.mode === "new" || dlg.mode === "format"
        x: 110
        y: dlg.mode === "new" ? sizeBox.y + 30 : intro.y + intro.height + 10
        width: parent.width - x - 12
        popupMenu: dlg.popupMenu
        model: dlg.fsChoices.map(function (f) { return f.label; })
    }

    // label (new, format, label)
    Text {
        visible: dlg.mode !== "table"
        x: 12
        y: labelField.y + 4
        text: "<u>L</u>abel:"
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    TextField95 {
        id: labelField
        visible: dlg.mode !== "table"
        x: 110
        y: dlg.mode === "label" ? intro.y + intro.height + 10 : fsBox.y + 30
        width: parent.width - x - 12
        enabled: dlg.mode === "label" || dlg.fsType !== ""
        onAccepted: dlg.accept()
        onEscaped: dlg.close()
    }
    Text {
        visible: dlg.mode !== "table"
        x: 110
        y: labelField.y + labelField.height + 4
        width: parent.width - x - 12
        wrapMode: Text.Wrap
        color: dlg.labelProblem ? "#c00000" : Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        text: dlg.labelProblem ? dlg.labelProblem
            : dlg.mode === "label" || dlg.fsType !== "" ? "Optional." : ""
    }

    // table
    Column {
        visible: dlg.mode === "table"
        x: 12
        y: intro.y + intro.height + 12
        width: parent.width - 24
        spacing: 6
        RadioButton95 {
            id: gptButton
            text: "<u>G</u>PT (recommended: any size, 128 partitions)"
            checked: true
            onClicked: { checked = true; mbrButton.checked = false; }
        }
        RadioButton95 {
            id: mbrButton
            text: "<u>M</u>BR (old computers: up to 2 TB, 4 partitions)"
            checked: !gptButton.checked
            onClicked: { checked = true; gptButton.checked = false; }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 {
            text: "OK"
            isDefault: true
            enabled: !dlg.labelProblem
            onClicked: dlg.accept()
        }
        Button95 { text: "Cancel"; onClicked: dlg.close() }
    }
}
