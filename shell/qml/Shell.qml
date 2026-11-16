import QtQuick
import QtQuick.Window
import Win95Ui as Ui
import Win95Shell

// win95shell: Wayland 95's desktop, as panels (layer-shell surfaces) of the
// wlroots compositor (docs/architecture.md). The QML is the Qt
// compositor's, shared: Taskbar.qml, StartMenu.qml, the tray, the desktop
// icons. What's different is only where each part lives:
//
//   desktop      background layer: the backdrop and the icons
//   taskbar      top layer, along the bottom; windows stay clear of it
//   popupLayer   overlay, the whole screen, shown only while the Start
//                menu, a tray pop-up or the taskbar's window menu is open;
//                it has the keyboard then, and a click outside closes them
//   tipLayer     overlay, the whole screen, never takes clicks: tooltips
//   notificationLayer  top, bottom right above the taskbar, sized to the
//                pop-ups, shown while there are any; never takes the keyboard
//   osdLayer     overlay, the volume/brightness level, never takes clicks
//   dialog layers  (DialogLayer.qml) Run, Shut Down, messages, passwords
QtObject {
    id: shell

    property Window desktop: Window {
        LayerShell.layer: LayerShell.Background
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: 0 // the area above the taskbar
        LayerShell.keyboard: LayerShell.KeyboardOnDemand
        LayerShell.scope: "desktop"
        title: "Desktop"
        color: Ui.Wayland95Settings.backgroundColor
        width: 640; height: 480 // (the compositor gives the real size)
        visible: true

        DesktopBackground {
            anchors.fill: parent
        }
        DesktopIcons {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 100
        }
    }

    property Window taskbarWindow: Window {
        LayerShell.layer: LayerShell.Top
        LayerShell.anchors: LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: height
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "taskbar"
        title: "Taskbar"
        color: Theme.face
        width: 640
        height: Theme.taskbarHeight
        visible: true

        Taskbar {
            id: taskbar
            anchors.fill: parent
            popupParent: shell.popupArea
            tipParent: shell.tipArea
            onRunRequested: runDialog.open()
            onShutDownRequested: shutDownDialog.open()
            onHelpRequested: shellMessage.show("Wayland 95",
                "Wayland 95\n\nA Windows 95-style desktop for Wayland, built with Qt.\n\n"
                + "Programs started from the Start menu connect to WAYLAND_DISPLAY=" + ShellSession.socketName
                + (ShellSession.x11Display ? ", and X11 programs to DISPLAY=" + ShellSession.x11Display : "")
                + ".", "info", ["OK"])
        }
    }

    property Window popupLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1 // over the taskbar too
        LayerShell.keyboard: LayerShell.KeyboardExclusive
        LayerShell.scope: "popups"
        title: "Pop-ups"
        color: "transparent"
        width: 640; height: 480
        visible: taskbar.anyPopupVisible

        // the screen-sized parent of the pop-ups: sized before the layer is
        // first shown (its window's own content item isn't), since the tray
        // and menus work out where to go when they open
        Item {
            id: popupArea
            width: Screen.width
            height: Screen.height
            // a click outside the pop-ups closes them (as on the Qt compositor)
            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.AllButtons
                onPressed: taskbar.closePopups()
            }
        }
    }
    readonly property Item popupArea: popupArea

    property Window tipLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "tooltips"
        title: "Tooltips"
        color: "transparent"
        flags: Qt.WindowTransparentForInput
        width: 640; height: 480
        // shown only while a tooltip is (an empty overlay over the whole
        // screen would cost every frame of every window a blend)
        visible: taskbar.tipShowing
        Item {
            id: tipArea
            width: Screen.width
            height: Screen.height
        }
    }
    readonly property Item tipArea: tipArea

    // --- the Start menu's dialogs (as on the Qt compositor: Main.qml) -------
    property Window runLayer: DialogLayer {
        showing: runDialog.visible
        RunDialog {
            id: runDialog
            onFailed: (command, message) => shellMessage.show(command, message, "error", ["OK"],
                                                              runDialog.refocus)
        }
    }
    property Window shutDownLayer: DialogLayer {
        showing: shutDownDialog.visible
        ShutDownDialog {
            id: shutDownDialog
            onChosen: (option) => {
                console.info("win95shell: Shut Down:", option);
                if (option === "standby") {
                    Ui.Power.suspend();
                } else if (option === "hibernate") {
                    Ui.Power.hibernate();
                } else {
                    Ui.Wayland95Settings.playSound("exit");
                    if (option === "shutdown")
                        Ui.Power.powerOff();
                    else if (option === "restart")
                        Ui.Power.reboot();
                    else
                        ShellSession.exitDesktop();
                }
            }
        }
    }
    // a program needs a password (polkit), or NetworkManager a network's
    property Window passwordLayer: DialogLayer {
        showing: passwordDialog.visible
        PasswordDialog { id: passwordDialog }
    }
    property Window networkPasswordLayer: DialogLayer {
        showing: networkPasswordDialog.visible
        NetworkPasswordDialog { id: networkPasswordDialog }
    }
    // (created after the others: a message over the Run dialog is on top)
    property Window messageLayer: DialogLayer {
        showing: shellMessage.visible
        Ui.MessageBox95 { id: shellMessage }
    }

    // --- notifications ----------------------------------------------------
    // bottom right, just above the taskbar (the compositor keeps a panel
    // with no exclusive zone of its own clear of the taskbar's), newest
    // nearest it, at most four at once
    property Window notificationLayer: Window {
        LayerShell.layer: LayerShell.Top
        LayerShell.anchors: LayerShell.AnchorBottom | LayerShell.AnchorRight
        LayerShell.exclusiveZone: 0
        LayerShell.marginRight: 8
        LayerShell.marginBottom: 8
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "notifications"
        title: "Notifications"
        color: "transparent"
        width: 300
        height: Math.max(1, notificationStack.implicitHeight)
        visible: Notifications.popups.length > 0
        Column {
            id: notificationStack
            width: 300
            spacing: 6
            Repeater {
                model: Notifications.popups.slice(-4)
                NotificationPopup {
                    required property var modelData
                    notification: modelData
                }
            }
        }
    }
    property SystemNotices notices: SystemNotices {}

    // --- the volume and brightness keys -----------------------------------
    // the compositor sends them here (wayland95-shell-v1); a held key
    // repeats at the keyboard's rate (Control Panel > Keyboard)
    property Window osdLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorBottom
        LayerShell.exclusiveZone: 0
        LayerShell.marginBottom: 80
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "osd"
        title: "Level"
        color: "transparent"
        flags: Qt.WindowTransparentForInput
        width: levelOsd.width
        height: levelOsd.height
        visible: levelOsd.visible
        LevelOsd { id: levelOsd }
    }
    property DesktopKeys desktopKeys: DesktopKeys {
        onPressed: (key) => {
            shell.desktopKey(key);
            // (only the level keys repeat; a held power button acts once)
            if (key === DesktopKeys.VolumeUp || key === DesktopKeys.VolumeDown
                    || key === DesktopKeys.BrightnessUp || key === DesktopKeys.BrightnessDown) {
                keyRepeat.key = key;
                keyRepeat.interval = Ui.Wayland95Settings.keyRepeatDelay;
                keyRepeat.restart();
            }
        }
        onReleased: (key) => { if (key === keyRepeat.key) keyRepeat.stop(); }
    }
    property Timer keyRepeat: Timer {
        property int key: 0
        repeat: true
        onTriggered: {
            interval = Math.max(15, Math.round(1000 / Math.max(1, Ui.Wayland95Settings.keyRepeatRate)));
            shell.desktopKey(key);
        }
    }
    function desktopKey(key) {
        switch (key) {
        case DesktopKeys.BrightnessUp: levelOsd.changeBrightness(5); break;
        case DesktopKeys.BrightnessDown: levelOsd.changeBrightness(-5); break;
        case DesktopKeys.VolumeUp: levelOsd.changeVolume(0.05); break;
        case DesktopKeys.VolumeDown: levelOsd.changeVolume(-0.05); break;
        case DesktopKeys.VolumeMute: levelOsd.toggleMute(); break;
        case DesktopKeys.Print: shell.printScreen(); break;
        case DesktopKeys.Capture:
            if (screenCapture.active)
                screenCapture.close();
            else
                screenCapture.open();
            break;
        default: shell.powerPolicy.key(key); break;
        }
    }

    // --- Print Screen and the screen capture tool (Ctrl+Shift+5) --------
    // The tool is an overlay with the keyboard while it's open, gone while
    // the picture is taken (screencopy: ScreenCopy); its thumbnail has a
    // little panel of its own at the bottom right, so the desktop stays
    // usable while it shows.
    property Window captureLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1
        LayerShell.keyboard: LayerShell.KeyboardExclusive
        LayerShell.scope: "capture"
        title: "Screen Capture"
        color: "transparent"
        width: 640; height: 480
        visible: (screenCapture.active || screenCapture.countdown > 0) && !screenCapture.capturing
        Item {
            width: Screen.width
            height: Screen.height
            ScreenCapture {
                id: screenCapture
                thumbParent: thumbArea
                takePicture: function (r, destination) {
                    shell.captureId = ScreenCopy.take(Qt.rect(r.x, r.y, r.width, r.height),
                                                      destination === "Clipboard" ? "Clipboard" : ScreenCopy.folder(destination));
                }
                onWindowsWanted: ScreenCopy.listWindows()
            }
        }
    }
    property Window captureThumbLayer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorBottom | LayerShell.AnchorRight
        LayerShell.exclusiveZone: 0
        LayerShell.marginRight: 16
        LayerShell.marginBottom: 16
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "capture-thumbnail"
        title: "Screenshot"
        color: "transparent"
        width: Math.max(1, screenCapture.thumbWidth)
        height: Math.max(1, screenCapture.thumbHeight)
        visible: screenCapture.thumbShowing
        Item { id: thumbArea; anchors.fill: parent }
    }
    property int captureId: 0
    property int printId: 0
    property Connections screenCopy: Connections {
        target: ScreenCopy
        function onTaken(id, path) {
            if (id === shell.captureId) {
                screenCapture.pictureTaken(path);
            } else if (id === shell.printId) {
                Ui.Wayland95Settings.playSound(path ? "notification" : "critical");
                Notifications.notify("Wayland 95", path ? "Screenshot saved" : "The screenshot couldn't be saved",
                                     path ? path : ScreenCopy.printFolder(), "", 1, 5000);
            }
        }
        function onWindowsListed(windows) { screenCapture.windowRects = windows; }
    }
    function printScreen() {
        shell.printId = ScreenCopy.take(Qt.rect(0, 0, 0, 0), ScreenCopy.printFolder());
    }

    // --- the screen saver ----------------------------------------------
    property ScreenSaver screenSaver: ScreenSaver {
        onActiveChanged: if (active) taskbar.closePopups()
    }
    property PowerPolicy powerPolicy: PowerPolicy {
        onAskRequested: { taskbar.closePopups(); shutDownDialog.open(); }
    }

    // the "Start Wayland 95" sound
    Component.onCompleted: Ui.Wayland95Settings.playSound("startup")
}
