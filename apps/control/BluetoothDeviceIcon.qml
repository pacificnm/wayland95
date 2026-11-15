import QtQuick

// Small pictures for Bluetooth devices, drawn on a 16x16 grid and scaled to
// `size`. `kind` is one of BluetoothInfo's kinds: audio, keyboard, mouse,
// gamepad, phone, computer, other.
Canvas {
    id: icon

    property string kind: "other"
    property int size: 16

    width: size
    height: size
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 16, size / 16);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }

        switch (kind) {
        case "audio":
            // headphones: headband and two ear cups
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.arc(8, 9, 6, Math.PI, 0); ctx.stroke();
            frame(1, 8, 4, 7, "#808080");
            frame(11, 8, 4, 7, "#808080");
            break;
        case "keyboard":
            frame(0, 4, 16, 9, "#c0c0c0");
            for (var r = 0; r < 2; ++r)
                for (var k = 0; k < 6; ++k)
                    rect(2 + k * 2 + r, 6 + r * 2, 1, 1, "#000000");
            rect(4, 10, 8, 1, "#000000"); // space bar
            break;
        case "mouse":
            ctx.fillStyle = "#e0e0e0";
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(4.5, 6); ctx.bezierCurveTo(4.5, 1.5, 11.5, 1.5, 11.5, 6);
            ctx.lineTo(11.5, 11); ctx.bezierCurveTo(11.5, 15.5, 4.5, 15.5, 4.5, 11);
            ctx.closePath(); ctx.fill(); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(4.5, 7.5); ctx.lineTo(11.5, 7.5); ctx.moveTo(8, 2.5); ctx.lineTo(8, 7.5); ctx.stroke();
            break;
        case "gamepad":
            frame(1, 5, 14, 7, "#808080");
            rect(3, 8, 4, 1, "#000000");  // d-pad
            rect(4, 7, 1, 3, "#000000");
            rect(10, 7, 2, 2, "#c00000"); // buttons
            rect(12, 9, 2, 2, "#0000c0");
            break;
        case "phone":
            frame(4, 0, 8, 16, "#404040");
            rect(5, 2, 6, 10, "#008080");  // screen
            rect(7, 13, 2, 1, "#c0c0c0");
            break;
        case "computer":
            frame(1, 1, 14, 10, "#c0c0c0");
            rect(3, 3, 10, 6, "#008080");
            rect(6, 11, 4, 2, "#808080");
            frame(3, 13, 10, 3, "#c0c0c0");
            break;
        default:
            // a small box sending radio waves
            frame(1, 7, 6, 8, "#c0c0c0");
            ctx.strokeStyle = "#000080";
            ctx.lineWidth = 1.2;
            for (var w = 0; w < 3; ++w) {
                ctx.beginPath(); ctx.arc(4, 7, 3.5 + w * 3.5, -Math.PI * 0.45, -Math.PI * 0.05); ctx.stroke();
            }
            break;
        }
    }
}
