import QtQuick

// A font file's icon: a page with a folded corner and a mark for its kind
// ("TT" TrueType, "O" OpenType, "A" anything else). Drawn on a 32x32
// grid and scaled to `size`.
Canvas {
    id: icon

    property string kind: "truetype"
    property int size: 32

    width: size
    height: size
    onKindChanged: requestPaint()
    onSizeChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.scale(size / 32, size / 32);

        // the page
        ctx.fillStyle = "#000000";
        ctx.beginPath();
        ctx.moveTo(5, 1); ctx.lineTo(21, 1); ctx.lineTo(28, 8); ctx.lineTo(28, 31); ctx.lineTo(5, 31);
        ctx.closePath(); ctx.fill();
        ctx.fillStyle = "#ffffff";
        ctx.beginPath();
        ctx.moveTo(6, 2); ctx.lineTo(20, 2); ctx.lineTo(27, 9); ctx.lineTo(27, 30); ctx.lineTo(6, 30);
        ctx.closePath(); ctx.fill();
        // the fold
        ctx.fillStyle = "#c0c0c0";
        ctx.beginPath(); ctx.moveTo(20, 2); ctx.lineTo(20, 9); ctx.lineTo(27, 9); ctx.closePath(); ctx.fill();
        ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(20.5, 2); ctx.lineTo(20.5, 8.5); ctx.lineTo(27, 8.5); ctx.stroke();

        ctx.textAlign = "center";
        ctx.textBaseline = "alphabetic";
        if (kind === "truetype") {
            // two overlapping T's, gray behind blue
            ctx.font = "bold 15px 'Liberation Serif'";
            ctx.fillStyle = "#808080";
            ctx.fillText("T", 13, 23);
            ctx.fillStyle = "#0000c0";
            ctx.fillText("T", 18, 27);
        } else if (kind === "opentype") {
            ctx.lineWidth = 3;
            ctx.strokeStyle = "#0060c0";
            ctx.beginPath(); ctx.ellipse(10, 13, 12, 14); ctx.stroke();
            ctx.strokeStyle = "#e00000";
            ctx.beginPath(); ctx.arc(16, 20, 6, Math.PI * 0.75, Math.PI * 1.25); ctx.stroke();
        } else {
            ctx.font = "bold 17px 'Liberation Serif'";
            ctx.fillStyle = "#c00000";
            ctx.fillText("A", 16, 26);
        }
    }
}
