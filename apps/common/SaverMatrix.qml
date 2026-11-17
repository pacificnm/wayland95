import QtQuick

// Digital rain: columns of glowing characters falling down a black screen,
// each with a bright head and a fading trail.
// Options: speed (1-10), density (1-10: how many columns rain at once),
// size (character height), glyphs ("code": mirrored letters, digits and
// symbols; "binary": 0 and 1), rain (the color).
//
// Nothing is cleared between frames: each frame a thin black wash dims
// what's there (the trails) and only the heads are drawn, two characters a
// column, so it costs the same at any screen size.
Canvas {
    id: canvas

    property var view
    readonly property real speed: view ? view.option("speed", 5) : 5
    readonly property real density: view ? view.option("density", 6) : 6
    readonly property string glyphs: view ? view.option("glyphs", "code") : "code"
    readonly property color rain: view ? view.option("rain", "#00ff41") : "#00ff41"
    // (scaled down in the Display applet's little monitor)
    readonly property int cell: Math.max(4, Math.round((view ? view.option("size", 16) : 16) * Math.min(1, Math.max(width, 1) / 640)))
    readonly property int cols: Math.ceil(width / cell)
    readonly property int rows: Math.ceil(height / cell)
    readonly property string alphabet: glyphs === "binary" ? "01"
        : "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789:.=*+-<>|#$%&"

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
        var mirrored = glyphs !== "binary";

        function put(c, r, color) {
            var x = c * cell, y = r * cell;
            ctx.fillStyle = "#000000";
            ctx.fillRect(x, y, cell, cell);
            ctx.fillStyle = color;
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
