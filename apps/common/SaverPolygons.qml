import QtQuick

// Bouncing outlines that leave a trail and slowly change color.
// Options: count (polygons, 1-4), trail (copies drawn, 1-20), speed (1-10).
Canvas {
    id: canvas

    property var view
    readonly property int count: view ? Math.max(1, Math.min(4, view.option("count", 2))) : 2
    readonly property int trail: view ? Math.max(1, Math.min(20, view.option("trail", 8))) : 8
    readonly property real speed: view ? view.option("speed", 5) : 5
    property var shapes: []
    property real hue: Math.random()

    function reset() {
        var list = [];
        for (var p = 0; p < count; ++p) {
            var pts = [];
            for (var i = 0; i < 4; ++i)
                pts.push({ x: Math.random(), y: Math.random(),
                           dx: (Math.random() * 0.6 + 0.4) * (Math.random() < 0.5 ? -1 : 1),
                           dy: (Math.random() * 0.6 + 0.4) * (Math.random() < 0.5 ? -1 : 1) });
            list.push({ points: pts, history: [], hueOffset: p * 0.35 });
        }
        shapes = list;
    }
    onCountChanged: reset()
    Component.onCompleted: reset()

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
        var step = 0.0025 * speed;
        hue = (hue + 0.002) % 1;
        ctx.lineWidth = Math.max(1, width / 800);
        for (var p = 0; p < shapes.length; ++p) {
            var shape = shapes[p];
            for (var i = 0; i < shape.points.length; ++i) {
                var pt = shape.points[i];
                pt.x += pt.dx * step;
                pt.y += pt.dy * step;
                if (pt.x < 0 || pt.x > 1) { pt.dx = -pt.dx; pt.x = Math.max(0, Math.min(1, pt.x)); }
                if (pt.y < 0 || pt.y > 1) { pt.dy = -pt.dy; pt.y = Math.max(0, Math.min(1, pt.y)); }
            }
            shape.history.unshift(shape.points.map(function (q) { return { x: q.x, y: q.y }; }));
            // spread the trail out a little so the copies don't overlap
            if (shape.history.length > trail * 4)
                shape.history.length = trail * 4;
            ctx.strokeStyle = Qt.hsla((hue + shape.hueOffset) % 1, 1, 0.55, 1);
            for (var h = 0; h < shape.history.length; h += 4) {
                var poly = shape.history[h];
                ctx.beginPath();
                ctx.moveTo(poly[0].x * width, poly[0].y * height);
                for (var k = 1; k < poly.length; ++k)
                    ctx.lineTo(poly[k].x * width, poly[k].y * height);
                ctx.closePath();
                ctx.stroke();
            }
        }
    }
}
