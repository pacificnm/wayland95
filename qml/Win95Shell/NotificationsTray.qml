import QtQuick
import Win95Ui as Ui
import Win95Shell

// Notifications: a speech balloon, with a red count of ones you missed (or
// "z" while Do not disturb is on). Click for the recent ones, Clear All and
// Do not disturb.
TrayIcon {
    id: tray95

    readonly property int unread: Notifications.unreadCount
    readonly property bool quiet: Ui.Wayland95Settings.doNotDisturb

    toolTip: (quiet ? "Do not disturb is on. " : "")
             + (unread === 0 ? "No new notifications" : unread === 1 ? "1 new notification" : unread + " new notifications")
    popupWidth: 300
    popupHeight: 330

    icon: Component {
        Item {
            Canvas {
                anchors.fill: parent
                property bool quiet: tray95.quiet
                onQuietChanged: requestPaint()
                onPaint: {
                    // a bell: gold (gray in Do Not Disturb, struck through)
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.lineWidth = 1;
                    ctx.strokeStyle = "#000000";
                    ctx.fillStyle = quiet ? "#c0c0c0" : "#f0c020";
                    ctx.beginPath();
                    ctx.moveTo(8, 1.5);                         // the top
                    ctx.bezierCurveTo(4.5, 1.5, 3.5, 4.5, 3.5, 7.5);
                    ctx.lineTo(3.5, 10);
                    ctx.lineTo(1.5, 12.5);                      // the flared rim
                    ctx.lineTo(14.5, 12.5);
                    ctx.lineTo(12.5, 10);
                    ctx.lineTo(12.5, 7.5);
                    ctx.bezierCurveTo(12.5, 4.5, 11.5, 1.5, 8, 1.5);
                    ctx.closePath();
                    ctx.fill();
                    ctx.stroke();
                    // a shine, the handle and the clapper
                    ctx.fillStyle = quiet ? "#e0e0e0" : "#fff0a0";
                    ctx.fillRect(5, 5, 1, 4);
                    ctx.fillStyle = "#000000";
                    ctx.fillRect(7, 0, 2, 2);
                    ctx.beginPath();
                    ctx.arc(8, 14, 1.6, 0, 2 * Math.PI);
                    ctx.fill();
                    if (quiet) {
                        ctx.strokeStyle = "#c00000";
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        ctx.moveTo(2, 2); ctx.lineTo(14, 14);
                        ctx.stroke();
                    }
                }
            }
            // how many you missed
            Rectangle {
                visible: tray95.unread > 0
                anchors.right: parent.right
                anchors.rightMargin: -3
                y: -3
                width: Math.max(10, badge.implicitWidth + 4)
                height: 10
                radius: 5
                color: "#d00000"
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: tray95.unread > 9 ? "9+" : tray95.unread
                    color: "#ffffff"
                    font.family: Theme.fontFamily
                    font.pixelSize: 8
                    font.bold: true
                }
            }
        }
    }

    onPopupOpened: Notifications.markAllRead()

    Text {
        x: 10
        y: 8
        text: "Notifications"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }

    Ui.Bevel {
        id: listBox
        x: 10
        y: 28
        width: parent.width - 20
        height: parent.height - y - 46
        style: "sunken"
        face: Ui.Theme.window

        ListView {
            id: list
            x: 2; y: 2
            width: parent.width - 4 - bar.width
            height: parent.height - 4
            clip: true
            interactive: false
            model: Notifications.history
            spacing: 1
            delegate: Item {
                id: entry
                required property var modelData
                width: list.width
                height: col.height + 8
                Rectangle {
                    anchors.fill: parent
                    color: entry.modelData.unread ? "#ffffe1" : "transparent"
                }
                Column {
                    id: col
                    x: 6
                    y: 4
                    width: parent.width - 12
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: entry.modelData.appName + "  •  "
                              + Qt.formatTime(new Date(entry.modelData.time), Ui.Wayland95Settings.clock24Hour ? "HH:mm" : "h:mm ap")
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize - 1
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: entry.modelData.summary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: entry.modelData.body
                        visible: text.length > 0
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#e0e0e0" }
            }
            WheelHandler { onWheel: (event) => bar.scrollBy(-event.angleDelta.y / 120 * 60) }
        }
        Ui.ScrollBar95 {
            id: bar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: list
            stepSize: 20
        }
        Text {
            anchors.centerIn: parent
            visible: Notifications.history.length === 0
            text: "No notifications"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    Ui.CheckBox95 {
        id: quietBox
        x: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 13
        text: "<u>D</u>o not disturb"
        onToggled: (on) => Ui.Wayland95Settings.doNotDisturb = on
    }
    Binding { target: quietBox; property: "checked"; value: tray95.quiet }
    Ui.Button95 {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        text: "<u>C</u>lear All"
        enabled: Notifications.history.length > 0
        onClicked: Notifications.clearHistory()
    }
}
