import QtQuick
import Win95Ui as Ui
import Win95Shell

// The desktop's notifications, shared by the Qt compositor (Main.qml) and
// win95shell (Shell.qml): the sound when one arrives, Do Not Disturb, and
// the desktop's own notices: network, Bluetooth and battery changes,
// updates found in the background (UpdateCheck), and problem reports.
// Where the pop-ups are drawn is each desktop's own business.
QtObject {
    id: notices

    // the desktop itself: its crash is "Wayland 95 restarted"
    readonly property var desktopPrograms: ["win95compositor", "win95shell", "wayland95-compositor"]

    default property list<QtObject> contents: [
        // Do Not Disturb, and the sound when one arrives
        Binding { target: Notifications; property: "quiet"; value: Ui.Wayland95Settings.doNotDisturb },

        Connections {
            target: Notifications
            function onArrived(notification, shown) {
                if (shown && !notification.silent)
                    Ui.Wayland95Settings.playSound(notification.urgency === 2 ? "exclamation" : "notification");
            }
        },

        // The desktop's own notifications. The first few seconds are skipped,
        // so starting up doesn't announce the state things were already in.
        QtObject {
            id: systemNotices
            property bool ready: false
            property string netState: ""
            property string netName: ""
            property var btConnected: []
            property int batteryWarnedAt: 101

            function connectedDevices() {
                return Ui.Bluetooth.devices.filter(function (d) { return d.connected; }).map(function (d) { return d.name; });
            }
            function networkChanged() {
                var state = Ui.NetworkStatus.state, name = Ui.NetworkStatus.connectionName;
                var wasUp = netState === "connected" || netState === "limited";
                var isUp = state === "connected" || state === "limited";
                if (ready) {
                    if (isUp && (!wasUp || name !== netName))
                        Notifications.notify("Network", "Connected to " + name,
                                             state === "limited" ? "Connected, but there's no internet access." : "", "", 0);
                    else if (wasUp && !isUp && state !== "connecting")
                        Notifications.notify("Network", "Disconnected",
                                             netName ? "No longer connected to " + netName + "." : "", "", 1);
                }
                netState = state;
                netName = name;
            }
            function bluetoothChanged() {
                var now = connectedDevices();
                if (ready) {
                    now.filter(function (n) { return btConnected.indexOf(n) < 0; }).forEach(function (n) {
                        Notifications.notify("Bluetooth", "Connected to " + n, "", "", 0);
                    });
                    btConnected.filter(function (n) { return now.indexOf(n) < 0; }).forEach(function (n) {
                        Notifications.notify("Bluetooth", "Disconnected from " + n, "", "", 0);
                    });
                }
                btConnected = now;
            }
            function batteryChanged() {
                if (!Ui.Battery.present)
                    return;
                if (Ui.Battery.pluggedIn) {
                    batteryWarnedAt = 101; // warn again next time
                    return;
                }
                var p = Ui.Battery.percent;
                var level = p <= 5 ? 5 : p <= 10 ? 10 : 101;
                if (level < batteryWarnedAt) {
                    batteryWarnedAt = level;
                    Notifications.notify("Power", "Battery low", p + "% remaining. Plug in your computer soon"
                                         + (level === 5 ? ", or it will turn off." : "."), "", 2);
                }
            }
        },

        Timer {
            interval: 5000
            running: true
            onTriggered: {
                systemNotices.networkChanged();
                systemNotices.bluetoothChanged();
                systemNotices.ready = true;
                systemNotices.batteryChanged();
            }
        },

        Connections { target: Ui.NetworkStatus; function onChanged() { systemNotices.networkChanged(); } },

        Connections { target: Ui.Bluetooth; function onDevicesChanged() { systemNotices.bluetoothChanged(); } },

        Connections { target: Ui.Battery; function onChanged() { systemNotices.batteryChanged(); } },

        // Windows Update's tray notice: updates found in the background
        UpdateCheck {
            id: updateCheck
            property real notice: 0
            property int noticedCount: 0
            onChecked: {
                // only when there's something new to say
                if (count === 0 || count === noticedCount)
                    return;
                noticedCount = count;
                var what = desktopUpdate ? "A new version of Wayland 95" + (count > 1 ? " and " + (count - 1) + " other updates are" : " is")
                                         : (count === 1 ? "1 update is" : count + " updates are");
                notice = Notifications.notifyWithActions(
                    "Wayland 95 Update", "Updates are ready to install",
                    what + " available for your computer" + (securityCount > 0 ? ", including security fixes" : "")
                        + ". Click here to install them.",
                    ["default", "Install", "install", "Install Updates"], 1, 15000);
            }
        },

        Connections {
            target: Notifications
            function onActionInvoked(id, key) {
                if (id === updateCheck.notice)
                    Launcher.launch("win95control", ["--applet", "programs", "--tab", "1", "--check"]);
                else if (id === problemNotice.id)
                    Launcher.launch("win95control", ["--applet", "problems"]);
                else if (driveNotice.drives[id])
                    driveNotice.open(driveNotice.drives[id]);
            }
        },

        // a drive plugged in (Drives: UDisks2): Open mounts it and shows it
        // in the file manager, like Windows' AutoPlay
        QtObject {
            id: driveNotice
            property var drives: ({}) // notification id -> drive id

            function open(id) {
                Ui.Drives.mount(id, function (ok, result) {
                    if (ok)
                        Launcher.launch("win95files", [result]);
                    else
                        Notifications.notify("Drives", "The drive couldn't be opened", result, "", 1);
                });
            }
        },

        Connections {
            target: Ui.Drives
            function onDriveAdded(drive) {
                // Control Panel > Backup: back up now if this is its drive and one is due
                Launcher.launch("wayland95-backup", ["--due"]);
                var n = Notifications.notifyWithActions(
                    "Drives", drive.name + " was plugged in",
                    (drive.encrypted ? "An encrypted disk" : "A removable disk")
                        + (drive.sizeText ? " (" + drive.sizeText + ")" : "")
                        + (drive.driveName ? ", " + drive.driveName : "") + ". Click here to open it.",
                    ["default", "Open", "open", "Open Folder"], 1, 15000);
                var map = driveNotice.drives;
                map[n] = drive.id;
                driveNotice.drives = map;
            }
            function onDriveRemoved(drive) {
                if (drive.mounted && !drive.ejected)
                    Notifications.notify("Drives", drive.name + " was removed without ejecting it",
                                         "Next time, right-click it in My Computer and choose Eject first, "
                                         + "so nothing that was being saved to it is lost.", "", 1);
            }
        },

        // Problem reports (apps/common/crashhandler.h): a program that crashed,
        // or the desktop itself before wayland95-session restarted it. Each
        // gets one notice; Details opens Control Panel > Problem Reports.
        QtObject {
            id: problemNotice
            property real id: 0

            function tell(reports) {
                if (reports.length === 0)
                    return;
                var desktop = reports.filter(function (r) { return notices.desktopPrograms.indexOf(r.program) >= 0; });
                var r = desktop.length ? desktop[0] : reports[0];
                var when = new Date(r.time).toLocaleTimeString(Qt.locale(), "h:mm AP");
                var body = desktop.length
                        ? "The Wayland 95 desktop stopped unexpectedly at " + when
                          + " and was started again. Programs that were open had to close."
                        : r.name + " closed unexpectedly at " + when + ".";
                if (reports.length > 1)
                    body += " (" + reports.length + " problem reports.)";
                id = Notifications.notifyWithActions("Wayland 95", desktop.length ? "Wayland 95 restarted" : r.name + " crashed",
                                                     body + " Click here to see the report.",
                                                     ["default", "Details", "details", "Details..."], 1, 20000);
                Ui.ProblemReports.markAllSeen();
            }
        },

        Connections {
            target: Ui.ProblemReports
            function onReportAdded(report) {
                problemNotice.tell([report]);
            }
        },

        // reports from before this start (the desktop's own crash among them)
        Timer {
            interval: 4000
            running: true
            onTriggered: problemNotice.tell(Ui.ProblemReports.unseen())
        }
    ]
}
