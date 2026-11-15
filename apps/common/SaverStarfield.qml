import QtQuick

// Flying through a field of stars. Options: speed (1-10), stars (count).
Canvas {
    id: canvas

    property var view
    readonly property real speed: view ? view.option("speed", 5) : 5
    readonly property int count: view ? Math.max(10, view.option("stars", 200)) : 200
    property var stars: []

    function spawn(star, far) {
        star.x = (Math.random() - 0.5) * 2;
        star.y = (Math.random() - 0.5) * 2;
        star.z = far ? 1 : 0.1 + Math.random() * 0.9;
    }

    onCountChanged: {
        var list = [];
        for (var i = 0; i < count; ++i) {
            var s = {};
            spawn(s, false);
            list.push(s);
        }
        stars = list;
    }
    Component.onCompleted: countChanged()

    Timer {
        interval: 33
        repeat: true
        running: canvas.view ? canvas.view.running : false
        onTriggered: canvas.requestPaint()
    }

    onPaint: {
        var ctx = getContext("2d");
        ctx.fillStyle = "black";
        ctx.fillRect(0, 0, width, height);
        var cx = width / 2, cy = height / 2, scale = Math.max(width, height) / 2;
        var step = 0.002 * speed;
        for (var i = 0; i < stars.length; ++i) {
            var s = stars[i];
            s.z -= step;
            var px = cx + s.x / s.z * scale, py = cy + s.y / s.z * scale;
            if (s.z <= 0.02 || px < 0 || px >= width || py < 0 || py >= height) {
                spawn(s, true);
                continue;
            }
            // nearer stars are bigger and brighter
            var size = Math.max(1, (1 - s.z) * 3 * Math.max(1, width / 640));
            var b = Math.round(255 * Math.min(1, 1.2 - s.z));
            ctx.fillStyle = "rgb(" + b + "," + b + "," + b + ")";
            ctx.fillRect(px, py, size, size);
        }
    }
}
