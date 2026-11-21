import QtQuick
import Win95Ui

// Draws a candle pattern's example bars ([open, high, low, close] on 0-100). Bars before the last `size` are context
// (dimmed); the pattern itself is highlighted and, in the large view, numbered. Port of components/PatternChart.tsx.
Canvas {
    id: chart
    property var bars: []
    property int size: 1
    property bool big: false
    implicitWidth: big ? 360 : 120
    implicitHeight: big ? 220 : 80
    onBarsChanged: requestPaint()
    onSizeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        const bs = bars || [];
        const n = bs.length;
        if (!n || width <= 0) return;
        const k = width / (big ? 360 : 120);
        const W = width, H = height;
        const padX = (big ? 18 : 8) * k, padT = (big ? 12 : 6) * k, padB = (big ? 26 : 6) * k;
        let lo = Infinity, hi = -Infinity;
        bs.forEach(function (b) { lo = Math.min(lo, b[2]); hi = Math.max(hi, b[1]); });
        const y = (v) => padT + ((hi - v) / (hi - lo || 1)) * (H - padT - padB);
        const slot = (W - padX * 2) / n, cw = Math.min((big ? 30 : 12) * k, slot * 0.62);
        const x = (i) => padX + slot * (i + 0.5);
        const first = n - size;
        ctx.fillStyle = Qt.rgba(0, 122 / 255, 204 / 255, 0.12);
        ctx.fillRect(padX + slot * first, 0, slot * size, H);
        for (let i = 0; i < n; ++i) {
            const b = bs[i];
            const col = b[3] >= b[0] ? "#26a69a" : "#ef5350";
            const inPat = i >= first;
            ctx.globalAlpha = inPat ? 1 : 0.4;
            ctx.strokeStyle = col;
            ctx.lineWidth = (big ? 2 : 1.4) * k;
            ctx.lineCap = "round";
            ctx.beginPath();
            ctx.moveTo(x(i), y(b[1]));
            ctx.lineTo(x(i), y(b[2]));
            ctx.stroke();
            const top = y(Math.max(b[0], b[3])), h = Math.max(1.5, Math.abs(y(b[0]) - y(b[3])));
            ctx.fillStyle = col;
            ctx.fillRect(x(i) - cw / 2, top, cw, h);
            if (big && inPat && size > 1) {
                ctx.globalAlpha = 1;
                ctx.fillStyle = "#606060";
                ctx.font = "11px \"" + Theme.fontFamily + "\"";
                ctx.textAlign = "center";
                ctx.fillText(String(i - first + 1), x(i), H - 8 * k);
            }
        }
        ctx.globalAlpha = 1;
    }
}
