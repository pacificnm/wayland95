import QtQuick
import QtQuick.Window
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell
import Win95Ui as Ui
import Win95Shell

// The compositor itself. WaylandCompositor is the non-visual root; all the
// actual pixels come from the WaylandOutput's `window`, which hosts our
// Win95-styled desktop + taskbar + per-window chrome.
WaylandCompositor {
    id: compositor

    // Chosen in main.cpp. Left empty, Qt would reuse $WAYLAND_DISPLAY and
    // collide with the host compositor's socket when running nested.
    socketName: compositorSocketName

    WaylandOutput {
        id: output
        compositor: compositor
        sizeFollowsWindow: true

        window: Window {
            id: outputWindow
            width: 1280
            height: 800
            visible: true
            title: "Wayland 95"
            color: "black"

            Item {
                id: desktop
                anchors.fill: parent

                DesktopBackground {
                    anchors.fill: parent
                }

                DesktopIcons {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.bottom: taskbar.top
                    anchors.right: parent.right // (more icons: more columns)
                }

                // every mapped client's WindowFrame gets parented here
                Item {
                    id: windowLayer
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: taskbar.top
                }

                // catches clicks outside the Start menu to close it; sits
                // above the icons/windows but below the taskbar itself
                MouseArea {
                    anchors.fill: windowLayer
                    enabled: taskbar.popupOpen
                    visible: enabled
                    onClicked: taskbar.closePopups()
                }

                Taskbar {
                    id: taskbar
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    onRunRequested: runDialog.open()
                    onShutDownRequested: shutDownDialog.open()
                    onHelpRequested: shellMessage.show("Wayland 95",
                        "Wayland 95\n\nA Windows 95-style desktop for Wayland, built with Qt.\n\n"
                        + "Programs started from the Start menu connect to WAYLAND_DISPLAY="
                        + compositorSocketName
                        + (xwayland.displayName ? ", and X11 programs to DISPLAY=" + xwayland.displayName : "")
                        + ".", "info", ["OK"], desktop.focusActiveWindow)
                    onStartMenuClosed: {
                        if (!runDialog.visible && !shutDownDialog.visible && !shellMessage.visible)
                            desktop.focusActiveWindow();
                    }
                }

                // X11 menus and tooltips: above every window, and above the
                // taskbar too, like Win95 menus
                Item {
                    id: xPopupLayer
                    anchors.fill: parent
                }

                // Shell dialogs sit above windows and the taskbar.
                RunDialog {
                    id: runDialog
                    onFailed: (command, message) => shellMessage.show(command, message, "error", ["OK"],
                                                                      runDialog.refocus)
                    onClosed: desktop.focusActiveWindow()
                }
                ShutDownDialog {
                    id: shutDownDialog
                    onChosen: (option) => {
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
                                Qt.quit();
                        }
                    }
                    onClosed: desktop.focusActiveWindow()
                }
                Ui.MessageBox95 { id: shellMessage }
                // Alt+Tab
                TaskSwitcher {
                    id: taskSwitcher
                    z: 2500
                }

                // Ctrl+Shift+5: the screen capture tool
                ScreenCapture {
                    id: screenCapture
                    z: 9800
                    targetWindow: outputWindow
                    waylandCompositor: compositor
                    windows: windowLayer
                    onActiveChanged: if (!active) desktop.focusActiveWindow()
                }

                // NetworkManager needs a network's password
                NetworkPasswordDialog {
                    z: 2000
                    onClosed: desktop.focusActiveWindow()
                }
                // the password list (win95keyring) needs its password
                KeyringPasswordDialog {
                    z: 2000
                    onClosed: desktop.focusActiveWindow()
                }
                // a program needs a password (polkit): above windows and dialogs
                PasswordDialog {
                    z: 2000
                    onClosed: desktop.focusActiveWindow()
                }

                // notifications: bottom right, above the taskbar, newest
                // nearest it; at most four at once
                Column {
                    id: notificationStack
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.bottom: taskbar.top
                    anchors.bottomMargin: 8
                    spacing: 6
                    // above windows, below the taskbar's own pop-ups
                    z: 50
                    Repeater {
                        model: Notifications.popups.slice(-4)
                        NotificationPopup {
                            required property var modelData
                            notification: modelData
                        }
                    }
                }

                // brightness/volume level, when a laptop key changes it
                LevelOsd {
                    id: levelOsd
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: taskbar.top
                    anchors.bottomMargin: 80
                    z: 950
                }

                // a turned-off display (really off on the hardware; this
                // black cover is for when that isn't possible)
                Rectangle {
                    anchors.fill: parent
                    z: 10001
                    color: "black"
                    visible: screenSaver.displayOff
                    MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.BlankCursor }
                }

                // --- Accessibility Options: what they show --------------
                // SoundSentry: a flash where the sound came from
                Rectangle {
                    id: sentryFlash
                    z: 9000
                    visible: opacity > 0
                    opacity: 0
                    color: "#ffffff"
                    function flash(mode) {
                        var frame = WindowManager.activeFrame();
                        if (mode === "desktop" || !frame) {
                            x = 0; y = 0; width = desktop.width; height = desktop.height;
                        } else {
                            var p = frame.mapToItem(desktop, 0, 0);
                            x = p.x; y = p.y; width = frame.width;
                            height = mode === "caption" ? Theme.titleBarHeight + 4 : frame.height;
                        }
                        flashAnim.restart();
                    }
                    SequentialAnimation {
                        id: flashAnim
                        PropertyAction { target: sentryFlash; property: "opacity"; value: 0.85 }
                        PauseAnimation { duration: 120 }
                        PropertyAction { target: sentryFlash; property: "opacity"; value: 0 }
                        PauseAnimation { duration: 80 }
                        PropertyAction { target: sentryFlash; property: "opacity"; value: 0.85 }
                        PauseAnimation { duration: 120 }
                        PropertyAction { target: sentryFlash; property: "opacity"; value: 0 }
                    }
                }
                // ShowSounds: a caption naming the sound; StickyKeys: the
                // keys it's holding down
                Column {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: taskbar.top
                    anchors.bottomMargin: 40
                    spacing: 6
                    z: 960
                    Repeater {
                        model: [soundCaption.text, access.heldModifiers.length ? "StickyKeys: " + access.heldModifiers.join(" + ") : ""]
                            .filter(function (t) { return t; })
                        Rectangle {
                            required property string modelData
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: captionText.implicitWidth + 16
                            height: captionText.implicitHeight + 8
                            color: "#ffffe1"
                            border.color: "#000000"
                            Text {
                                id: captionText
                                anchors.centerIn: parent
                                text: parent.modelData
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize + 2
                            }
                        }
                    }
                    QtObject {
                        id: soundCaption
                        property string text: ""
                        property var labels: ({})
                        function show(event) {
                            if (Object.keys(labels).length === 0) {
                                var l = {};
                                Ui.Wayland95Settings.soundEvents().forEach(function (e) { l[e.id] = e.label; });
                                labels = l;
                            }
                            text = "\u266A " + (labels[event] || event);
                            captionTimer.restart();
                        }
                    }
                    Timer { id: captionTimer; interval: 2500; onTriggered: soundCaption.text = "" }
                }
                // MouseKeys' pointer, when the real one can't be moved
                // (nested in another desktop)
                Canvas {
                    z: 9500
                    visible: access.pointerMoved && !Ui.Power.primarySession
                    x: access.pointer.x
                    y: access.pointer.y
                    width: 12
                    height: 19
                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.fillStyle = "#ffffff";
                        ctx.strokeStyle = "#000000";
                        ctx.beginPath();
                        ctx.moveTo(0.5, 0.5); ctx.lineTo(0.5, 16); ctx.lineTo(4.5, 12); ctx.lineTo(7.5, 18.5);
                        ctx.lineTo(9.5, 17.5); ctx.lineTo(6.5, 11.5); ctx.lineTo(11.5, 11.5); ctx.closePath();
                        ctx.fill(); ctx.stroke();
                    }
                }

                // the password-protected screen saver's password box
                UnlockDialog {
                    z: 10002
                    controller: screenSaver
                }

                // the screen saver, over everything
                Ui.ScreenSaverView {
                    id: screenSaverView
                    anchors.fill: parent
                    z: 10000
                    visible: screenSaver.active
                    running: visible
                    saver: Ui.Wayland95Settings.screenSaver === "none" ? "blank" : Ui.Wayland95Settings.screenSaver
                    options: Ui.Wayland95Settings.screenSaverOptions
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.BlankCursor
                    }
                }

                // Ctrl+Shift+Esc opens the System Monitor
                Shortcut {
                    sequence: "Ctrl+Shift+Esc"
                    context: Qt.ApplicationShortcut
                    onActivated: Launcher.launch("win95sysmon")
                }

                // Print Screen saves a picture of the whole screen in
                // ~/Pictures (or the home folder), and says where
                Shortcut {
                    sequence: "Print"
                    context: Qt.ApplicationShortcut
                    onActivated: {
                        var dir = Ui.Win95Fs.homePath() + "/Pictures";
                        if (!Ui.Win95Fs.isDir(dir))
                            dir = Ui.Win95Fs.homePath();
                        var path = dir + "/Screenshot " + Qt.formatDateTime(new Date(), "yyyy-MM-dd hh-mm-ss") + ".png";
                        outputWindow.contentItem.grabToImage(function (result) {
                            var ok = result.saveToFile(path);
                            Ui.Wayland95Settings.playSound(ok ? "notification" : "critical");
                            Notifications.notify("Wayland 95", ok ? "Screenshot saved" : "The screenshot couldn't be saved",
                                                 ok ? path : dir, "", 1, 5000);
                        });
                    }
                }

                // Ctrl+Esc opens the Start menu, as in Win95 (it works even
                // while a client window has keyboard focus)
                Shortcut {
                    sequence: "Ctrl+Esc"
                    context: Qt.ApplicationShortcut
                    onActivated: taskbar.toggleStartMenu()
                }

                // give keyboard focus back to the active client window
                function focusActiveWindow() {
                    for (var i = 0; i < windowLayer.children.length; ++i) {
                        var w = windowLayer.children[i];
                        if (w.isActive && w.requestActivate && w.visible) {
                            w.requestActivate();
                            return;
                        }
                    }
                }
            }
        }
    }

    // the "Start Wayland 95" sound
    Component.onCompleted: {
        Ui.Wayland95Settings.playSound("startup");
        // as the desktop, handle the lid and power keys ourselves
        if (Ui.Power.primarySession)
            Ui.Power.takeOverPowerKeys();
    }

    // notifications: the sound, Do Not Disturb, and the desktop's own
    // notices (network, Bluetooth, battery, updates, problem reports)
    SystemNotices {}

    // ------------------------------------------------------------------ power
    // Control Panel > Power. Sleeping, turning the display off and handling
    // the lid happen only when Wayland 95 is the desktop; nested inside
    // another one, that desktop does it.
    QtObject {
        id: powerPolicy
        readonly property var settings: Ui.Wayland95Settings.power
        readonly property bool onBattery: Ui.Battery.present && !Ui.Battery.pluggedIn
        readonly property int displayOffMinutes: onBattery ? settings.displayOffBattery : settings.displayOffAC
        readonly property int sleepMinutes: onBattery ? settings.sleepBattery : settings.sleepAC

        // "nothing", "display", "sleep", "hibernate", "shutdown", "ask"
        function act(action) {
            switch (action) {
            case "display":
                screenSaver.displayOff = true;
                break;
            case "sleep":
                Ui.Power.suspend();
                break;
            case "hibernate":
                Ui.Power.canHibernate ? Ui.Power.hibernate() : Ui.Power.suspend();
                break;
            case "shutdown":
                Ui.Wayland95Settings.playSound("exit");
                Ui.Power.powerOff();
                break;
            case "ask":
                taskbar.closePopups();
                shutDownDialog.open();
                break;
            }
        }
    }

    // idle: turn the display off, then sleep (programs that keep the screen
    // saver away, like a playing video, keep these away too)
    Timer {
        interval: 10000
        repeat: true
        running: Ui.Power.primarySession
        onTriggered: {
            var idle = screenSaver.idleSeconds();
            if (screenSaver.inhibited)
                return;
            if (powerPolicy.displayOffMinutes > 0 && idle >= powerPolicy.displayOffMinutes * 60)
                screenSaver.displayOff = true;
            if (powerPolicy.sleepMinutes > 0 && idle >= powerPolicy.sleepMinutes * 60 && Ui.Power.canSuspend) {
                screenSaver.poke(); // don't go straight back to sleep after waking
                Ui.Power.suspend();
            }
        }
    }

    Connections {
        target: Ui.Power
        function onLidClosedChanged() {
            if (!Ui.Power.primarySession)
                return;
            if (Ui.Power.lidClosed)
                powerPolicy.act(powerPolicy.settings.lidAction);
            else
                screenSaver.displayOff = false;
        }
    }

    // laptop keys
    Connections {
        target: screenSaver
        function onHardwareKey(key) {
            switch (key) {
            case Qt.Key_MonBrightnessUp:
            case Qt.Key_MonBrightnessDown:
                levelOsd.changeBrightness(key === Qt.Key_MonBrightnessUp ? 5 : -5);
                break;
            case Qt.Key_VolumeUp:
            case Qt.Key_VolumeDown:
                levelOsd.changeVolume(key === Qt.Key_VolumeUp ? 0.05 : -0.05);
                break;
            case Qt.Key_VolumeMute:
                levelOsd.toggleMute();
                break;
            case Qt.Key_PowerOff:
            case Qt.Key_PowerDown:
                if (Ui.Power.primarySession)
                    powerPolicy.act(powerPolicy.settings.powerButton);
                break;
            case Qt.Key_Sleep:
            case Qt.Key_Suspend:
            case Qt.Key_Standby:
                if (Ui.Power.primarySession)
                    powerPolicy.act(powerPolicy.settings.sleepButton);
                break;
            case Qt.Key_Hibernate:
                if (Ui.Power.primarySession)
                    powerPolicy.act("hibernate");
                break;
            }
        }
    }

    // critically low battery: warn, give a minute to plug in, then act
    Connections {
        target: Ui.Battery
        function onChanged() {
            if (!Ui.Power.primarySession || !Ui.Battery.present)
                return;
            if (Ui.Battery.pluggedIn) {
                criticalTimer.stop();
                criticalTimer.handled = false;
            } else if (Ui.Battery.percent <= 5 && !criticalTimer.handled
                       && powerPolicy.settings.criticalBattery !== "nothing") {
                criticalTimer.handled = true;
                criticalTimer.start();
                var what = ({ sleep: "go to sleep", hibernate: Ui.Power.canHibernate ? "hibernate" : "go to sleep",
                              shutdown: "shut down" })[powerPolicy.settings.criticalBattery];
                Notifications.notify("Power", "Battery critically low",
                                     "The computer will " + what + " in one minute unless you plug it in.", "", 2);
            }
        }
    }
    Timer {
        id: criticalTimer
        property bool handled: false
        interval: 60000
        onTriggered: {
            if (Ui.Battery.present && !Ui.Battery.pluggedIn)
                powerPolicy.act(powerPolicy.settings.criticalBattery);
        }
    }

    // Control Panel > Display > Screen Saver
    // Control Panel > Accessibility Options
    AccessibilityController {
        id: access
        readonly property var a: Ui.Wayland95Settings.accessibility
        readonly property var names: ({ stickyKeys: "StickyKeys", filterKeys: "FilterKeys", toggleKeys: "ToggleKeys",
                                        mouseKeys: "MouseKeys", highContrast: "High Contrast" })
        window: outputWindow
        stickyKeys: a.stickyKeys
        stickyLock: a.stickyLock
        stickySound: a.stickySound
        filterKeys: a.filterKeys
        bounceMs: a.filterBounceMs
        slowMs: a.filterSlowMs
        toggleKeys: a.toggleKeys
        mouseKeys: a.mouseKeys
        mouseKeysSpeed: a.mouseKeysSpeed
        mouseKeysAcceleration: a.mouseKeysAcceleration
        mouseKeysNumLockOn: a.mouseKeysNumLockOn
        shortcuts: ["stickyKeys", "filterKeys", "toggleKeys", "mouseKeys", "highContrast"]
            .filter(function (f) { return access.a[f + "Shortcut"]; })

        function setFeature(feature, on) {
            if (feature === "highContrast") {
                Ui.Wayland95Settings.setHighContrast(on);
            } else {
                var copy = Object.assign({}, a);
                copy[feature] = on;
                Ui.Wayland95Settings.setAccessibility(copy);
            }
        }
        onShortcutPressed: (feature) => {
            var on = !a[feature];
            setFeature(feature, on);
            if (a.notifySound)
                playTone(on ? "on" : "off");
            if (a.notifyMessage)
                Notifications.notify("Accessibility Options", names[feature] + (on ? " is on" : " is off"),
                                     "Its shortcut key turned it " + (on ? "on" : "off")
                                     + ". Control Panel > Accessibility Options changes it and its shortcut.", "", 1, 5000);
        }
        onLockToggled: (on) => playTone(on ? "high" : "low")
        onToneReady: (path) => Ui.Wayland95Settings.playFile(path)
        onSoundEvent: (event) => {
            if (a.soundSentry)
                sentryFlash.flash(a.soundSentryFlash);
            if (a.showSounds)
                soundCaption.show(event);
        }
    }
    // Control Panel > Mouse > Touchpad (on the screen itself; inside
    // another desktop, that desktop handles the touchpad)
    TouchpadControl {
        settings: Ui.Wayland95Settings.touchpad
    }

    // Alt+Tab's keys (TaskSwitcher shows the box)
    TaskSwitchKeys {
        window: outputWindow
        onStarted: (backwards) => taskSwitcher.start(backwards)
        onStepped: (backwards) => taskSwitcher.step(backwards)
        onFinished: taskSwitcher.finish()
        onCancelled: taskSwitcher.cancel()
        onCaptureRequested: screenCapture.active ? screenCapture.close() : screenCapture.open()
    }

    // General > Automatic reset: off again after a while without use
    Timer {
        interval: 30000
        repeat: true
        running: access.a.autoReset
        onTriggered: {
            if (access.idleSeconds < access.a.autoResetMinutes * 60)
                return;
            var features = ["stickyKeys", "filterKeys", "toggleKeys", "mouseKeys", "highContrast", "soundSentry", "showSounds"];
            var on = features.filter(function (f) { return access.a[f]; });
            if (on.length === 0)
                return;
            if (access.a.highContrast)
                Ui.Wayland95Settings.setHighContrast(false);
            var copy = Object.assign({}, Ui.Wayland95Settings.accessibility);
            on.forEach(function (f) { copy[f] = false; });
            Ui.Wayland95Settings.setAccessibility(copy);
        }
    }

    ScreenSaverController {
        id: screenSaver
        compositor: compositor
        enabled: Ui.Wayland95Settings.screenSaver !== "none"
        waitMinutes: Ui.Wayland95Settings.screenSaverWait
        requirePassword: Ui.Wayland95Settings.screenSaverPassword
        onActiveChanged: {
            if (!active) {
                taskbar.closePopups();
                desktop.focusActiveWindow();
            }
        }
    }
    // lets video players keep the screen saver away (zwp_idle_inhibit_v1)
    IdleInhibitManagerV1 {}

    // keyboard layout and repeat from Control Panel > Keyboard
    SeatSettings {
        seat: compositor.defaultSeat
        layout: Ui.Wayland95Settings.keyboardLayout
        repeatDelay: Ui.Wayland95Settings.keyRepeatDelay
        repeatRate: Ui.Wayland95Settings.keyRepeatRate
    }

    // Tell clients that support it (Qt, foot, kitty, ...) to skip drawing
    // their own title bar - WindowFrame already provides the Win95 one.
    XdgDecorationManagerV1 {
        preferredMode: XdgToplevel.ServerSideDecoration
    }
    // a program activating one of its windows (Firefox and VS Code need
    // this to take the keyboard at all)
    XdgActivation {
        onActivateRequested: (surface) => {
            for (var i = 0; i < windowLayer.children.length; ++i) {
                var w = windowLayer.children[i];
                if (w.xdgSurface && w.xdgSurface.surface === surface) {
                    if (w.minimized)
                        WindowManager.setMinimized(w, false);
                    w.requestActivate();
                    return;
                }
            }
        }
    }

    // X11 programs: runs Xwayland and manages its windows
    XWayland {
        id: xwayland
        compositor: compositor
        enabled: xwaylandEnabled

        onWindowMapped: (xwindow) => {
            const file = xwindow.overrideRedirect ? "XPopup.qml" : "XWindowFrame.qml";
            const component = Qt.createComponent(file);
            if (component.status === Component.Error) {
                console.warn("Failed to load " + file + ":", component.errorString());
                return;
            }
            if (xwindow.overrideRedirect)
                component.createObject(xPopupLayer, { xwindow: xwindow });
            else
                component.createObject(windowLayer, { xwindow: xwindow, xwayland: xwayland });
        }
    }

    // where Qt and other programs' windows get out of step (inputfixes.h)
    InputFixes {
        id: inputFixes
        compositor: compositor
    }

    XdgShell {
        id: xdgShell

        onToplevelCreated: (toplevel, xdgSurface) => {
            const component = Qt.createComponent("WindowFrame.qml");
            if (component.status === Component.Error) {
                console.warn("Failed to load WindowFrame.qml:", component.errorString());
                return;
            }
            component.createObject(windowLayer, {
                toplevel: toplevel,
                xdgSurface: xdgSurface,
                inputFixes: inputFixes,
            });
        }
    }
}
