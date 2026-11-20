import QtQuick

// Icon95: Wayland 95's own small icons, drawn here on a 16x16 grid (any
// size: they're scaled), the way Windows 95's toolbars drew theirs. The
// names, and what the Icon chooser calls them, are in icons.json.
//
//   Icon95 { name: "save" }                 // 16x16
//   Icon95 { name: "print"; size: 32 }      // twice the size
//   Icon95 { name: "cut"; enabled: false }  // grey, with a white shadow
//
// Disabled, an icon is drawn as Windows 95 drew a toolbar's: its dark
// parts grey with a white copy a pixel down and right; its light parts
// gone. A name it doesn't know draws nothing (`known` is false).
Item {
    id: icon

    property string name: ""
    property int size: 16
    property bool enabled: true
    readonly property bool known: names.indexOf(name) >= 0

    implicitWidth: size
    implicitHeight: size

    // (icons.json's names, for `known`; kept in step by tests/studio/run.sh)
    readonly property var names: [
        "new", "document", "open", "save", "print", "folder", "cut", "copy", "paste", "undo", "redo", "find",
        "add", "remove", "delete", "edit", "ok", "refresh", "first", "previous", "next", "last", "back",
        "forward", "up", "down", "home", "exit", "help", "info", "warning", "error", "settings", "filter",
        "sort", "person", "people", "mail", "phone", "calendar", "datetime", "clock", "book", "chart", "table",
        "database", "money", "cart", "tag", "star", "heart", "flag", "bell", "lock", "key", "globe", "link",
        "picture", "music", "play", "pause", "stop", "bullets", "numbers", "checks", "indent", "outdent",
        "quote", "code", "rule"
    ]

    // enabled: the icon; disabled, a white copy a pixel down and right
    // under a grey one (each draws the icon's dark colors in its own
    // color, its light ones erasing what they're drawn over)
    Layer { mode: "white"; x: Math.max(1, Math.round(icon.size / 16)); y: x; visible: !icon.enabled }
    Layer { mode: icon.enabled ? "color" : "grey" }

    component Layer: Canvas {
        id: layer
        property string mode: "color"
        width: icon.width
        height: icon.height
        visible: icon.known
        property string drawn: icon.name + "/" + mode + "/" + icon.size
        onDrawnChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.clearRect(0, 0, width, height);
            if (!icon.known || width <= 0)
                return;
            ctx.save();
            ctx.scale(width / 16, height / 16);
            icon.draw(mode === "color" ? ctx : icon.flat(ctx, mode === "white" ? "#ffffff" : "#808080"), icon.name);
            ctx.restore();
        }
    }

    // a context that draws dark colors in `ink` and erases with light ones
    function flat(ctx, ink) {
        function dark(c) {
            var m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(String(c));
            if (!m)
                return true;
            return 0.3 * parseInt(m[1], 16) + 0.59 * parseInt(m[2], 16) + 0.11 * parseInt(m[3], 16) < 190;
        }
        var w = {};
        ["beginPath", "moveTo", "lineTo", "closePath", "arc", "ellipse", "quadraticCurveTo", "bezierCurveTo",
         "save", "restore", "translate", "scale", "rotate", "fill", "stroke", "fillRect", "fillText"].forEach(function (m) {
            w[m] = function () { return ctx[m].apply(ctx, arguments); };
        });
        function paint(c) {
            ctx.globalCompositeOperation = dark(c) ? "source-over" : "destination-out";
            return dark(c) ? ink : "#000000";
        }
        Object.defineProperty(w, "fillStyle", { set: function (c) { ctx.fillStyle = paint(c); } });
        Object.defineProperty(w, "strokeStyle", { set: function (c) { ctx.strokeStyle = paint(c); } });
        Object.defineProperty(w, "lineWidth", { set: function (v) { ctx.lineWidth = v; } });
        Object.defineProperty(w, "lineCap", { set: function (v) { ctx.lineCap = v; } });
        Object.defineProperty(w, "font", { set: function (v) { ctx.font = v; } });
        return w;
    }

    function draw(ctx, name) {
        var ink = "#000000", navy = "#000080", blue = "#0000ff", red = "#ff0000", dred = "#800000",
            green = "#008000", yellow = "#ffff00", gold = "#e0b000", white = "#ffffff", grey = "#c0c0c0", dgrey = "#808080";
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function frame(x, y, w, h, fill) { rect(x, y, w, h, ink); rect(x + 1, y + 1, w - 2, h - 2, fill); }
        function lines(x, y, w, n, gap) { for (var i = 0; i < n; ++i) rect(x, y + i * gap, w, 1, ink); }
        function poly(points, fill, outline) {
            ctx.beginPath();
            ctx.moveTo(points[0], points[1]);
            for (var i = 2; i < points.length; i += 2)
                ctx.lineTo(points[i], points[i + 1]);
            ctx.closePath();
            if (fill) { ctx.fillStyle = fill; ctx.fill(); }
            if (outline) { ctx.strokeStyle = outline; ctx.lineWidth = 1; ctx.stroke(); }
        }
        function stroke(c, w, path) {
            ctx.strokeStyle = c; ctx.lineWidth = w; ctx.lineCap = "round";
            ctx.beginPath(); path(); ctx.stroke(); ctx.lineCap = "butt";
        }
        function circle(x, y, r, fill, outline, w) {
            ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * Math.PI);
            if (fill) { ctx.fillStyle = fill; ctx.fill(); }
            if (outline) { ctx.strokeStyle = outline; ctx.lineWidth = w || 1; ctx.stroke(); }
        }
        function page(x, y) {
            ctx.fillStyle = ink;
            ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x + 7, y); ctx.lineTo(x + 10, y + 3);
            ctx.lineTo(x + 10, y + 13); ctx.lineTo(x, y + 13); ctx.closePath(); ctx.fill();
            ctx.fillStyle = white;
            ctx.beginPath(); ctx.moveTo(x + 1, y + 1); ctx.lineTo(x + 6, y + 1); ctx.lineTo(x + 9, y + 4);
            ctx.lineTo(x + 9, y + 12); ctx.lineTo(x + 1, y + 12); ctx.closePath(); ctx.fill();
        }
        function text(t, x, y, px, c) {
            ctx.fillStyle = c; ctx.font = "bold " + px + "px sans-serif"; ctx.fillText(t, x, y);
        }
        function mirrored(fn) { ctx.save(); ctx.translate(16, 0); ctx.scale(-1, 1); fn(); ctx.restore(); }
        function turned(angle, fn) { ctx.save(); ctx.translate(8, 8); ctx.rotate(angle); ctx.translate(-8, -8); fn(); ctx.restore(); }

        function arrow(c) { poly([1, 8, 8, 1, 8, 5, 15, 5, 15, 11, 8, 11, 8, 15], c); }
        function undoArrow() {
            stroke(navy, 2, function () { ctx.arc(8, 9, 5, Math.PI * 1.1, Math.PI * 2.4, false); });
            poly([0, 6, 6, 6, 3, 11], navy);
        }
        function triangle() { poly([11, 3, 4, 8, 11, 13], ink); }
        function person(x, y, coat) {
            ctx.beginPath(); ctx.arc(x + 5, y + 14, 5, Math.PI, 2 * Math.PI); ctx.closePath();
            ctx.fillStyle = coat; ctx.fill(); ctx.strokeStyle = ink; ctx.lineWidth = 1; ctx.stroke();
            circle(x + 5, y + 5, 3, "#ffd8a8", ink);
        }

        switch (name) {
        case "new":
        case "document":
            page(3, 1);
            break;
        case "open":
            frame(1, 5, 12, 9, "#f8d878");
            frame(1, 4, 5, 2, "#e8c048");
            stroke(navy, 1.5, function () { ctx.moveTo(9, 3); ctx.quadraticCurveTo(12, 0, 14, 4); });
            rect(13, 4, 3, 1, navy);
            break;
        case "save":
            frame(1, 1, 14, 14, navy);
            rect(4, 2, 8, 5, white);
            rect(4, 10, 8, 5, grey);
            rect(9, 11, 2, 3, ink);
            break;
        case "print":
            frame(4, 1, 8, 6, white);
            frame(1, 6, 14, 7, grey);
            rect(12, 8, 2, 1, "#00c000");
            frame(4, 10, 8, 6, white);
            lines(6, 12, 4, 2, 2);
            break;
        case "folder":
            frame(1, 3, 6, 3, "#e8c048");
            frame(1, 5, 14, 10, "#f8d878");
            break;
        case "cut":
            stroke(ink, 1, function () { ctx.moveTo(5, 1); ctx.lineTo(10, 10); ctx.moveTo(11, 1); ctx.lineTo(6, 10); });
            circle(5, 12, 2.5, null, navy, 1.5);
            circle(11, 12, 2.5, null, navy, 1.5);
            break;
        case "copy":
            page(1, 0);
            page(5, 3);
            lines(7, 8, 5, 3, 2);
            break;
        case "paste":
            frame(1, 2, 11, 13, "#a06020");
            frame(4, 1, 5, 3, grey);
            page(6, 3);
            lines(8, 8, 5, 3, 2);
            break;
        case "undo":
            undoArrow();
            break;
        case "redo":
            mirrored(undoArrow);
            break;
        case "find":
            circle(6, 6, 4, "#e0f0ff", ink, 1.5);
            stroke(ink, 2.5, function () { ctx.moveTo(9, 9); ctx.lineTo(14, 14); });
            break;
        case "add":
            rect(6, 1, 4, 14, ink); rect(1, 6, 14, 4, ink);
            rect(7, 2, 2, 12, "#00a000"); rect(2, 7, 12, 2, "#00a000");
            break;
        case "remove":
            rect(1, 6, 14, 4, ink);
            rect(2, 7, 12, 2, red);
            break;
        case "delete":
            stroke("#c00000", 3, function () { ctx.moveTo(3, 3); ctx.lineTo(13, 13); ctx.moveTo(13, 3); ctx.lineTo(3, 13); });
            break;
        case "edit":
            turned(-Math.PI / 4, function () {
                frame(2, 6, 9, 4, yellow);
                frame(1, 6, 2, 4, "#ff8080");
                poly([11, 6, 15, 8, 11, 10], "#f0d0a0", ink);
                rect(14, 7.5, 1.5, 1, ink);
            });
            break;
        case "ok":
            stroke(green, 3, function () { ctx.moveTo(2, 8); ctx.lineTo(6, 12); ctx.lineTo(14, 3); });
            break;
        case "refresh":
            stroke(green, 2, function () { ctx.arc(8, 8, 5, Math.PI * 1.15, Math.PI * 1.9, false); });
            poly([11, 1, 15, 5, 10, 6], green);
            stroke(green, 2, function () { ctx.arc(8, 8, 5, Math.PI * 0.15, Math.PI * 0.9, false); });
            poly([5, 15, 1, 11, 6, 10], green);
            break;
        case "first":
            rect(3, 3, 2, 10, ink);
            ctx.save(); ctx.translate(1, 0); triangle(); ctx.restore();
            break;
        case "previous":
            triangle();
            break;
        case "next":
            mirrored(triangle);
            break;
        case "last":
            mirrored(function () { rect(3, 3, 2, 10, ink); ctx.translate(1, 0); triangle(); });
            break;
        case "back":
            arrow(navy);
            break;
        case "forward":
            mirrored(function () { arrow(navy); });
            break;
        case "up":
            turned(Math.PI / 2, function () { arrow(navy); });
            break;
        case "down":
            turned(-Math.PI / 2, function () { arrow(navy); });
            break;
        case "home":
            poly([0, 8, 8, 1, 16, 8], dred);
            frame(3, 8, 10, 7, "#ffffc0");
            rect(7, 10, 3, 5, ink);
            break;
        case "exit":
            frame(1, 1, 8, 14, "#a06020");
            rect(6, 8, 1, 1, yellow);
            poly([8, 6, 12, 6, 12, 3, 16, 8, 12, 13, 12, 10, 8, 10], red, ink);
            break;
        case "help":
            circle(8, 8, 7, yellow, ink);
            text("?", 4.5, 13, 12, navy);
            break;
        case "info":
            circle(8, 8, 7, blue, ink);
            rect(7, 3, 2, 2, white);
            rect(7, 6, 2, 7, white);
            break;
        case "warning":
            poly([8, 1, 15, 14, 1, 14], yellow, ink);
            rect(7, 5, 2, 5, ink);
            rect(7, 11, 2, 2, ink);
            break;
        case "error":
            circle(8, 8, 7, red, ink);
            stroke(white, 2, function () { ctx.moveTo(5, 5); ctx.lineTo(11, 11); ctx.moveTo(11, 5); ctx.lineTo(5, 11); });
            break;
        case "settings":
            ctx.fillStyle = dgrey;
            for (var t = 0; t < 8; ++t) {
                ctx.save(); ctx.translate(8, 8); ctx.rotate(t * Math.PI / 4);
                ctx.fillRect(-1.5, -8, 3, 4); ctx.restore();
            }
            circle(8, 8, 5, dgrey, ink);
            circle(8, 8, 2, white, ink);
            break;
        case "filter":
            poly([1, 2, 15, 2, 9, 9, 9, 14, 7, 15, 7, 9], navy, ink);
            break;
        case "sort":
            text("A", 0, 7, 7, ink);
            text("Z", 0, 15, 7, ink);
            rect(11, 2, 2, 8, navy);
            poly([8, 9, 16, 9, 12, 15], navy);
            break;
        case "person":
            person(3, 1, navy);
            break;
        case "people":
            person(6, 0, "#008080");
            person(0, 2, navy);
            break;
        case "mail":
            frame(1, 3, 14, 10, white);
            stroke(ink, 1, function () { ctx.moveTo(1.5, 3.5); ctx.lineTo(8, 9); ctx.lineTo(14.5, 3.5); });
            break;
        case "phone":
            poly([4, 8, 12, 8, 15, 14, 1, 14], dred, ink);
            circle(8, 11, 1.8, white, ink);
            ctx.beginPath(); ctx.moveTo(1, 7); ctx.quadraticCurveTo(8, 1, 15, 7); ctx.lineTo(13, 8); ctx.quadraticCurveTo(8, 4, 3, 8); ctx.closePath();
            ctx.fillStyle = ink; ctx.fill();
            break;
        case "calendar":
        case "datetime":
            frame(1, 2, 14, 13, white);
            rect(2, 3, 12, 3, "#c00000");
            for (var d = 0; d < 6; ++d)
                rect(3 + (d % 3) * 4, 8 + Math.floor(d / 3) * 3, 2, 2, ink);
            break;
        case "clock":
            circle(8, 8, 7, white, ink);
            stroke(ink, 1.5, function () { ctx.moveTo(8, 8); ctx.lineTo(8, 3); ctx.moveTo(8, 8); ctx.lineTo(11, 10); });
            break;
        case "book":
            frame(2, 1, 12, 14, dred);
            rect(12, 2, 1, 12, white);
            rect(5, 4, 5, 2, yellow);
            rect(4, 2, 1, 12, "#400000");
            break;
        case "chart":
            rect(1, 1, 1, 14, ink);
            rect(1, 14, 15, 1, ink);
            frame(3, 8, 4, 6, navy);
            frame(7, 3, 4, 11, red);
            frame(11, 10, 4, 4, green);
            break;
        case "table":
            frame(0, 1, 16, 14, white);
            rect(1, 2, 14, 3, navy);
            rect(5, 2, 1, 13, ink); rect(10, 2, 1, 13, ink);
            rect(1, 5, 14, 1, ink); rect(1, 10, 14, 1, ink);
            break;
        case "database":
            ctx.fillStyle = grey;
            ctx.fillRect(2, 3, 12, 10);
            ctx.beginPath(); ctx.ellipse(2, 10, 12, 5); ctx.fillStyle = grey; ctx.fill(); ctx.strokeStyle = ink; ctx.lineWidth = 1; ctx.stroke();
            rect(2, 3, 12, 10, grey);
            rect(1.5, 3, 1, 10, ink); rect(13.5, 3, 1, 10, ink);
            ctx.beginPath(); ctx.ellipse(2, 1, 12, 5); ctx.fillStyle = "#e0e0e0"; ctx.fill(); ctx.strokeStyle = ink; ctx.stroke();
            stroke(ink, 1, function () { ctx.moveTo(2, 7); ctx.quadraticCurveTo(8, 10, 14, 7); });
            break;
        case "money":
            frame(0, 3, 16, 10, "#a0d0a0");
            circle(8, 8, 3, "#e0f0e0", "#004000");
            text("$", 6, 11, 7, "#004000");
            break;
        case "cart":
            stroke(ink, 1, function () { ctx.moveTo(0, 2); ctx.lineTo(3, 2); ctx.lineTo(5, 10); ctx.lineTo(13, 10); });
            poly([4, 4, 15, 4, 13, 9, 5, 9], "#c0c0ff", ink);
            circle(6, 13, 1.5, ink);
            circle(12, 13, 1.5, ink);
            break;
        case "tag":
            poly([1, 1, 8, 1, 15, 8, 8, 15, 1, 8], "#ffffc0", ink);
            circle(4.5, 4.5, 1.3, white, ink);
            break;
        case "star":
            var pts = [];
            for (var s = 0; s < 10; ++s) {
                var r = s % 2 === 0 ? 7.5 : 3;
                var a = -Math.PI / 2 + s * Math.PI / 5;
                pts.push(8 + r * Math.cos(a), 8.5 + r * Math.sin(a));
            }
            poly(pts, yellow, ink);
            break;
        case "heart":
            ctx.beginPath();
            ctx.moveTo(8, 15);
            ctx.bezierCurveTo(0, 9, 0, 2, 4.5, 2);
            ctx.bezierCurveTo(6.5, 2, 8, 4, 8, 5);
            ctx.bezierCurveTo(8, 4, 9.5, 2, 11.5, 2);
            ctx.bezierCurveTo(16, 2, 16, 9, 8, 15);
            ctx.closePath();
            ctx.fillStyle = red; ctx.fill(); ctx.strokeStyle = ink; ctx.lineWidth = 1; ctx.stroke();
            break;
        case "flag":
            rect(2, 1, 1, 15, ink);
            poly([3, 2, 14, 2, 11, 5, 14, 8, 3, 8], red, ink);
            break;
        case "bell":
            ctx.beginPath(); ctx.moveTo(2, 12); ctx.quadraticCurveTo(4, 10, 4, 6); ctx.arc(8, 6, 4, Math.PI, 2 * Math.PI);
            ctx.quadraticCurveTo(12, 10, 14, 12); ctx.closePath();
            ctx.fillStyle = gold; ctx.fill(); ctx.strokeStyle = ink; ctx.lineWidth = 1; ctx.stroke();
            circle(8, 13.5, 1.5, ink);
            rect(7, 1, 2, 1, ink);
            break;
        case "lock":
            stroke(ink, 2, function () { ctx.moveTo(5, 8); ctx.lineTo(5, 5); ctx.arc(8, 5, 3, Math.PI, 2 * Math.PI); ctx.lineTo(11, 8); });
            frame(3, 7, 10, 8, gold);
            rect(7, 9, 2, 4, ink);
            break;
        case "key":
            circle(4.5, 8, 3, null, gold, 2);
            circle(4.5, 8, 3.8, null, ink, 0.6);
            rect(7, 7, 8, 2, gold);
            rect(12, 9, 1, 3, gold);
            rect(14, 9, 1, 2, gold);
            break;
        case "globe":
            circle(8, 8, 7, "#3070ff", ink);
            poly([4, 4, 8, 3, 9, 6, 6, 9, 3, 8], "#00a000");
            poly([9, 10, 13, 8, 13, 12, 10, 14], "#00a000");
            stroke(ink, 0.6, function () { ctx.ellipse(4.5, 1, 7, 14); });
            break;
        case "link":
            stroke(navy, 1.5, function () { ctx.moveTo(7, 5); ctx.lineTo(4, 5); ctx.arc(4, 8, 3, Math.PI * 1.5, Math.PI * 0.5, true); ctx.lineTo(7, 11); });
            stroke(navy, 1.5, function () { ctx.moveTo(9, 5); ctx.lineTo(12, 5); ctx.arc(12, 8, 3, Math.PI * 1.5, Math.PI * 0.5, false); ctx.lineTo(9, 11); });
            rect(5, 7.5, 6, 1.5, ink);
            break;
        case "picture":
            frame(0, 2, 16, 12, "#80c0ff");
            poly([1, 13, 6, 6, 10, 11, 12, 9, 15, 13], green);
            rect(11, 4, 3, 3, yellow);
            break;
        case "music":
            rect(10, 2, 2, 10, ink);
            poly([12, 2, 15, 4, 15, 7, 12, 5], ink);
            ctx.beginPath(); ctx.ellipse(5, 10, 7, 5); ctx.fillStyle = ink; ctx.fill();
            break;
        case "play":
            poly([4, 2, 13, 8, 4, 14], ink);
            break;
        case "pause":
            rect(3, 3, 4, 10, ink);
            rect(9, 3, 4, 10, ink);
            break;
        case "stop":
            rect(3, 3, 10, 10, ink);
            break;
        case "bullets":
            for (var b = 0; b < 3; ++b) {
                rect(1, 2 + b * 5, 3, 3, navy);
                rect(6, 3 + b * 5, 9, 1, ink);
            }
            break;
        case "numbers":
            for (var n = 0; n < 3; ++n) {
                text(String(n + 1), 1, 6 + n * 5, 6, navy);
                rect(6, 3 + n * 5, 9, 1, ink);
            }
            break;
        case "checks":
            for (var c = 0; c < 2; ++c) {
                frame(1, 2 + c * 7, 6, 6, white);
                rect(9, 4 + c * 7, 6, 1, ink);
            }
            stroke(navy, 1.5, function () { ctx.moveTo(2, 4); ctx.lineTo(4, 6); ctx.lineTo(8, 0); });
            break;
        case "indent":
        case "outdent":
            lines(1, 1, 14, 2, 13);
            lines(7, 5, 8, 3, 3);
            if (name === "indent")
                poly([1, 5, 5, 8, 1, 11], navy);
            else
                poly([5, 5, 1, 8, 5, 11], navy);
            break;
        case "quote":
            rect(1, 1, 2, 14, navy);
            lines(5, 3, 10, 4, 3);
            break;
        case "code":
            stroke(navy, 1.5, function () { ctx.moveTo(5, 3); ctx.lineTo(1, 8); ctx.lineTo(5, 13); ctx.moveTo(11, 3); ctx.lineTo(15, 8); ctx.lineTo(11, 13); });
            stroke(ink, 1, function () { ctx.moveTo(9.5, 2); ctx.lineTo(6.5, 14); });
            break;
        case "rule":
            lines(1, 2, 14, 2, 2);
            rect(0, 7, 16, 2, navy);
            lines(1, 11, 14, 2, 2);
            break;
        }
    }
}
