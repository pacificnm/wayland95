import QtQuick
import QtQuick.Window
import Win95Ui

// App.chooseFile(), chooseFiles(), chooseFolder() and chooseSaveFile(): the
// Win95 Open, Select Folder and Save As boxes, in a window of their own
// (a form may be too small to hold one). `answered` once: the paths
// chosen, or ok false when it was cancelled or closed.
Window {
    id: win

    property string mode: "open"      // or "save", "folder"
    property bool multiple: false
    property string dialogTitle: ""
    property string startFolder: ""
    property string startName: ""
    property var filters: []
    property string suffix: ""
    property bool done: false
    signal answered(bool ok, var paths)

    // (tests and code can answer it too: box.choose("/home/me/a.mp3"), box.cancel())
    function answer(ok, paths) {
        if (done)
            return;
        done = true;
        visible = false;
        // (the program deletes the window once the answer's given)
        answered(ok, ok ? paths : []);
    }
    function choose(paths) { answer(true, Array.isArray(paths) ? paths : [String(paths)]); }
    function cancel() { answer(false, []); }

    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 440
    height: 289
    minimumWidth: 360
    minimumHeight: 240
    color: Theme.face
    title: dialog.title
    onClosing: answer(false, [])

    FileDialog95 {
        id: dialog
        anchors.fill: parent
        embedded: true
        mode: win.mode
        multiple: win.multiple
        filters: win.filters.length ? win.filters : [{ label: "All Files (*.*)", patterns: ["*"] }]
        defaultSuffix: win.suffix
        messageBox: msgBox
        popupMenu: popup
        Component.onCompleted: {
            if (win.dialogTitle)
                title = win.dialogTitle;
            openAt(win.startFolder, win.startName);
        }
        onAcceptedPaths: (paths) => win.answer(true, paths)
        // Cancel (or Esc) closes the box: that's a no (accepting closes it
        // too, just before the paths: so only once that's past)
        onClosed: Qt.callLater(function () { if (!win.done) win.answer(false, []); })
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
