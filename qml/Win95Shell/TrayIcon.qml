import QtQuick
import Win95Ui as Ui
import Win95Shell

// One icon in the taskbar's notification area. Shows `icon` (a Component,
// 16x16), a tooltip on hover, and - if it has any children - a pop-up panel
// above the taskbar when clicked. Double-click usually opens a settings
// program (`doubleClicked`).
//
//   TrayIcon {
//       icon: Component { Canvas { ... } }
//       toolTip: "Volume"
//       popupWidth: 72; popupHeight: 178
//       Text { text: "pop-up content" }   // goes in the pop-up
//   }
Item {
    id: trayIcon

    required property Item tray     // the Tray this sits in
    property Component icon
    property string toolTip: ""
    property real popupWidth: 160
    property real popupHeight: 100
    default property alias popupContent: popupBody.data
    readonly property bool hasPopup: popupBody.children.length > 0
    readonly property bool popupOpen: popup.visible
    // scene position of the icon, for programs that want to know
    readonly property point scenePos: mapToItem(null, width / 2, 0)

    signal clicked(var mouse)
    signal doubleClicked()
    signal rightClicked(var mouse)
    signal wheel(int delta)
    signal popupOpened()

    width: 20
    height: 20

    function openPopup() {
        tray.closePopups();
        var parentItem = tray.popupParent;
        // just above the taskbar, kept on screen
        var p = trayIcon.mapToItem(parentItem, 0, 0);
        popup.x = Math.max(2, Math.min(parentItem.width - popup.width - 2, p.x + width / 2 - popup.width / 2));
        popup.y = p.y - popup.height - 6;
        popup.visible = true;
        popupOpened();
    }
    function closePopup() { popup.visible = false; }

    // 16x16 for icons; wider items (the clock) just set a bigger width
    Loader {
        anchors.centerIn: parent
        width: trayIcon.width - 4
        height: 16
        sourceComponent: trayIcon.icon
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onClicked: (m) => {
            tray.hideToolTip();
            if (m.button === Qt.RightButton) {
                trayIcon.rightClicked(m);
            } else {
                if (m.button === Qt.LeftButton && trayIcon.hasPopup)
                    popup.visible ? trayIcon.closePopup() : trayIcon.openPopup();
                trayIcon.clicked(m);
            }
        }
        onDoubleClicked: (m) => {
            if (m.button !== Qt.LeftButton)
                return;
            trayIcon.closePopup();
            trayIcon.doubleClicked();
        }
        onWheel: (w) => trayIcon.wheel(w.angleDelta.y)
        onContainsMouseChanged: {
            if (containsMouse && trayIcon.toolTip.length > 0 && !popup.visible)
                tray.showToolTip(trayIcon, trayIcon.toolTip);
            else
                tray.hideToolTip();
        }
    }

    // the pop-up panel, living in the desktop above everything
    Item {
        id: popup
        parent: trayIcon.tray.popupParent
        visible: false
        z: 100
        width: trayIcon.popupWidth
        height: trayIcon.popupHeight
        onVisibleChanged: if (visible) popupBody.forceActiveFocus()

        Ui.Bevel { anchors.fill: parent; style: "raised" }
        // swallow clicks so they don't fall through to the desktop
        MouseArea { anchors.fill: parent }
        FocusScope {
            id: popupBody
            anchors.fill: parent
            Keys.onEscapePressed: trayIcon.closePopup()
        }
    }
}
