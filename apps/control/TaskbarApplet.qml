import QtQuick
import Win95Ui
import Win95Control

// Taskbar Properties: the clock, and which icons the notification area
// shows, with a preview of the taskbar.
PropertySheet {
    id: sheet

    title: "Taskbar Properties"
    titles: ["Taskbar Options"]

    property bool showClock: Wayland95Settings.showClock
    property bool clock24: Wayland95Settings.clock24Hour
    property bool showVolume: Wayland95Settings.showVolume
    property bool showNetwork: Wayland95Settings.showNetwork
    property bool showBluetooth: Wayland95Settings.showBluetooth
    property bool showBattery: Wayland95Settings.showBattery
    property bool showAppIcons: Wayland95Settings.showAppIcons
    property bool showNotifications: Wayland95Settings.showNotifications

    onApplyRequested: {
        Wayland95Settings.showClock = showClock;
        Wayland95Settings.clock24Hour = clock24;
        Wayland95Settings.showVolume = showVolume;
        Wayland95Settings.showNetwork = showNetwork;
        Wayland95Settings.showBluetooth = showBluetooth;
        Wayland95Settings.showBattery = showBattery;
        Wayland95Settings.showAppIcons = showAppIcons;
        Wayland95Settings.showNotifications = showNotifications;
    }

    Item {
        // preview: a corner of the desktop with a window and the taskbar
        Bevel {
            id: preview
            width: parent.width
            height: 130
            style: "sunken"
            face: "#008080"

            Bevel {
                x: 30; y: 14
                width: 180; height: 74
                style: "raised"
                Rectangle {
                    x: 3; y: 3
                    width: parent.width - 6; height: 14
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.titleActiveStart }
                        GradientStop { position: 1; color: Theme.titleActiveEnd }
                    }
                }
                Rectangle { x: 3; y: 18; width: parent.width - 6; height: parent.height - 21; color: Theme.window }
            }

            Bevel {
                x: 2
                y: parent.height - height - 2
                width: parent.width - 4
                height: 26
                style: "raised"

                Bevel {
                    x: 3; y: 3; width: 54; height: 20
                    style: "raised"
                    Row {
                        anchors.centerIn: parent
                        spacing: 3
                        Grid {
                            columns: 2
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle { width: 5; height: 5; color: "#ff0000" }
                            Rectangle { width: 5; height: 5; color: "#00a000" }
                            Rectangle { width: 5; height: 5; color: "#0000ff" }
                            Rectangle { width: 5; height: 5; color: "#ffcc00" }
                        }
                        Text { text: "Start"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.bold: true }
                    }
                }
                Bevel {
                    visible: sheet.showClock
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    y: 4
                    width: clockText.implicitWidth + 12
                    height: 18
                    style: "thinSunken"
                    Text {
                        id: clockText
                        anchors.centerIn: parent
                        text: Qt.formatTime(new Date(), sheet.clock24 ? "HH:mm" : "h:mm ap")
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }

        Column {
            y: preview.height + 16
            spacing: 8
            CheckBox95 {
                text: "Show <u>C</u>lock"
                checked: sheet.showClock
                onToggled: (on) => { sheet.showClock = on; sheet.dirty = true; }
            }
            CheckBox95 {
                text: "Use <u>2</u>4-hour clock"
                checked: sheet.clock24
                enabled: sheet.showClock
                opacity: enabled ? 1 : 0.5
                onToggled: (on) => { sheet.clock24 = on; sheet.dirty = true; }
            }
        }

        GroupBox95 {
            y: preview.height + 80
            width: parent.width
            height: 112
            title: "Notification area"


            Grid {
                columns: 2
                columnSpacing: 24
                rowSpacing: 8
                CheckBox95 {
                    text: "<u>V</u>olume"
                    checked: sheet.showVolume
                    onToggled: (on) => { sheet.showVolume = on; sheet.dirty = true; }
                }
                CheckBox95 {
                    text: "<u>N</u>etwork"
                    checked: sheet.showNetwork
                    onToggled: (on) => { sheet.showNetwork = on; sheet.dirty = true; }
                }
                CheckBox95 {
                    text: "<u>B</u>luetooth"
                    checked: sheet.showBluetooth
                    onToggled: (on) => { sheet.showBluetooth = on; sheet.dirty = true; }
                }
                CheckBox95 {
                    text: "B<u>a</u>ttery (laptops)"
                    checked: sheet.showBattery
                    onToggled: (on) => { sheet.showBattery = on; sheet.dirty = true; }
                }
                CheckBox95 {
                    text: "No<u>t</u>ifications"
                    checked: sheet.showNotifications
                    onToggled: (on) => { sheet.showNotifications = on; sheet.dirty = true; }
                }
                CheckBox95 {
                    text: "<u>O</u>ther programs' icons"
                    checked: sheet.showAppIcons
                    onToggled: (on) => { sheet.showAppIcons = on; sheet.dirty = true; }
                }
            }
        }
    }
}
