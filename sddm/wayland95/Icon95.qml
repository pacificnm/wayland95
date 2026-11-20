import QtQuick

// The desktop's 32x32 drawings (the Start menu's key and computer), drawn
// on a pixel grid. Generic drawings, not Microsoft artwork.
Canvas {
    id: icon

    property string kind: "key"

    width: 32
    height: 32
    // stays sharp when the whole screen is scaled up
    smooth: false

    onKindChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }

        switch (kind) {
        case "shutdown":
            frame(4, 2, 24, 19, "#c0c0c0");    // monitor
            rect(5, 3, 22, 1, "#ffffff");
            frame(7, 5, 18, 13, "#000040");    // dark screen
            rect(13, 21, 6, 2, "#808080");
            frame(3, 23, 26, 7, "#c0c0c0");    // case
            rect(23, 26, 3, 2, "#00c000");
            break;
        default: // key: round bow with a hole, and a toothed blade
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(10, 12, 8, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#e0b000";
            ctx.beginPath(); ctx.arc(10, 12, 7, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#fff080";
            ctx.beginPath(); ctx.arc(8, 10, 2.5, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(10, 12, 2.5, 0, 2 * Math.PI); ctx.fill();
            frame(16, 10, 15, 5, "#e0b000");
            frame(24, 14, 3, 5, "#e0b000");
            frame(28, 14, 3, 4, "#e0b000");
            break;
        }
    }
}
