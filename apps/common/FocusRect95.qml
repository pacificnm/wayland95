import QtQuick

// The Win95 dotted focus rectangle (alternating pixels).
Canvas {
    id: rect
    property color color: "#000000"
    onPaint: {
        var ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        ctx.fillStyle = rect.color;
        var w = Math.floor(width), h = Math.floor(height);
        for (var x = 0; x < w; x += 2) {
            ctx.fillRect(x, 0, 1, 1);
            ctx.fillRect(x, h - 1, 1, 1);
        }
        for (var y = 0; y < h; y += 2) {
            ctx.fillRect(0, y, 1, 1);
            ctx.fillRect(w - 1, y, 1, 1);
        }
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onColorChanged: requestPaint()
}
