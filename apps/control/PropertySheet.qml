import QtQuick
import QtQuick.Window
import Win95Ui

// A Win95 property sheet: a fixed-size window with tabs and OK / Cancel /
// Apply. Applets put one page per tab as children, edit local copies of
// the settings, set `dirty` when something changes, and write the
// settings in onApplyRequested.
//
// Dialogs that must cover the whole window (file pickers) go in `extras`.
Window {
    id: sheet

    property alias titles: tabs.titles
    property alias currentTab: tabs.currentIndex
    default property alias pages: tabs.pages
    property alias extras: extrasLayer.data
    property alias popupMenu: popup
    property alias messageBox: msgBox

    // false for read-only sheets: Apply stays disabled
    property bool canApply: true
    property bool dirty: false

    signal applyRequested()

    function apply() {
        if (dirty && canApply) {
            applyRequested();
            dirty = false;
        }
    }

    width: 404
    height: 432
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    visible: true
    color: Theme.face

    Shortcut {
        sequence: "Esc"
        enabled: !msgBox.visible && !popup.visible
        onActivated: sheet.close()
    }

    TabWidget95 {
        id: tabs
        x: 6
        y: 8
        width: parent.width - 12
        height: parent.height - y - 42
        focus: true
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 9
        spacing: 6

        Button95 {
            text: "OK"
            isDefault: true
            onClicked: {
                sheet.apply();
                sheet.close();
            }
        }
        Button95 {
            text: "Cancel"
            onClicked: sheet.close()
        }
        Button95 {
            text: "Apply"
            enabled: sheet.canApply && sheet.dirty
            onClicked: sheet.apply()
        }
    }

    // full-window dialogs supplied by the applet
    Item {
        id: extrasLayer
        anchors.fill: parent
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    // W95_CONTROL_GRAB: every tab saved as a picture, for the tests
    Timer {
        id: grabTimer
        running: typeof testGrabDir !== "undefined" && testGrabDir !== "" && sheet.visible
        interval: 2500
        onTriggered: {
            var f = testGrabDir + "/" + sheet.title.replace(/[^A-Za-z0-9]+/g, "-") + "-" + tabs.currentIndex + ".png";
            sheet.contentItem.grabToImage(function (r) {
                r.saveToFile(f);
                console.log("grabbed " + f);
                if (tabs.currentIndex < tabs.titles.length - 1) {
                    tabs.currentIndex++;
                    grabTimer.interval = 800;
                    grabTimer.restart();
                }
            });
        }
    }
}
