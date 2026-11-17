import QtQuick
import QtQuick.Window

// A dialog box as a window of its own (the compositor frames it), for
// programs whose window is too small to hold one (Dialog95 draws inside
// the window). Modal to its program's windows: the compositor keeps it in
// front of transientParent, and clicking that brings the keyboard back
// here (xdg-dialog-v1). Escape closes it; Enter is `accepted` (the
// default button's job).
//
//   DialogWindow95 {
//       transientParent: window
//       title: "Best Times"; width: 300; height: 110
//       onAccepted: close()
//       ...content...
//   }
Window {
    id: dialog

    signal accepted()

    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    color: Theme.face
    visible: false
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height

    function show() {
        visible = true;
        requestActivate();
    }

    Shortcut { sequence: "Escape"; onActivated: dialog.close() }
    Shortcut { sequences: ["Return", "Enter"]; onActivated: dialog.accepted() }
}
