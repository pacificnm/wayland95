import QtQuick
import Win95Ui as Ui
import Win95Shell

// Control Panel > Power, for win95shell: turning the display off and
// sleeping when idle, the lid, the power and sleep buttons, and a
// critically low battery. Only as the session's desktop (--session; the
// power manager's primarySession): a shell started by hand or nested in
// another desktop leaves all this to that desktop, or to logind.
QtObject {
    id: policy

    readonly property bool primary: Ui.Power.primarySession
    readonly property var settings: Ui.Wayland95Settings.power
    readonly property bool onBattery: Ui.Battery.present && !Ui.Battery.pluggedIn
    readonly property int displayOffMinutes: onBattery ? settings.displayOffBattery : settings.displayOffAC
    readonly property int sleepMinutes: onBattery ? settings.sleepBattery : settings.sleepAC
    readonly property int minute: ShellSession.minuteMs
    // "ask": the Shut Down dialog
    signal askRequested()

    // "nothing", "display", "sleep", "hibernate", "shutdown", "ask"
    function act(action) {
        console.info("win95shell: power:", action);
        switch (action) {
        case "display":
            DisplayPower.setOn(false);
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
            askRequested();
            break;
        }
    }
    // the power and sleep buttons (the compositor sends them: DesktopKeys)
    function key(k) {
        if (!primary)
            return;
        if (k === DesktopKeys.Power)
            act(settings.powerButton);
        else if (k === DesktopKeys.Sleep)
            act(settings.sleepButton);
        else if (k === DesktopKeys.Hibernate)
            act("hibernate");
    }

    // idle: the display off, then sleep (what holds the screen saver off,
    // a playing video, holds these off too)
    property IdleTimer displayIdle: IdleTimer {
        timeout: policy.primary && policy.displayOffMinutes > 0 ? policy.displayOffMinutes * policy.minute : 0
        onIdled: { if (!ScreenSaverService.inhibited) DisplayPower.setOn(false); }
    }
    property IdleTimer sleepIdle: IdleTimer {
        timeout: policy.primary && policy.sleepMinutes > 0 && Ui.Power.canSuspend ? policy.sleepMinutes * policy.minute : 0
        onIdled: { if (!ScreenSaverService.inhibited) Ui.Power.suspend(); }
    }
    // the display is off (idle, the lid, a button): any input turns it on
    property IdleTimer wake: IdleTimer {
        timeout: DisplayPower.on ? 0 : 500
        onResumed: { if (!DisplayPower.on) DisplayPower.setOn(true); }
    }

    property Connections lid: Connections {
        target: Ui.Power
        function onLidClosedChanged() {
            if (!policy.primary)
                return;
            if (Ui.Power.lidClosed)
                policy.act(policy.settings.lidAction);
            else if (!DisplayPower.on)
                DisplayPower.setOn(true);
        }
    }

    // critically low battery: warn, give a minute to plug in, then act
    property Connections battery: Connections {
        target: Ui.Battery
        function onChanged() {
            if (!policy.primary || !Ui.Battery.present)
                return;
            if (Ui.Battery.pluggedIn) {
                criticalTimer.stop();
                criticalTimer.handled = false;
            } else if (Ui.Battery.percent <= 5 && !criticalTimer.handled
                       && policy.settings.criticalBattery !== "nothing") {
                criticalTimer.handled = true;
                criticalTimer.start();
                var what = ({ sleep: "go to sleep", hibernate: Ui.Power.canHibernate ? "hibernate" : "go to sleep",
                              shutdown: "shut down" })[policy.settings.criticalBattery];
                Notifications.notify("Power", "Battery critically low",
                                     "The computer will " + what + " in one minute unless you plug it in.", "", 2);
            }
        }
    }
    property Timer criticalTimer: Timer {
        id: criticalTimer
        property bool handled: false
        interval: policy.minute
        onTriggered: {
            if (Ui.Battery.present && !Ui.Battery.pluggedIn)
                policy.act(policy.settings.criticalBattery);
        }
    }

    Component.onCompleted: {
        // as the desktop, the lid and power keys are ours, not logind's
        if (primary)
            Ui.Power.takeOverPowerKeys();
    }
}
