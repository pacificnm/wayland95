import QtQuick

// Digital rain: columns of glowing characters falling down a black screen,
// each with a bright head and a fading trail.
// Options: speed (1-10), density (1-10: how many columns rain at once),
// size (character height), glyphs ("runes": a script of our own; "code":
// mirrored letters, digits and symbols; "binary": 0 and 1), rain (the color).
//
// The runes are made up here, not taken from anywhere: each is a few pen
// strokes along a 3 by 4 grid of points (sometimes with a dot), written by
// a random walk from a fixed seed, so it's the same alphabet every time.
//
// Nothing is cleared between frames: each frame a thin black wash dims
// what's there (the trails) and only the heads are drawn, two characters a
// column, so it costs the same at any screen size.
Canvas {
    id: canvas

    property var view
    readonly property real speed: view ? view.option("speed", 5) : 5
    readonly property real density: view ? view.option("density", 6) : 6
    readonly property string glyphs: view ? view.option("glyphs", "runes") : "runes"
    readonly property color rain: view ? view.option("rain", "#00ff41") : "#00ff41"
    // (scaled down in the Display applet's little monitor)
    readonly property int cell: Math.max(4, Math.round((view ? view.option("size", 16) : 16) * Math.min(1, Math.max(width, 1) / 640)))
    readonly property int cols: Math.ceil(width / cell)
    readonly property int rows: Math.ceil(height / cell)
    readonly property string alphabet: glyphs === "binary" ? "01"
        : "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789:.=*+-<>|#$%&"

    // runes[i]: { strokes: [[x0, y0, x1, y1, ...], ...], dot: [x, y] or null },
    // in grid points (x 0-2, y 0-3)
    readonly property var runes: {
        var state = 95;
        function rnd() { // mulberry32
            var t = (state += 0x6D2B79F5) >>> 0;
            state = t;
            t = Math.imul(t ^ (t >>> 15), t | 1);
            t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
            return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
        }
        function walk(steps, used) {
            var x = Math.floor(rnd() * 3), y = Math.floor(rnd() * 4), line = [x, y];
            for (var n = 0; n < steps; ++n) {
                // a step to a neighbouring point: mostly straight, sometimes
                // diagonal, never back along a stroke already made
                for (var tries = 0; tries < 12; ++tries) {
                    var dx = Math.floor(rnd() * 3) - 1, dy = Math.floor(rnd() * 3) - 1;
                    if ((!dx && !dy) || (dx && dy && rnd() < 0.55))
                        continue;
                    var nx = x + dx, ny = y + dy;
                    if (nx < 0 || nx > 2 || ny < 0 || ny > 3)
                        continue;
                    var key = Math.min(x + y * 3, nx + ny * 3) + "-" + Math.max(x + y * 3, nx + ny * 3);
                    if (used[key])
                        continue;
                    used[key] = true;
                    x = nx; y = ny;
                    line.push(x, y);
                    break;
                }
            }
            return line;
        }
        var list = [], seen = {};
        while (list.length < 56) {
            var used = {}, strokes = [walk(3 + Math.floor(rnd() * 3), used)];
            if (rnd() < 0.6)
                strokes.push(walk(1 + Math.floor(rnd() * 2), used));
            strokes = strokes.filter(function (l) { return l.length >= 4; });
            var dot = rnd() < 0.3 ? [Math.floor(rnd() * 3), Math.floor(rnd() * 4)] : null;
            var name = Object.keys(used).sort().join(",") + (dot ? "." + dot : "");
            // tall enough to look like a letter, and not one we have
            var ys = [];
            strokes.forEach(function (l) { for (var i = 1; i < l.length; i += 2) ys.push(l[i]); });
            if (!strokes.length || seen[name] || Math.max.apply(null, ys) - Math.min.apply(null, ys) < 2)
                continue;
            seen[name] = true;
            list.push({ strokes: strokes, dot: dot });
        }
        return list;
    }

    // drops[c]: { y: the head's row (fractional), v: rows a frame, last: the row drawn }
    // or null while the column waits its turn
    property var drops: []
    property bool wipe: true

    function restart() {
        var list = [];
        for (var c = 0; c < cols; ++c)
            list.push(null);
        drops = list;
        wipe = true;
        requestPaint();
    }
    onColsChanged: restart()
    onRowsChanged: restart()
    onGlyphsChanged: restart()
    onRainChanged: restart()
    Component.onCompleted: restart()

    Timer {
        interval: 50
        repeat: true
        running: canvas.view ? canvas.view.running : false
        onTriggered: canvas.requestPaint()
    }

    onPaint: {
        var ctx = getContext("2d");
        if (wipe) {
            ctx.fillStyle = "#000000";
            ctx.fillRect(0, 0, width, height);
            wipe = false;
        }
        // dim everything a little: the trails
        ctx.fillStyle = "rgba(0, 0, 0, 0.07)";
        ctx.fillRect(0, 0, width, height);

        ctx.font = "bold " + cell + "px 'Liberation Mono'";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        var body = Qt.rgba(rain.r, rain.g, rain.b, 1);
        var head = Qt.rgba(0.7 + rain.r * 0.3, 0.7 + rain.g * 0.3, 0.7 + rain.b * 0.3, 1);
        var mirrored = glyphs === "code";
        var own = glyphs !== "code" && glyphs !== "binary";
        ctx.lineWidth = Math.max(1, cell / 9);
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        function put(c, r, color) {
            var x = c * cell, y = r * cell;
            ctx.fillStyle = "#000000";
            ctx.fillRect(x, y, cell, cell);
            ctx.fillStyle = color;
            if (own) {
                var rune = runes[Math.floor(Math.random() * runes.length)];
                var gx = cell * 0.27, gy = cell * 0.235, ox = x + cell * 0.23, oy = y + cell * 0.15;
                ctx.strokeStyle = color;
                ctx.beginPath();
                for (var k = 0; k < rune.strokes.length; ++k) {
                    var l = rune.strokes[k];
                    ctx.moveTo(ox + l[0] * gx, oy + l[1] * gy);
                    for (var i = 2; i < l.length; i += 2)
                        ctx.lineTo(ox + l[i] * gx, oy + l[i + 1] * gy);
                }
                ctx.stroke();
                if (rune.dot) {
                    ctx.beginPath();
                    ctx.arc(ox + rune.dot[0] * gx, oy + rune.dot[1] * gy, ctx.lineWidth * 0.9, 0, 2 * Math.PI);
                    ctx.fill();
                }
                return;
            }
            var ch = alphabet.charAt(Math.floor(Math.random() * alphabet.length));
            if (mirrored) {
                ctx.save();
                ctx.translate(x + cell / 2, y + cell / 2);
                ctx.scale(-1, 1);
                ctx.fillText(ch, 0, 0);
                ctx.restore();
            } else {
                ctx.fillText(ch, x + cell / 2, y + cell / 2);
            }
        }

        // a waiting column starts with this chance a frame: at density 10
        // nearly all rain at once, at 1 a few
        var start = 0.002 + density * density * 0.0008;
        for (var c = 0; c < drops.length; ++c) {
            var d = drops[c];
            if (!d) {
                if (Math.random() < start)
                    drops[c] = { y: -Math.random() * 6, v: (0.25 + Math.random() * 0.5) * (0.4 + speed * 0.16), last: -1 };
                continue;
            }
            d.y += d.v;
            var row = Math.floor(d.y);
            // every row it passed: the old head turns the rain's color, the new one is bright
            while (d.last < row) {
                if (d.last >= 0 && d.last < rows)
                    put(c, d.last, body);
                ++d.last;
            }
            if (row >= 0 && row < rows)
                put(c, row, head);
            // now and then a character in the trail changes
            if (row > 2 && Math.random() < 0.15) {
                var back = row - 1 - Math.floor(Math.random() * Math.min(row - 1, 12));
                if (back >= 0 && back < rows)
                    put(c, back, body);
            }
            if (row > rows + 2)
                drops[c] = null;
        }
    }
}
