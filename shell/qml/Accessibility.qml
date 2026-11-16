import QtQuick
import QtQuick.Window
import Win95Ui as Ui
import Win95Shell

// Control Panel > Accessibility Options, the shell's part: the compositor
// does the keys (StickyKeys, FilterKeys, ToggleKeys, MouseKeys) and tells
// us (DesktopKeys) what to sound and which shortcut was used. Here: the
// tones, turning a feature on or off from its shortcut (and saying so),
// the keys StickyKeys holds and ShowSounds' captions, SoundSentry's flash,
// and the automatic reset.
QtObject {
    id: access

    required property DesktopKeys keys
    readonly property var a: Ui.Wayland95Settings.accessibility
    readonly property var names: ({ stickyKeys: "StickyKeys", filterKeys: "FilterKeys", toggleKeys: "ToggleKeys",
                                    mouseKeys: "MouseKeys", highContrast: "High Contrast" })
    readonly property var featureNames: [null, "stickyKeys", "filterKeys", "toggleKeys", "mouseKeys", "highContrast"]
    property string stickyHeld: ""
    property string soundCaption: ""

    function setFeature(feature, on) {
        if (feature === "highContrast") {
            Ui.Wayland95Settings.setHighContrast(on);
        } else {
            var copy = Object.assign({}, a);
            copy[feature] = on;
            Ui.Wayland95Settings.setAccessibility(copy);
        }
    }

    property Connections fromCompositor: Connections {
        target: access.keys
        function onTone(t) {
            Ui.Wayland95Settings.playTone(t === DesktopKeys.High ? "high" : t === DesktopKeys.Low ? "low" : "click");
        }
        function onShortcut(f) {
            var feature = access.featureNames[f];
            if (!feature)
                return;
            var on = !access.a[feature];
            console.info("win95shell: accessibility shortcut:", feature, on ? "on" : "off");
            access.setFeature(feature, on);
            if (access.a.notifySound)
                Ui.Wayland95Settings.playTone(on ? "on" : "off");
            if (access.a.notifyMessage)
                Notifications.notify("Accessibility Options", access.names[feature] + (on ? " is on" : " is off"),
                                     "Its shortcut key turned it " + (on ? "on" : "off")
                                     + ". Control Panel > Accessibility Options changes it and its shortcut.", "", 1, 5000);
        }
        function onStickyModifiers(held) { access.stickyHeld = held; }
    }

    // a program played a sound (Wayland95Settings::playSound tells us)
    property var labels: ({})
    property Connections sounds: Connections {
        target: AccessibilityService
        function onSoundPlayed(event) {
            if (access.a.soundSentry)
                access.flash(access.a.soundSentryFlash);
            if (access.a.showSounds) {
                if (Object.keys(access.labels).length === 0) {
                    var l = {};
                    Ui.Wayland95Settings.soundEvents().forEach(function (e) { l[e.id] = e.label; });
                    access.labels = l;
                }
                access.soundCaption = "♪ " + (access.labels[event] || event);
                captionTimer.restart();
            }
        }
    }
    property Timer captionTimer: Timer { interval: 2500; onTriggered: access.soundCaption = "" }

    // SoundSentry: a flash where the sound came from (the active window's
    // title bar, the window, or the whole desktop)
    property string flashMode: ""
    function flash(mode) {
        flashMode = mode;
        if (mode === "desktop")
            showFlash(0, 0, Screen.width, Screen.height);
        else
            ScreenCopy.listWindows("sentry");
    }
    function showFlash(x, y, w, h) {
        sentryFlash.x = x; sentryFlash.y = y; sentryFlash.width = w; sentryFlash.height = h;
        flashAnim.restart();
    }
    property Connections windowList: Connections {
        target: ScreenCopy
        function onWindowsListed(tag, windows) {
            if (tag !== "sentry")
                return;
            if (windows.length === 0) {
                access.showFlash(0, 0, Screen.width, Screen.height);
                return;
            }
            var w = windows[0]; // the top one: the active window
            access.showFlash(w.x, w.y, w.width, access.flashMode === "caption" ? Theme.titleBarHeight + 4 : w.height);
        }
    }

    // over everything, taking no input; shown only while there's something
    property Window layer: Window {
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1
        LayerShell.keyboard: LayerShell.KeyboardNone
        LayerShell.scope: "accessibility"
        title: "Accessibility"
        color: "transparent"
        flags: Qt.WindowTransparentForInput
        width: 640; height: 480
        visible: flashAnim.running || access.stickyHeld !== "" || access.soundCaption !== ""
        Item {
            width: Screen.width
            height: Screen.height
            Rectangle {
                id: sentryFlash
                color: "#ffffff"
                opacity: 0
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
            // ShowSounds' caption; the keys StickyKeys holds
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.taskbarHeight + 40
                spacing: 6
                Repeater {
                    model: [access.soundCaption, access.stickyHeld ? "StickyKeys: " + access.stickyHeld : ""]
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
            }
        }
    }

    // General > Automatic reset: off again after a while without use
    property IdleTimer resetIdle: IdleTimer {
        timeout: access.a.autoReset ? Math.max(1, access.a.autoResetMinutes) * ShellSession.minuteMs : 0
        onIdled: {
            var features = ["stickyKeys", "filterKeys", "toggleKeys", "mouseKeys", "highContrast", "soundSentry", "showSounds"];
            var on = features.filter(function (f) { return access.a[f]; });
            if (on.length === 0)
                return;
            console.info("win95shell: accessibility: automatic reset:", on.join(", "));
            if (access.a.highContrast)
                Ui.Wayland95Settings.setHighContrast(false);
            var copy = Object.assign({}, Ui.Wayland95Settings.accessibility);
            on.forEach(function (f) { copy[f] = false; });
            Ui.Wayland95Settings.setAccessibility(copy);
        }
    }
}
