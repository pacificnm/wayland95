import QtQuick
import QtQml
import QtQuick.Window
import Win95Ui as Ui
import Win95Shell

// Control Panel > Display > Screen Saver, for win95shell. After its wait
// without input (the compositor counts it: ext-idle-notify; programs that
// keep the screen awake hold it off, and so do programs asking over D-Bus,
// ScreenSaverService) it covers the screen until input wakes it.
// Password protected (and started by itself, not Preview), it locks the
// session (ext-session-lock): nothing else shows or gets input, and waking
// it asks for the password. With no input for 30 seconds the password box
// goes and the screen saver carries on.
QtObject {
    id: saver

    property bool active: false
    property bool protectedRun: false   // locked: only the password ends it
    property bool unlocking: false      // the password box is up
    property bool grace: false
    signal ended()

    readonly property string kind: Ui.Wayland95Settings.screenSaver === "none" ? "blank" : Ui.Wayland95Settings.screenSaver

    function start(preview) {
        if (active)
            return;
        protectedRun = !preview && Ui.Wayland95Settings.screenSaverPassword && SessionLock.supported;
        if (protectedRun && !SessionLock.lock())
            protectedRun = false;
        unlocking = false;
        grace = preview;
        if (preview)
            graceTimer.restart();
        active = true;
        console.info("win95shell: screen saver started" + (preview ? " (preview)" : protectedRun ? " (locked)" : ""));
    }
    function stop() {
        if (!active)
            return;
        if (protectedRun)
            SessionLock.unlock();
        protectedRun = false;
        unlocking = false;
        active = false;
        console.info("win95shell: screen saver ended");
        ended();
    }
    function wake() {
        if (!protectedRun) {
            stop();
            return;
        }
        unlocking = true;
        unlockTimeout.restart();
    }
    // for UnlockDialog (it calls these the controller's)
    function unlock() { stop(); }
    function cancelUnlock() {
        unlockTimeout.stop();
        if (!unlocking)
            return;
        unlocking = false;
        lockInput.reset();
        lockInput.takeKeys();
        grace = true; // the click on Cancel mustn't wake it again at once
        graceTimer.restart();
    }

    property Timer graceTimer: Timer { interval: 1500; onTriggered: saver.grace = false }
    property Timer unlockTimeout: Timer { interval: 30000; onTriggered: saver.cancelUnlock() }

    property IdleTimer idle: IdleTimer {
        timeout: Ui.Wayland95Settings.screenSaver !== "none"
                 ? Math.max(1, Ui.Wayland95Settings.screenSaverWait) * ShellSession.minuteMs : 0
        onIdled: { if (!ScreenSaverService.inhibited) saver.start(false); }
    }
    property Connections service: Connections {
        target: ScreenSaverService
        function onPreviewRequested() { saver.start(true); }
        function onActivateRequested(on) { if (on) saver.start(false); else if (!saver.protectedRun) saver.stop(); }
        function onActivityReported() { if (!saver.protectedRun) saver.stop(); }
        function onInhibitedChanged() {
            // held off until now, and nobody has touched anything since
            if (!ScreenSaverService.inhibited && saver.idle.idle)
                saver.start(false);
        }
    }
    property Binding serviceActive: Binding { target: ScreenSaverService; property: "active"; value: saver.active }
    property Connections lockState: Connections {
        target: SessionLock
        function onRefused() { saver.protectedRun = false; saver.stop(); }
    }

    // not protected: an overlay panel with the keyboard, over everything
    property Window plainWindow: Window {
        screen: ShellSession.mainScreen(Qt.application.screens, ShellSession.primaryOutput)
        LayerShell.layer: LayerShell.Overlay
        LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
        LayerShell.exclusiveZone: -1
        LayerShell.keyboard: LayerShell.KeyboardExclusive
        LayerShell.scope: "screensaver"
        title: "Screen Saver"
        color: "black"
        width: 640; height: 480
        visible: saver.active && !saver.protectedRun
        onVisibleChanged: if (visible) { plainInput.reset(); plainInput.takeKeys(); }
        Ui.ScreenSaverView {
            anchors.fill: parent
            running: parent.visible
            saver: saver.kind
            options: Ui.Wayland95Settings.screenSaverOptions
        }
        SaverInput {
            id: plainInput
            grace: saver.grace
            onWoken: saver.wake()
        }
    }

    // the other displays, not protected: covered too, and input on them
    // ends it as well (locked, the compositor covers them in black)
    property Instantiator otherCovers: Instantiator {
        model: Qt.application.screens
        delegate: Window {
            id: cover
            required property var modelData
            readonly property var owner: saver // (the view below has a "saver" of its own)
            screen: modelData
            LayerShell.layer: LayerShell.Overlay
            LayerShell.anchors: LayerShell.AnchorTop | LayerShell.AnchorBottom | LayerShell.AnchorLeft | LayerShell.AnchorRight
            LayerShell.exclusiveZone: -1
            LayerShell.keyboard: LayerShell.KeyboardNone
            LayerShell.scope: "screensaver-other"
            title: "Screen Saver (" + modelData.name + ")"
            color: "black"
            width: 640; height: 480
            visible: saver.active && !saver.protectedRun
                     && !ShellSession.isMainScreen(modelData, Qt.application.screens, ShellSession.primaryOutput)
            onVisibleChanged: if (visible) coverInput.reset()
            Ui.ScreenSaverView {
                anchors.fill: parent
                running: cover.visible
                saver: cover.owner.kind
                options: Ui.Wayland95Settings.screenSaverOptions
            }
            SaverInput {
                id: coverInput
                grace: saver.grace
                onWoken: saver.wake()
            }
        }
    }

    // protected: the session lock's window (the compositor gives it the
    // whole screen and all input until it unlocks)
    property Window lockWindow: Window {
        screen: ShellSession.mainScreen(Qt.application.screens, ShellSession.primaryOutput)
        title: "Screen Saver (locked)"
        color: "black"
        width: 640; height: 480
        visible: SessionLock.active
        Component.onCompleted: SessionLock.attach(this)
        onVisibleChanged: if (visible) { lockInput.reset(); lockInput.takeKeys(); }
        Ui.ScreenSaverView {
            anchors.fill: parent
            running: parent.visible && !saver.unlocking
            saver: saver.kind
            options: Ui.Wayland95Settings.screenSaverOptions
        }
        SaverInput {
            id: lockInput
            grace: saver.grace
            enabled: !saver.unlocking
            onWoken: saver.wake()
        }
        Item {
            id: lockArea
            width: Screen.width
            height: Screen.height
            UnlockDialog {
                controller: saver
                onTyping: saver.unlockTimeout.restart()
            }
            // typing or moving the mouse keeps the password box up
            MouseArea {
                anchors.fill: parent
                z: -1
                enabled: saver.unlocking
                hoverEnabled: true
                onPositionChanged: saver.unlockTimeout.restart()
                onPressed: (mouse) => { mouse.accepted = false; saver.unlockTimeout.restart(); }
            }
        }
    }
}
