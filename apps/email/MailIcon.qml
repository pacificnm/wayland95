import QtQuick

// Email's icons, drawn on a 32x32 grid and scaled to `size` (32 for the
// toolbar, 16 for folders and messages). Generic drawings of our own.
// kind: compose reply replyall forward delete sendrecv inbox sent drafts
// trash junk folder account envelope envelope-open attach flag answered
Canvas {
    id: icon

    property string kind: "folder"
    property int size: 16
    property bool dim: false

    width: size
    height: size
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()
    onDimChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);
        var ink = dim ? "#808080" : "#000000";
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, ink); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function c(col) { return dim ? "#c0c0c0" : col; }
        function envelope(x, y, w, h, open) {
            frame(x, y, w, h, c("#ffffe8"));
            ctx.strokeStyle = ink; ctx.lineWidth = 1.5;
            ctx.beginPath();
            if (open) { ctx.moveTo(x, y); ctx.lineTo(x + w / 2, y - h / 2); ctx.lineTo(x + w, y); }
            ctx.moveTo(x + 1, y + 1); ctx.lineTo(x + w / 2, y + h * 0.6); ctx.lineTo(x + w - 1, y + 1);
            ctx.stroke();
        }
        function arrow(x1, y, x2, col) {
            ctx.strokeStyle = col; ctx.lineWidth = 3;
            ctx.beginPath(); ctx.moveTo(x1, y); ctx.lineTo(x2, y); ctx.stroke();
            ctx.fillStyle = col;
            var d = x2 < x1 ? -1 : 1;
            ctx.beginPath(); ctx.moveTo(x2 + d * 4, y); ctx.lineTo(x2 - d * 2, y - 6); ctx.lineTo(x2 - d * 2, y + 6); ctx.closePath(); ctx.fill();
        }
        function tray(fill) {
            ctx.fillStyle = ink;
            ctx.beginPath(); ctx.moveTo(2, 18); ctx.lineTo(8, 10); ctx.lineTo(24, 10); ctx.lineTo(30, 18); ctx.lineTo(30, 28);
            ctx.lineTo(2, 28); ctx.closePath(); ctx.fill();
            ctx.fillStyle = fill;
            ctx.beginPath(); ctx.moveTo(3, 18); ctx.lineTo(9, 11); ctx.lineTo(23, 11); ctx.lineTo(29, 18); ctx.lineTo(29, 27);
            ctx.lineTo(3, 27); ctx.closePath(); ctx.fill();
            rect(3, 18, 26, 1, ink);
            rect(11, 18, 10, 4, ink);
            rect(12, 18, 8, 3, fill);
        }
        switch (kind) {
        case "compose":
            envelope(2, 12, 22, 16, false);
            ctx.save(); ctx.translate(26, 4); ctx.rotate(0.6);
            frame(-2, 0, 5, 20, c("#e0b000")); rect(-2, 20, 5, 3, c("#ffd0a0"));
            ctx.restore();
            break;
        case "reply":
            envelope(8, 10, 22, 16, false);
            arrow(20, 8, 2, c("#c00000"));
            break;
        case "replyall":
            envelope(10, 12, 20, 14, false);
            arrow(22, 6, 4, c("#c00000"));
            arrow(22, 12, 2, c("#c00000"));
            break;
        case "forward":
            envelope(2, 10, 22, 16, false);
            arrow(12, 8, 28, c("#0060c0"));
            break;
        case "delete":
            // a bin, its lid ajar
            frame(8, 10, 16, 20, c("#c0c0c0"));
            rect(11, 13, 1, 14, ink); rect(15, 13, 1, 14, ink); rect(19, 13, 1, 14, ink);
            ctx.save(); ctx.translate(16, 7); ctx.rotate(-0.15);
            frame(-10, -2, 20, 4, c("#c0c0c0")); frame(-3, -5, 6, 4, c("#c0c0c0"));
            ctx.restore();
            ctx.strokeStyle = c("#c00000"); ctx.lineWidth = 3;
            ctx.beginPath(); ctx.moveTo(2, 14); ctx.lineTo(10, 24); ctx.moveTo(10, 14); ctx.lineTo(2, 24); ctx.stroke();
            break;
        case "sendrecv":
            envelope(4, 12, 24, 16, false);
            arrow(6, 5, 26, c("#008000"));
            arrow(26, 5, 6, c("#0060c0"));
            break;
        case "inbox":
            tray(c("#ffffff"));
            envelope(9, 3, 14, 10, false);
            break;
        case "sent":
            envelope(2, 12, 20, 14, false);
            ctx.fillStyle = c("#0060c0");
            ctx.beginPath(); ctx.moveTo(14, 8); ctx.lineTo(30, 2); ctx.lineTo(24, 18); ctx.lineTo(20, 12); ctx.closePath(); ctx.fill();
            break;
        case "drafts":
            frame(5, 3, 18, 26, c("#ffffff"));
            for (var l = 0; l < 4; ++l) rect(8, 8 + l * 4, 12, 1, c("#000080"));
            ctx.save(); ctx.translate(25, 10); ctx.rotate(0.5);
            frame(-2, 0, 5, 18, c("#e0b000")); rect(-2, 18, 5, 3, c("#ffd0a0"));
            ctx.restore();
            break;
        case "trash":
            frame(7, 8, 18, 22, c("#c0c0c0"));
            rect(11, 11, 1, 16, ink); rect(16, 11, 1, 16, ink); rect(21, 11, 1, 16, ink);
            frame(5, 5, 22, 4, c("#c0c0c0"));
            frame(13, 2, 6, 4, c("#c0c0c0"));
            break;
        case "junk":
            tray(c("#e0e0e0"));
            ctx.strokeStyle = c("#c00000"); ctx.lineWidth = 3;
            ctx.beginPath(); ctx.arc(16, 9, 7, 0, 2 * Math.PI); ctx.moveTo(11, 4); ctx.lineTo(21, 14); ctx.stroke();
            break;
        case "account":
            // a globe on a stand: the mail server
            ctx.fillStyle = ink; ctx.beginPath(); ctx.arc(16, 13, 11, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = c("#4090e0"); ctx.beginPath(); ctx.arc(16, 13, 10, 0, 2 * Math.PI); ctx.fill();
            ctx.fillStyle = c("#30a030");
            ctx.beginPath(); ctx.moveTo(10, 7); ctx.lineTo(16, 6); ctx.lineTo(15, 12); ctx.lineTo(9, 14); ctx.closePath(); ctx.fill();
            ctx.beginPath(); ctx.moveTo(18, 14); ctx.lineTo(24, 12); ctx.lineTo(22, 20); ctx.lineTo(18, 21); ctx.closePath(); ctx.fill();
            frame(8, 26, 16, 4, c("#c0c0c0"));
            rect(15, 24, 2, 3, ink);
            break;
        case "envelope":
            envelope(2, 8, 28, 18, false);
            break;
        case "envelope-open":
            envelope(2, 14, 28, 16, true);
            frame(7, 6, 18, 12, c("#ffffff"));
            rect(10, 9, 12, 1, c("#808080")); rect(10, 12, 9, 1, c("#808080"));
            break;
        case "attach":
            ctx.strokeStyle = ink; ctx.lineWidth = 2.5;
            ctx.beginPath();
            ctx.moveTo(20, 6); ctx.lineTo(20, 22); ctx.arc(15, 22, 5, 0, Math.PI); ctx.lineTo(10, 8);
            ctx.arc(13.5, 8, 3.5, Math.PI, 0); ctx.lineTo(17, 20);
            ctx.stroke();
            break;
        case "flag":
            rect(8, 3, 3, 27, ink);
            ctx.fillStyle = c("#e00000");
            ctx.beginPath(); ctx.moveTo(11, 4); ctx.lineTo(27, 9); ctx.lineTo(11, 16); ctx.closePath(); ctx.fill();
            break;
        case "answered":
            envelope(6, 12, 24, 16, true);
            arrow(16, 6, 2, c("#800080"));
            break;
        default: // folder
            frame(1, 7, 13, 5, c("#e8c048"));
            frame(1, 10, 30, 19, c("#f8d878"));
            rect(2, 11, 28, 1, c("#fff0b0"));
            break;
        }
    }
}
