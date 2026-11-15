import QtQuick

// Device Manager icons, drawn on a 16x16 grid and scaled to `size`.
// `problem` adds the yellow "!" Win95 used for devices that aren't working.
Canvas {
    id: icon

    property string kind: "system"
    property int size: 16
    property bool problem: false

    width: size
    height: size
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()
    onProblemChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 16, size / 16);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function line(x1, y1, x2, y2, c, w) {
            ctx.strokeStyle = c; ctx.lineWidth = w || 1;
            ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke();
        }

        switch (kind) {
        case "computer":
            frame(1, 1, 14, 10, "#c0c0c0"); rect(3, 3, 10, 6, "#008080");
            rect(6, 11, 4, 2, "#808080"); frame(2, 13, 12, 3, "#c0c0c0");
            break;
        case "cpu":
            frame(3, 3, 10, 10, "#404040"); rect(5, 5, 6, 6, "#808080");
            for (var p = 0; p < 4; ++p) {
                rect(4 + p * 2.5, 0, 1, 3, "#c0a000"); rect(4 + p * 2.5, 13, 1, 3, "#c0a000");
                rect(0, 4 + p * 2.5, 3, 1, "#c0a000"); rect(13, 4 + p * 2.5, 3, 1, "#c0a000");
            }
            break;
        case "display":
            // an expansion card with a monitor port
            frame(0, 4, 16, 8, "#008000");
            for (var c = 0; c < 6; ++c) rect(2 + c * 2, 12, 1, 3, "#c0a000");
            frame(10, 6, 5, 4, "#0000c0");
            break;
        case "monitor":
            frame(1, 1, 14, 11, "#c0c0c0"); rect(3, 3, 10, 7, "#000080");
            rect(6, 12, 4, 2, "#808080"); rect(4, 14, 8, 2, "#808080");
            break;
        case "disk":
            frame(1, 4, 14, 9, "#c0c0c0"); rect(2, 5, 12, 2, "#ffffff");
            rect(11, 9, 2, 2, "#00c000");
            break;
        case "cdrom":
            ctx.fillStyle = "#e0e0e0"; ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.arc(8, 8, 7, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            ctx.fillStyle = "#ffffff"; ctx.beginPath(); ctx.arc(8, 8, 2, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            break;
        case "storage":
            frame(0, 4, 16, 8, "#008000");
            for (var s = 0; s < 6; ++s) rect(2 + s * 2, 12, 1, 3, "#c0a000");
            frame(2, 6, 5, 4, "#404040"); frame(8, 6, 5, 4, "#404040");
            break;
        case "network":
            frame(0, 1, 7, 6, "#008080"); frame(9, 1, 7, 6, "#008080");
            line(3.5, 7, 3.5, 10, "#000000"); line(12.5, 7, 12.5, 10, "#000000");
            line(3.5, 10.5, 12.5, 10.5, "#000000"); line(8, 10, 8, 13, "#000000");
            frame(5, 13, 6, 3, "#c0c0c0");
            break;
        case "sound":
            frame(1, 5, 4, 6, "#c0c0c0");
            ctx.fillStyle = "#808080"; ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(5, 5); ctx.lineTo(9, 1.5); ctx.lineTo(9, 14.5); ctx.lineTo(5, 11); ctx.closePath();
            ctx.fill(); ctx.stroke();
            ctx.strokeStyle = "#000080"; ctx.lineWidth = 1.2;
            ctx.beginPath(); ctx.arc(9, 8, 3, -0.8, 0.8); ctx.stroke();
            ctx.beginPath(); ctx.arc(9, 8, 5.5, -0.8, 0.8); ctx.stroke();
            break;
        case "keyboard":
            frame(0, 4, 16, 9, "#c0c0c0");
            for (var r = 0; r < 2; ++r) for (var k = 0; k < 6; ++k) rect(2 + k * 2 + r, 6 + r * 2, 1, 1, "#000000");
            rect(4, 10, 8, 1, "#000000");
            break;
        case "mouse":
            ctx.fillStyle = "#e0e0e0"; ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(4.5, 6); ctx.bezierCurveTo(4.5, 1.5, 11.5, 1.5, 11.5, 6);
            ctx.lineTo(11.5, 11); ctx.bezierCurveTo(11.5, 15.5, 4.5, 15.5, 4.5, 11);
            ctx.closePath(); ctx.fill(); ctx.stroke();
            line(4.5, 7.5, 11.5, 7.5, "#000000"); line(8, 2.5, 8, 7.5, "#000000");
            break;
        case "usb":
            // the trident-like USB plug outline
            line(8, 15, 8, 3, "#000000", 1.5);
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.moveTo(8, 0.5); ctx.lineTo(10.5, 4); ctx.lineTo(5.5, 4); ctx.closePath(); ctx.fill();
            line(8, 11, 4, 8, "#000000", 1.5); line(4, 8, 4, 6, "#000000", 1.5);
            ctx.beginPath(); ctx.arc(4, 5.5, 1.5, 0, Math.PI * 2); ctx.fill();
            line(8, 9, 12, 7, "#000000", 1.5); rect(11, 4.5, 3, 3, "#000000");
            ctx.beginPath(); ctx.arc(8, 14, 2, 0, Math.PI * 2); ctx.fill();
            break;
        case "bluetooth":
            ctx.fillStyle = "#0060d0"; ctx.strokeStyle = "#003080"; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.roundedRect(3.5, 0.5, 9, 15, 4.5, 4.5); ctx.fill(); ctx.stroke();
            ctx.strokeStyle = "#ffffff"; ctx.lineWidth = 1.3;
            ctx.beginPath();
            ctx.moveTo(5.5, 5); ctx.lineTo(10.5, 10.25); ctx.lineTo(8, 12.75); ctx.lineTo(8, 3.25);
            ctx.lineTo(10.5, 5.75); ctx.lineTo(5.5, 11);
            ctx.stroke();
            break;
        case "camera":
            frame(1, 4, 14, 10, "#404040"); rect(5, 2, 6, 2, "#404040");
            ctx.fillStyle = "#80c0ff"; ctx.strokeStyle = "#000000";
            ctx.beginPath(); ctx.arc(8, 9, 3.5, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            break;
        case "battery":
            rect(5, 0, 6, 2, "#000000"); frame(3, 1, 10, 15, "#ffffff"); rect(4, 6, 8, 9, "#00a000");
            break;
        default: // system
            frame(1, 3, 14, 10, "#008000");
            rect(3, 5, 4, 4, "#000000"); rect(9, 5, 4, 2, "#c0c0c0"); rect(9, 9, 4, 2, "#c0c0c0");
            for (var g = 0; g < 6; ++g) rect(2 + g * 2, 13, 1, 2, "#c0a000");
            break;
        }

        if (problem) {
            // yellow circle with "!" in the bottom right
            ctx.fillStyle = "#ffff00"; ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(11.5, 11.5, 4, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
            rect(11, 8.5, 1.5, 4, "#000000"); rect(11, 13.2, 1.5, 1.3, "#000000");
        }
    }
}
