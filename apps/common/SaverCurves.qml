import QtQuick

// A ribbon of curves sweeping across the screen in changing colors.
// Options: speed (1-10), density (curves in the ribbon, 5-40).
Canvas {
    id: canvas

    property var view
    readonly property real speed: view ? view.option("speed", 5) : 5
    readonly property int density: view ? Math.max(5, Math.min(40, view.option("density", 20))) : 20
    property real t: Math.random() * 100

    Timer {
        interval: 33
        repeat: true
        running: canvas.view ? canvas.view.running : false
        onTriggered: canvas.requestPaint()
    }

    // smooth wandering in 0..1, a sum of slow sine waves
    function wander(seed, time) {
        return 0.5 + 0.25 * Math.sin(time * 0.7 + seed) + 0.2 * Math.sin(time * 1.3 + seed * 2.1);
    }

    onPaint: {
        var ctx = getContext("2d");
        ctx.fillStyle = "black";
        ctx.fillRect(0, 0, width, height);
        t += 0.004 * speed;
        ctx.lineWidth = Math.max(1, width / 900);
        for (var i = 0; i < density; ++i) {
            var tt = t - i * 0.03;
            ctx.strokeStyle = Qt.hsla((t * 0.05 + i / density * 0.3) % 1, 1, 0.5, 1 - i / density * 0.7);
            ctx.beginPath();
            ctx.moveTo(wander(1, tt) * width, wander(2, tt) * height);
            ctx.bezierCurveTo(wander(3, tt) * width, wander(4, tt) * height,
                              wander(5, tt) * width, wander(6, tt) * height,
                              wander(7, tt) * width, wander(8, tt) * height);
            ctx.stroke();
        }
    }
}
