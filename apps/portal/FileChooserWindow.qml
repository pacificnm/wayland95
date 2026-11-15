import QtQuick
import QtQuick.Window
import Win95Ui

// One file dialog for a program, as its own window: the Win95 Open /
// Save As / Select Folder dialog. Answers the portal through
// portal.done(requestKey, ...).
Window {
    id: win

    property QtObject portal
    property string requestKey
    property string mode: "open"
    property string dialogTitle
    property string acceptLabel
    property string startFolder
    property string startName
    property var filters: []
    property int filterIndex: 0
    property bool multiple: false
    property bool answered: false

    function answer(ok, paths) {
        if (answered)
            return;
        answered = true;
        if (portal)
            portal.done(requestKey, ok, paths, dialog.filterIndex);
    }

    width: 440
    height: 289
    minimumWidth: 360
    minimumHeight: 240
    visible: true
    color: Theme.face
    title: dialog.title
    onClosing: answer(false, [])

    FileDialog95 {
        id: dialog
        anchors.fill: parent
        embedded: true
        mode: win.mode
        multiple: win.multiple
        acceptLabel: win.acceptLabel
        filters: win.filters.length ? win.filters : [{ label: "All Files (*.*)", patterns: ["*"] }]
        filterIndex: win.filterIndex
        defaultSuffix: ""
        messageBox: msgBox
        popupMenu: popup
        Component.onCompleted: {
            if (win.dialogTitle)
                title = win.dialogTitle;
            openAt(win.startFolder, win.startName);
        }
        onAcceptedPaths: (paths) => win.answer(true, paths)
        // Cancel (or Esc) closes the dialog: that's a no. (Accepting closes
        // it too, just before giving the paths: so only once that's past.)
        onClosed: Qt.callLater(function () { if (!win.answered) win.answer(false, []); })
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
