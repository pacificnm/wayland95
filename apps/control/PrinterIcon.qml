import QtQuick

// A printer, drawn on a 32x32 grid and scaled to `size`: paper in the
// top, a gray body with a slot. Badges: a check mark for the default
// printer, a red stop for one with a problem. "add" draws Win95's Add
// Printer: the printer with a magic sparkle.
Canvas {
    id: icon

    property string kind: "printer"   // or "add"
    property int size: 32
    property bool isDefault: false
    property bool paused: false
    property bool problem: false

    width: size
    height: size
    opacity: paused ? 0.55 : 1
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()
    onIsDefaultChanged: requestPaint()
    onProblemChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }

        // paper sticking out of the top, with lines of text
        frame(9, 3, 15, 12, "#ffffff");
        rect(11, 6, 10, 1, "#808080");
        rect(11, 8, 8, 1, "#808080");
        rect(11, 10, 10, 1, "#808080");
        // the body
        frame(3, 12, 27, 13, "#c0c0c0");
        rect(4, 13, 25, 1, "#ffffff");
        rect(4, 23, 25, 1, "#808080");
        rect(6, 15, 21, 2, "#000000");        // the paper slot
        rect(24, 19, 3, 2, "#00c000");        // power light
        frame(5, 25, 23, 4, "#a0a0a0");       // the paper tray

        if (kind === "add") {
            // a sparkle: Win95's "wizard" mark
            ctx.fillStyle = "#ffff00";
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(26, 0); ctx.lineTo(28, 5); ctx.lineTo(32, 6); ctx.lineTo(28, 8);
            ctx.lineTo(26, 13); ctx.lineTo(24, 8); ctx.lineTo(20, 6); ctx.lineTo(24, 5);
            ctx.closePath(); ctx.fill(); ctx.stroke();
        }
        if (isDefault) {
            // black disc with a white check, bottom left
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(6, 25, 6, 0, 2 * Math.PI); ctx.fill();
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(3, 25); ctx.lineTo(5.5, 27.5); ctx.lineTo(9.5, 22); ctx.stroke();
        }
        if (problem) {
            ctx.fillStyle = "#e00000";
            ctx.beginPath(); ctx.arc(26, 25, 5.5, 0, 2 * Math.PI); ctx.fill();
            ctx.strokeStyle = "#ffffff";
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(23.5, 22.5); ctx.lineTo(28.5, 27.5); ctx.moveTo(28.5, 22.5); ctx.lineTo(23.5, 27.5); ctx.stroke();
        }
    }
}
