import QtQuick

// Archive Manager's own icons, drawn on a 32x32 grid (scaled to `size`):
// the toolbar's (new, open, add, extract, view, delete, test) and the
// list's file kinds (folder, file, text, image, archive, exec, audio,
// video, pdf; best at 16). Disabled, they're drawn grey.
Canvas {
    id: icon

    property string name: ""
    property int size: 32
    property bool enabled: true

    width: size
    height: size
    onNameChanged: requestPaint()
    onEnabledChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.save();
        ctx.scale(size / 32, size / 32);
        draw(ctx, name);
        ctx.restore();
    }

    function draw(ctx, name) {
        var grey = !icon.enabled;
        function c(col) { return grey ? (col === "#000000" ? "#808080" : col === "#ffffff" ? "#ffffff" : "#c0c0c0") : col; }
        function rect(x, y, w, h, col) { ctx.fillStyle = c(col); ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, "#000000"); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function poly(pts, fill, outline) {
            ctx.beginPath();
            ctx.moveTo(pts[0], pts[1]);
            for (var i = 2; i < pts.length; i += 2)
                ctx.lineTo(pts[i], pts[i + 1]);
            ctx.closePath();
            if (fill) { ctx.fillStyle = c(fill); ctx.fill(); }
            if (outline) { ctx.strokeStyle = c(outline); ctx.lineWidth = 1; ctx.stroke(); }
        }
        // a page with a folded corner
        function page(x, y, w, h, fill) {
            poly([x + 0.5, y + 0.5, x + w - 6.5, y + 0.5, x + w - 0.5, y + 6.5, x + w - 0.5, y + h - 0.5, x + 0.5, y + h - 0.5],
                 fill || "#ffffff", "#000000");
            poly([x + w - 6.5, y + 0.5, x + w - 6.5, y + 6.5, x + w - 0.5, y + 6.5], "#c0c0c0", "#000000");
        }
        function lines(x, y, w, n) { for (var i = 0; i < n; ++i) rect(x, y + i * 3, w - (i % 2) * 3, 1, "#808080"); }
        // the archive: a card box with a zipper down its front
        function box(x, y, w, h) {
            poly([x + 0.5, y + 4.5, x + 4.5, y + 0.5, x + w + 3.5, y + 0.5, x + w - 0.5, y + 4.5], "#f0d070", "#000000");
            poly([x + w - 0.5, y + 4.5, x + w + 3.5, y + 0.5, x + w + 3.5, y + h - 4.5, x + w - 0.5, y + h - 0.5], "#a08020", "#000000");
            frame(x, y + 4, w, h - 4, "#e0b040");
            var zx = x + Math.floor(w / 2) - 1;
            rect(zx, y + 5, 3, h - 6, "#606060");
            for (var i = y + 6; i < y + h - 1; i += 2)
                rect(zx + (i % 4 === 0 ? 0 : 2), i, 1, 1, "#e0e0e0");
            rect(zx - 1, y + h - 6, 5, 4, "#c0c0c0");
            rect(zx, y + h - 5, 3, 1, "#000000");
        }
        function arrow(x, y, dir, fill) {
            // a fat arrow 10 long: dir "down", "up", "right", "left"
            var p = dir === "down" ? [x + 3, y, x + 7, y, x + 7, y + 5, x + 10, y + 5, x + 5, y + 10, x, y + 5, x + 3, y + 5]
                  : dir === "up" ? [x + 5, y, x + 10, y + 5, x + 7, y + 5, x + 7, y + 10, x + 3, y + 10, x + 3, y + 5, x, y + 5]
                  : dir === "right" ? [x, y + 3, x + 5, y + 3, x + 5, y, x + 10, y + 5, x + 5, y + 10, x + 5, y + 7, x, y + 7]
                  : [x + 10, y + 3, x + 5, y + 3, x + 5, y, x, y + 5, x + 5, y + 10, x + 5, y + 7, x + 10, y + 7];
            poly(p.map(function (v) { return v + 0.5; }), fill || "#00a000", "#000000");
        }
        function folder(x, y, w, h, open) {
            poly([x + 0.5, y + 2.5, x + 5.5, y + 0.5, x + 11.5, y + 0.5, x + 13.5, y + 2.5, x + w - 0.5, y + 2.5, x + w - 0.5, y + h - 0.5, x + 0.5, y + h - 0.5],
                 "#e0c000", "#000000");
            if (open)
                poly([x + 0.5, y + h - 0.5, x + 5.5, y + 7.5, x + w + 3.5, y + 7.5, x + w - 1.5, y + h - 0.5], "#ffff80", "#000000");
            else
                rect(x + 1, y + 3, w - 2, 1, "#ffff80");
        }

        switch (name) {
        case "new":
            page(6, 2, 20, 27);
            lines(10, 11, 12, 5);
            // a spark: new
            poly([6, 0.5, 7.5, 4.5, 11.5, 6, 7.5, 7.5, 6, 11.5, 4.5, 7.5, 0.5, 6, 4.5, 4.5], "#ffe000", "#806000");
            break;
        case "open":
            folder(2, 9, 24, 19, true);
            box(13, 2, 12, 12);
            break;
        case "add":
            page(2, 2, 16, 20);
            lines(5, 9, 9, 3);
            box(12, 14, 14, 16);
            arrow(13, 4, "down");
            break;
        case "extract":
            box(2, 12, 14, 18);
            folder(17, 16, 14, 12, false);
            arrow(17, 3, "right");
            break;
        case "view":
            page(3, 2, 20, 26);
            lines(7, 10, 12, 5);
            // magnifier
            ctx.beginPath(); ctx.arc(19.5, 18.5, 6, 0, Math.PI * 2);
            ctx.fillStyle = c("#c0e0ff"); ctx.fill();
            ctx.strokeStyle = c("#000000"); ctx.lineWidth = 2; ctx.stroke();
            ctx.lineWidth = 4; ctx.strokeStyle = c("#804000");
            ctx.beginPath(); ctx.moveTo(24, 23); ctx.lineTo(29, 28); ctx.stroke();
            break;
        case "delete":
            box(4, 8, 16, 20);
            ctx.lineWidth = 4; ctx.strokeStyle = c("#e00000"); ctx.lineCap = "round";
            ctx.beginPath(); ctx.moveTo(17, 4); ctx.lineTo(28, 15); ctx.moveTo(28, 4); ctx.lineTo(17, 15); ctx.stroke();
            break;
        case "test":
            box(3, 10, 16, 20);
            ctx.lineWidth = 4; ctx.strokeStyle = c("#008000"); ctx.lineCap = "round";
            ctx.beginPath(); ctx.moveTo(16, 10); ctx.lineTo(21, 16); ctx.lineTo(29, 3); ctx.stroke();
            break;
        // ---- the list's file kinds
        case "folder":
            folder(2, 7, 26, 20, false);
            break;
        case "archive":
            box(4, 4, 20, 26);
            break;
        case "image":
            page(5, 2, 22, 28);
            rect(8, 10, 16, 12, "#80c0ff");
            poly([8, 22, 13, 15, 17, 19, 20, 16, 24, 22], "#008000");
            rect(19, 11, 3, 3, "#ffe000");
            break;
        case "text":
            page(5, 2, 22, 28);
            lines(9, 10, 14, 6);
            break;
        case "audio":
            page(5, 2, 22, 28);
            rect(17, 9, 2, 13, "#000000");
            ctx.beginPath(); ctx.arc(15, 22, 3, 0, Math.PI * 2); ctx.fillStyle = c("#000080"); ctx.fill();
            rect(19, 9, 4, 2, "#000000");
            break;
        case "video":
            page(5, 2, 22, 28);
            frame(8, 11, 16, 12, "#000000");
            poly([13, 14, 19, 17, 13, 20], "#ffffff");
            break;
        case "pdf":
            page(5, 2, 22, 28);
            rect(8, 18, 16, 7, "#c00000");
            lines(9, 9, 14, 3);
            break;
        case "exec":
            frame(3, 6, 26, 20, "#c0c0c0");
            rect(4, 7, 24, 4, "#000080");
            rect(6, 14, 20, 9, "#ffffff");
            break;
        default:
            page(5, 2, 22, 28);
            break;
        }
    }
}
