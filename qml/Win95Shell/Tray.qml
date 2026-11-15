import QtQuick
import Win95Ui as Ui
import Win95Shell

// The taskbar's notification area: a sunken box at the right end holding
// other programs' icons (StatusNotifierItem), Wayland 95's own indicators
// (network, Bluetooth, battery, volume) and the clock. Control Panel >
// Taskbar chooses which of them show.
//
// To add a built-in indicator: make a TrayIcon (see VolumeTray.qml), add it
// to the Row below with `tray: area`, and give it a show/hide setting.
Ui.Bevel {
    id: area

    // where pop-ups, tooltips and menus go (the desktop, above everything)
    required property Item popupParent

    readonly property bool popupOpen: anyPopupOpen() || clock.popupOpen || appMenu.visible
    function anyPopupOpen() {
        for (var i = 0; i < icons.children.length; ++i)
            if (icons.children[i].popupOpen)
                return true;
        return false;
    }
    function closePopups() {
        for (var i = 0; i < icons.children.length; ++i)
            if (icons.children[i].closePopup)
                icons.children[i].closePopup();
        clock.closePopup();
        appMenu.close();
    }

    style: "sunken"
    visible: row.width > 0
    width: row.width + 6
    height: 22

    Row {
        id: row
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Row {
            id: icons
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            // other programs' icons, oldest first
            Repeater {
                model: Ui.Wayland95Settings.showAppIcons ? AppTray : null
                delegate: TrayIcon {
                    id: appIcon
                    required property string key
                    required property string title
                    required property string description
                    required property string iconSource
                    required property string status
                    tray: area
                    // "Passive" means "nothing to see right now"
                    visible: status !== "Passive"
                    width: visible ? 20 : 0
                    toolTip: description.length > 0 ? description : title
                    icon: Component {
                        Item {
                            Image {
                                anchors.fill: parent
                                source: appIcon.iconSource
                                sourceSize: Qt.size(32, 32)
                                smooth: true
                                visible: status === Image.Ready
                            }
                            // no icon: a generic program window
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 1
                                visible: appIcon.iconSource.length === 0
                                color: "#ffffff"
                                border.color: "#000000"
                                Rectangle { width: parent.width; height: 4; color: "#000080" }
                            }
                        }
                    }
                    onClicked: (m) => {
                        var p = appIcon.scenePos;
                        if (m.button === Qt.MiddleButton)
                            AppTray.secondaryActivate(key, p.x, p.y);
                        else
                            AppTray.activate(key, p.x, p.y);
                    }
                    onRightClicked: {
                        var p = appIcon.scenePos;
                        area.menuAnchor = appIcon.mapToItem(area.popupParent, 0, 0);
                        AppTray.requestMenu(key, p.x, p.y);
                    }
                    onWheel: (delta) => AppTray.scroll(key, delta)
                }
            }

            NotificationsTray {
                tray: area
                visible: Ui.Wayland95Settings.showNotifications
            }
            NetworkTray {
                tray: area
                visible: Ui.Wayland95Settings.showNetwork && Ui.NetworkStatus.available
            }
            BluetoothTray {
                tray: area
                visible: Ui.Wayland95Settings.showBluetooth && Ui.Bluetooth.available
            }
            BatteryTray {
                tray: area
                visible: Ui.Wayland95Settings.showBattery && Ui.Battery.present
            }
            VolumeTray {
                tray: area
                visible: Ui.Wayland95Settings.showVolume && Ui.Audio.available
            }
        }

        ClockTray {
            id: clock
            tray: area
            visible: Ui.Wayland95Settings.showClock
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ----------------------------------------------------------- tooltips
    function showToolTip(item, text) {
        tipText.text = text;
        tipTimer.target = item;
        tipTimer.restart();
    }
    function hideToolTip() {
        tipTimer.stop();
        tip.visible = false;
    }
    Timer {
        id: tipTimer
        property Item target
        interval: 500
        onTriggered: {
            if (!target)
                return;
            var p = target.mapToItem(area.popupParent, target.width / 2, 0);
            tip.x = Math.max(2, Math.min(area.popupParent.width - tip.width - 2, p.x - tip.width / 2));
            tip.y = p.y - tip.height - 4;
            tip.visible = true;
        }
    }
    Rectangle {
        id: tip
        parent: area.popupParent
        visible: false
        z: 200
        width: tipText.implicitWidth + 8
        height: tipText.implicitHeight + 4
        color: "#ffffe1"
        border.color: "#000000"
        Text {
            id: tipText
            x: 4
            y: 2
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // ------------------------------------- right-click menus of programs
    property point menuAnchor
    function menuItems(key, nodes) {
        var list = [];
        for (var i = 0; i < nodes.length; ++i) {
            var n = nodes[i];
            if (n.separator) {
                list.push({ separator: true });
                continue;
            }
            list.push({
                text: n.label + (n.children.length > 0 ? "  ▸" : ""),
                enabled: n.enabled,
                checked: n.checkable && n.checked,
                // a submenu opens in place of its parent
                action: n.children.length > 0
                    ? (function (children) { return function () { area.openMenu(key, children); }; })(n.children)
                    : (function (id) { return function () { AppTray.menuItemClicked(key, id); }; })(n.id),
            });
        }
        return list;
    }
    function openMenu(key, nodes) {
        if (nodes.length > 0)
            appMenu.popup(menuAnchor.x, menuAnchor.y, menuItems(key, nodes));
    }
    Connections {
        target: AppTray
        function onMenuReady(key, items, x, y) { area.openMenu(key, items); }
    }
    Ui.Menu95 {
        id: appMenu
        parent: area.popupParent
    }
}
