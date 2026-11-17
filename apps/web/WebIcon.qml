import QtQuick

// Web Browser's icons, drawn on a 32x32 grid and scaled to `size` (24 for
// the toolbar, 16 for tabs and lists). Generic drawings of our own.
// kind: back forward stop refresh home favorites history downloads page
// loading lock globe file star
Canvas {
    id: icon

    property string kind: "page"
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
        function c(col) { return dim ? "#c0c0c0" : col; }
        function rect(x, y, w, h, col) { ctx.fillStyle = col; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, ink); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function poly(pts, fill) {
            ctx.beginPath();
            ctx.moveTo(pts[0], pts[1]);
            for (var i = 2; i < pts.length; i += 2)
                ctx.lineTo(pts[i], pts[i + 1]);
            ctx.closePath();
            ctx.fillStyle = fill;
            ctx.fill();
            ctx.strokeStyle = ink;
            ctx.lineWidth = 1.5;
            ctx.stroke();
        }
        function globe(cx, cy, r) {
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.fillStyle = c("#3a7bd5"); ctx.fill();
            ctx.save();
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.clip();
            ctx.fillStyle = c("#3c9a3c");
            ctx.beginPath(); ctx.ellipse(cx - r * 0.9, cy - r * 0.8, r * 1.0, r * 0.9); ctx.fill();
            ctx.beginPath(); ctx.ellipse(cx + r * 0.05, cy + r * 0.05, r * 0.8, r * 0.9); ctx.fill();
            ctx.restore();
            ctx.strokeStyle = ink; ctx.lineWidth = 1.2;
            ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke();
            ctx.beginPath(); ctx.ellipse(cx - r * 0.45, cy - r, r * 0.9, r * 2); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy); ctx.stroke();
        }
        function star(cx, cy, r, fill) {
            var pts = [];
            for (var i = 0; i < 10; ++i) {
                var a = -Math.PI / 2 + i * Math.PI / 5;
                var rr = i % 2 ? r * 0.45 : r;
                pts.push(cx + Math.cos(a) * rr, cy + Math.sin(a) * rr);
            }
            poly(pts, fill);
        }
        function page(x, y) {
            poly([x, y, x + 15, y, x + 21, y + 6, x + 21, y + 26, x, y + 26], c("#ffffff"));
            ctx.beginPath(); ctx.moveTo(x + 15, y); ctx.lineTo(x + 15, y + 6); ctx.lineTo(x + 21, y + 6); ctx.stroke();
        }

        switch (kind) {
        case "back":
            poly([4, 16, 16, 4, 16, 11, 28, 11, 28, 21, 16, 21, 16, 28], c("#2f8f2f"));
            break;
        case "forward":
            poly([28, 16, 16, 4, 16, 11, 4, 11, 4, 21, 16, 21, 16, 28], c("#2f8f2f"));
            break;
        case "stop":
            poly([11, 3, 21, 3, 29, 11, 29, 21, 21, 29, 11, 29, 3, 21, 3, 11], c("#d03030"));
            ctx.strokeStyle = c("#ffffff"); ctx.lineWidth = 3.5;
            ctx.beginPath(); ctx.moveTo(11, 11); ctx.lineTo(21, 21); ctx.moveTo(21, 11); ctx.lineTo(11, 21); ctx.stroke();
            break;
        case "refresh":
        case "loading":
            ctx.strokeStyle = ink; ctx.lineWidth = 6;
            ctx.beginPath(); ctx.arc(16, 17, 10, -Math.PI * 0.15, Math.PI * 1.45); ctx.stroke();
            ctx.strokeStyle = c("#2f8f2f"); ctx.lineWidth = 3.5;
            ctx.beginPath(); ctx.arc(16, 17, 10, -Math.PI * 0.15, Math.PI * 1.45); ctx.stroke();
            poly([19, 1, 29, 8, 19, 14], c("#2f8f2f"));
            break;
        case "home":
            poly([3, 16, 16, 4, 29, 16, 25, 16, 25, 29, 7, 29, 7, 16], c("#f0e0b0"));
            poly([3, 16, 16, 4, 29, 16, 26, 18, 16, 9, 6, 18], c("#b03020"));
            frame(13, 20, 6, 9, c("#8a5a2a"));
            break;
        case "favorites":
        case "star":
            star(16, 17, 14, c("#f5c518"));
            break;
        case "history":
            ctx.beginPath(); ctx.arc(16, 16, 13, 0, Math.PI * 2);
            ctx.fillStyle = c("#ffffff"); ctx.fill();
            ctx.strokeStyle = ink; ctx.lineWidth = 2; ctx.stroke();
            for (var h = 0; h < 12; ++h) {
                var a = h * Math.PI / 6;
                rect(16 + Math.cos(a) * 10.5 - 1, 16 + Math.sin(a) * 10.5 - 1, 2, 2, ink);
            }
            ctx.lineWidth = 2.5;
            ctx.beginPath(); ctx.moveTo(16, 16); ctx.lineTo(16, 8); ctx.moveTo(16, 16); ctx.lineTo(22, 19); ctx.stroke();
            break;
        case "downloads":
            rect(4, 24, 24, 5, ink); rect(5, 25, 22, 3, c("#c0c0c0"));
            poly([11, 3, 21, 3, 21, 12, 27, 12, 16, 23, 5, 12, 11, 12], c("#2f6fd0"));
            break;
        case "lock":
            ctx.strokeStyle = ink; ctx.lineWidth = 3.5;
            ctx.beginPath(); ctx.arc(16, 13, 7, Math.PI, 0); ctx.lineTo(23, 16); ctx.moveTo(9, 16); ctx.lineTo(9, 13); ctx.stroke();
            frame(5, 15, 22, 15, c("#e0b020"));
            rect(15, 19, 2, 6, ink);
            break;
        case "file":
            page(6, 3);
            break;
        case "globe":
            globe(16, 16, 13);
            break;
        default: // "page": a page with the world on it
            page(4, 3);
            globe(20, 20, 9);
            break;
        }
    }
}
