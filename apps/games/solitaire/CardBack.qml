import QtQuick

// The back of a card: a white border around one of four patterns, all our
// own: 0 a blue lattice, 1 red diamonds, 2 green rings, 3 a teal "95".
Canvas {
    id: back

    property int design: 0

    onDesignChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        var w = width, h = height, m = 4;
        ctx.save();
        ctx.beginPath();
        ctx.rect(m, m, w - 2 * m, h - 2 * m);
        ctx.clip();
        var i, j;
        switch (design) {
        case 1:
            ctx.fillStyle = "#a00000";
            ctx.fillRect(0, 0, w, h);
            ctx.fillStyle = "#ffffff";
            for (j = 0; j < h; j += 10)
                for (i = (j / 10) % 2 ? 5 : 0; i < w; i += 10) {
                    ctx.beginPath();
                    ctx.moveTo(i, j - 3); ctx.lineTo(i + 3, j); ctx.lineTo(i, j + 3); ctx.lineTo(i - 3, j);
                    ctx.closePath(); ctx.fill();
                }
            break;
        case 2:
            ctx.fillStyle = "#006000";
            ctx.fillRect(0, 0, w, h);
            ctx.strokeStyle = "#60c060";
            ctx.lineWidth = 1;
            for (j = 6; j < h; j += 12)
                for (i = 6; i < w; i += 12) {
                    ctx.beginPath(); ctx.arc(i, j, 4.5, 0, 2 * Math.PI); ctx.stroke();
                }
            break;
        case 3:
            ctx.fillStyle = "#008080";
            ctx.fillRect(0, 0, w, h);
            ctx.fillStyle = "#006868";
            for (j = 0; j < h; j += 8)
                for (i = (j / 8) % 2 ? 0 : 8; i < w; i += 16)
                    ctx.fillRect(i, j, 8, 8);
            ctx.fillStyle = "#c0c0c0";
            ctx.fillRect(w / 2 - 18, h / 2 - 11, 36, 22);
            ctx.strokeStyle = "#000000";
            ctx.strokeRect(w / 2 - 17.5, h / 2 - 10.5, 35, 21);
            ctx.fillStyle = "#000080";
            ctx.font = "bold 16px sans-serif";
            ctx.textAlign = "center";
            ctx.textBaseline = "middle";
            ctx.fillText("95", w / 2, h / 2 + 1);
            break;
        default:
            ctx.fillStyle = "#000080";
            ctx.fillRect(0, 0, w, h);
            ctx.strokeStyle = "#4080ff";
            ctx.lineWidth = 1;
            for (i = -h; i < w + h; i += 8) {
                ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i + h, h); ctx.stroke();
                ctx.beginPath(); ctx.moveTo(i + h, 0); ctx.lineTo(i, h); ctx.stroke();
            }
            break;
        }
        ctx.restore();
    }
}
