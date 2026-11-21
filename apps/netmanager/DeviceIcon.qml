import QtQuick

// Network Manager's icons, drawn on a 32x32 grid and scaled to `size`:
// the kinds of device (router, switch, accesspoint, printer, storage,
// camera, server, other) and the program's own ("netmanager": a router
// with a web page over it). Generic drawings.
Canvas {
    id: icon

    property string kind: "other"
    property int size: 32
    width: size
    height: size

    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()
    Component.onCompleted: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function box(x, y, w, h) { // a grey case, lit from the top left
            frame(x, y, w, h, "#c0c0c0");
            rect(x + 1, y + 1, w - 2, 1, "#ffffff");
            rect(x + 1, y + 1, 1, h - 2, "#ffffff");
            rect(x + 1, y + h - 2, w - 2, 1, "#808080");
        }
        function lights(x, y, n, gap) {
            for (var i = 0; i < n; ++i)
                rect(x + i * gap, y, 2, 2, i % 3 === 2 ? "#ffff00" : "#00e000");
        }
        function antenna(x, y, h) { rect(x, y, 2, h, "#000000"); rect(x - 1, y - 1, 4, 2, "#000000"); }
        function waves(x, y) {
            ctx.strokeStyle = "#000080";
            ctx.lineWidth = 1.5;
            for (var r = 3; r <= 9; r += 3) {
                ctx.beginPath();
                ctx.arc(x, y, r, -Math.PI * 0.8, -Math.PI * 0.2);
                ctx.stroke();
            }
        }

        switch (kind) {
        case "router":
            antenna(6, 8, 10);
            antenna(24, 8, 10);
            box(1, 17, 30, 10);
            lights(5, 21, 6, 4);
            rect(4, 27, 4, 2, "#000000"); rect(24, 27, 4, 2, "#000000");
            break;
        case "switch":
            box(0, 11, 32, 12);
            for (var p = 0; p < 6; ++p) {
                frame(3 + p * 4.5, 16, 4, 4, "#404040");
                rect(3 + p * 4.5, 14, 2, 1, p % 2 ? "#00e000" : "#ffff00");
            }
            break;
        case "accesspoint":
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.ellipse(3, 18, 26, 10); ctx.fill();
            ctx.fillStyle = "#e8e8e8";
            ctx.beginPath(); ctx.ellipse(4, 19, 24, 8); ctx.fill();
            rect(15, 22, 2, 2, "#00a0ff");
            waves(16, 16);
            break;
        case "printer":
            frame(8, 2, 16, 12, "#ffffff");
            rect(11, 6, 10, 1, "#808080"); rect(11, 9, 8, 1, "#808080");
            box(2, 12, 28, 12);
            frame(6, 22, 20, 6, "#ffffff");
            lights(24, 15, 1, 0);
            break;
        case "storage":
            for (var d = 0; d < 3; ++d) {
                box(4, 3 + d * 9, 24, 9);
                rect(7, 7 + d * 9, 12, 1, "#808080");
                rect(23, 6 + d * 9, 2, 2, d === 1 ? "#ffff00" : "#00e000");
            }
            break;
        case "camera":
            box(3, 8, 22, 12);
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(14, 14, 5, 0, Math.PI * 2); ctx.fill();
            ctx.fillStyle = "#4060c0";
            ctx.beginPath(); ctx.arc(14, 14, 3.5, 0, Math.PI * 2); ctx.fill();
            rect(13, 12, 2, 2, "#ffffff");
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(25, 11); ctx.lineTo(31, 8); ctx.lineTo(31, 20); ctx.lineTo(25, 17); ctx.closePath(); ctx.fill();
            rect(12, 20, 4, 6, "#000000");
            rect(6, 26, 16, 3, "#000000");
            break;
        case "server":
            frame(7, 1, 18, 30, "#c0c0c0");
            for (var s = 0; s < 4; ++s) {
                rect(9, 4 + s * 6, 14, 4, "#808080");
                rect(10, 5 + s * 6, 8, 1, "#000000");
                rect(20, 5 + s * 6, 2, 2, "#00e000");
            }
            break;
        case "netmanager":
            // a web page with a router in front of it
            frame(1, 1, 24, 20, "#ffffff");
            rect(2, 2, 22, 3, "#000080");
            rect(4, 8, 14, 1, "#0000c0"); rect(4, 11, 10, 1, "#808080"); rect(4, 14, 12, 1, "#808080");
            antenna(12, 13, 9);
            antenna(26, 13, 9);
            box(8, 21, 23, 9);
            lights(11, 25, 5, 4);
            break;
        default: // a box on the network, with a cable
            box(5, 6, 22, 16);
            rect(8, 9, 16, 10, "#008080");
            lights(9, 24, 0, 0);
            rect(15, 22, 2, 6, "#000000");
            rect(6, 28, 20, 2, "#000080");
            break;
        }
    }
}
