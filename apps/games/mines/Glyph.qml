import QtQuick

// Small drawings for the minefield, on a 16x16 grid scaled to `size`:
// "mine", "wrong" (a flag that was wrong: a mine crossed out), "flag",
// and for the new-game button "ready", "pressed", "won" and "lost".
Canvas {
    id: glyph

    property string kind: "mine"
    property int size: 16

    width: size
    height: size
    onKindChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 16, size / 16);
        function rect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); }
        function mine() {
            // a round mine with four spikes and a shine
            rect(7.5, 2, 1, 12, "#000000");
            rect(2, 7.5, 12, 1, "#000000");
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(4, 4); ctx.lineTo(12, 12); ctx.moveTo(12, 4); ctx.lineTo(4, 12); ctx.stroke();
            ctx.fillStyle = "#000000";
            ctx.beginPath(); ctx.arc(8, 8, 4.2, 0, 2 * Math.PI); ctx.fill();
            rect(6, 6, 2, 2, "#ffffff");
        }
        function flag(x, y) {
            rect(x + 3, y + 10, 6, 1, "#000000");    // foot
            rect(x + 1, y + 11, 10, 2, "#000000");
            rect(x + 6, y + 2, 1, 9, "#000000");     // pole
            ctx.fillStyle = "#ff0000";               // pennant
            ctx.beginPath(); ctx.moveTo(x + 7, y + 1); ctx.lineTo(x + 7, y + 7); ctx.lineTo(x + 2, y + 4);
            ctx.closePath(); ctx.fill();
        }

        switch (kind) {
        case "mine":
            mine();
            break;
        case "wrong":
            mine();
            ctx.strokeStyle = "#ff0000";
            ctx.lineWidth = 1.5;
            ctx.beginPath(); ctx.moveTo(2, 2); ctx.lineTo(14, 14); ctx.moveTo(14, 2); ctx.lineTo(2, 14); ctx.stroke();
            break;
        case "flag":
            flag(2, 1);
            break;
        case "ready":
        case "pressed":
            // a mine, with a little fuse spark while a cell is held down
            mine();
            if (kind === "pressed") {
                rect(11, 1, 2, 2, "#ffff00");
                rect(13, 0, 1, 1, "#ff8000");
                rect(10, 0, 1, 1, "#ff8000");
            }
            break;
        case "won":
            flag(2, 1);
            break;
        case "lost":
            // a burst: yellow and red points around a dark centre
            ctx.fillStyle = "#ff0000";
            ctx.beginPath();
            for (var i = 0; i < 16; ++i) {
                var a = i * Math.PI / 8, r = (i % 2) ? 3.5 : 7.5;
                var px = 8 + r * Math.cos(a), py = 8 + r * Math.sin(a);
                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
            }
            ctx.closePath();
            ctx.fill();
            ctx.fillStyle = "#ffff00";
            ctx.beginPath(); ctx.arc(8, 8, 3, 0, 2 * Math.PI); ctx.fill();
            break;
        }
    }
}
