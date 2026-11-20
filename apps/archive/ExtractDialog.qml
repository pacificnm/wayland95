import QtQuick
import Win95Ui

// Extract: which folder, all files or the selected ones, with their
// folders or not. Browse... picks the folder with folderDialog.
Dialog95 {
    id: dlg

    property FileDialog95 folderDialog
    property int selectionCount: 0
    property bool openAfter: true
    signal accepted(string dest, bool selectedOnly, bool useFolders, bool openAfter)

    title: "Extract"
    dialogWidth: 420
    dialogHeight: 214

    function open(dest) {
        destField.text = dest;
        allRadio.checked = selectionCount === 0;
        selectedRadio.checked = selectionCount > 0;
        show();
        destField.forceActiveFocus();
        destField.input.selectAll();
    }
    function accept() {
        var d = destField.text.trim();
        if (!d)
            return;
        close();
        accepted(d, selectedRadio.checked, foldersBox.checked, openBox.checked);
    }

    Text {
        x: 12; y: 12
        text: "E<u>x</u>tract to:"
        textFormat: Text.StyledText
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    TextField95 {
        id: destField
        x: 12; y: 30
        width: parent.width - 120
        onAccepted: dlg.accept()
        onEscaped: dlg.close()
    }
    Button95 {
        x: parent.width - 100; y: 30
        width: 88
        text: "Browse..."
        onClicked: if (dlg.folderDialog) dlg.folderDialog.openAt(Win95Fs.isDir(destField.text) ? destField.text : Win95Fs.homePath(), "")
    }

    Text {
        x: 12; y: 64
        text: "Files"
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }
    RadioButton95 {
        id: selectedRadio
        x: 20; y: 82
        text: "<u>S</u>elected files"
        enabled: dlg.selectionCount > 0
        onClicked: { checked = true; allRadio.checked = false; }
    }
    RadioButton95 {
        id: allRadio
        x: 20; y: 102
        text: "<u>A</u>ll files"
        checked: true
        onClicked: { checked = true; selectedRadio.checked = false; }
    }
    CheckBox95 {
        id: foldersBox
        x: 200; y: 82
        text: "<u>U</u>se folder names"
        checked: true
    }
    CheckBox95 {
        id: openBox
        x: 200; y: 102
        text: "<u>O</u>pen the folder afterwards"
        checked: dlg.openAfter
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        spacing: 6
        Button95 { text: "Extract"; isDefault: true; onClicked: dlg.accept() }
        Button95 { text: "Cancel"; onClicked: dlg.close() }
    }
    Keys.onReturnPressed: dlg.accept()
    Keys.onEnterPressed: dlg.accept()

    Connections {
        target: dlg.folderDialog
        enabled: dlg.visible
        function onAccepted(path) { destField.text = path; destField.forceActiveFocus(); }
    }
}
