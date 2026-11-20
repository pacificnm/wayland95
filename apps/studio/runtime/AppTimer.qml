import QtQuick
import Win95Ui

// Timer: runs its `timer` event every Interval milliseconds while it's
// Enabled. Like Visual Basic's, it's only seen in the Studio (a stopwatch
// on the form); the program doesn't show it.
//
//   timer1.interval = 500        timer1.enabled = false (stops it)
//   timer1.start()  stop()  restart() (from now)   timer1.running   timer1.count (times it's gone off)
AppElement {
    id: timer

    property int interval: 1000
    // how many times it has gone off since the form opened (or start())
    property int count: 0
    readonly property bool running: ticker.running
    signal timer()

    function start() { count = 0; enabled = true; ticker.restart(); }
    function stop() { enabled = false; }
    function restart() { enabled = true; ticker.restart(); }

    visible: designMode
    activeFocusOnTab: false

    Timer {
        id: ticker
        interval: Math.max(1, timer.interval)
        repeat: true
        running: timer.enabled && !timer.designMode && timer.interval > 0 && timer.form !== null
        onTriggered: {
            timer.count += 1;
            timer.timer();
        }
    }

    // (the Studio's: a raised button with a stopwatch)
    Bevel {
        anchors.fill: parent
        visible: timer.designMode
        style: "raised"
        face: Theme.face
    }
    Icon95 {
        anchors.centerIn: parent
        visible: timer.designMode
        name: "clock"
        enabled: timer.enabled
    }
}
