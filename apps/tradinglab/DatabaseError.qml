import QtQuick
import QtQuick.Window
import Win95Ui

// The database could not be opened: say so, and stop (nothing in it is changed)
Window {
    id: window
    title: "Trading Lab"
    width: 460
    height: 200
    visible: true
    color: Theme.face
    MessageBox95 { id: box }
    Component.onCompleted: box.show("Trading Lab", databaseFailure, "error", ["OK"], function () { Qt.quit(); })
}
