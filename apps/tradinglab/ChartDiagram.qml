import QtQuick
import Win95Ui

// A schematic drawn inside a lesson: a price path with annotated lines and labels, on a 0-100 grid (y up), from a
// ```chartdiagram fenced block holding the Diagram JSON (data/strategyDoc.ts). Bad JSON shows a muted note instead of
// breaking the page. Port of components/ChartDiagram.tsx (an SVG of 200 x 110 units, drawn here on a Canvas scaled
// to the width it is given, at most 560 px like upstream's figure).
Item {
    id: root
    property string source: ""

    readonly property var diagram: {
        try {
            const d = JSON.parse(source);
            if (!d || !Array.isArray(d.path) || d.path.length < 2) return null;
            return d;
        } catch (e) {
            return null;
        }
    }
    readonly property real drawWidth: Math.min(560, width)
    implicitHeight: diagram ? Math.round(drawWidth * 110 / 200) + 16 : err.implicitHeight + 16

    Text {
        id: err
        visible: !root.diagram
        y: 8
        text: "(This diagram could not be drawn.)"
        textFormat: Text.PlainText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Bevel {
        visible: !!root.diagram
        y: 8
        width: root.drawWidth
        height: root.drawWidth * 110 / 200
        style: "sunken"
        face: Theme.window
        Canvas {
            id: canvas
            anchors.fill: parent
            anchors.margins: 2
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections { target: root; function onDiagramChanged() { canvas.requestPaint(); } }
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const d = root.diagram;
                if (!d || width <= 0) return;
                const k = width / 200;
                const sx = (x) => (10 + x * 1.8) * k;
                const sy = (y) => (98 - y * 0.9) * k;
                const font = (px, bold) => (bold ? "bold " : "") + Math.max(8, Math.round(px * k)) + "px \"" + Theme.fontFamily + "\"";
                ctx.fillStyle = "#ffffff";
                ctx.fillRect(0, 0, width, height);
                ctx.textBaseline = "alphabetic";
                if (d.title) {
                    ctx.font = font(6, true);
                    ctx.fillStyle = "#000000";
                    ctx.textAlign = "left";
                    ctx.fillText(d.title, 6 * k, 9 * k);
                }
                (d.lines || []).forEach(function (l) {
                    const col = l.tone === "target" ? "#1a4fd0" : "#c05a00";
                    ctx.strokeStyle = col;
                    ctx.lineWidth = Math.max(1, 0.9 * k);
                    // Qt counts dash lengths in line widths
                    ctx.setLineDash(l.style === "dashed" ? [3 * k / ctx.lineWidth, 2 * k / ctx.lineWidth] : []);
                    ctx.beginPath();
                    ctx.moveTo(sx(l.a[0]), sy(l.a[1]));
                    ctx.lineTo(sx(l.b[0]), sy(l.b[1]));
                    ctx.stroke();
                    ctx.setLineDash([]);
                    if (l.label) {
                        const hasLx = l.lx !== undefined && l.lx !== null;
                        const ly = hasLx ? l.a[1] + (l.b[1] - l.a[1]) * ((l.lx - l.a[0]) / (l.b[0] - l.a[0])) : l.b[1];
                        ctx.font = font(4.6, false);
                        ctx.fillStyle = col;
                        ctx.textAlign = "right";
                        ctx.fillText(l.label, sx(hasLx ? l.lx : l.b[0]), sy(ly) + (l.below ? 6 : -2) * k);
                    }
                });
                ctx.strokeStyle = "#000080";
                ctx.lineWidth = Math.max(1.5, 1.3 * k);
                ctx.lineJoin = "round";
                ctx.lineCap = "round";
                ctx.beginPath();
                d.path.forEach(function (p, i) { if (i === 0) ctx.moveTo(sx(p[0]), sy(p[1])); else ctx.lineTo(sx(p[0]), sy(p[1])); });
                ctx.stroke();
                (d.notes || []).forEach(function (n) {
                    ctx.font = font(4.8, false);
                    ctx.textAlign = "center";
                    ctx.lineWidth = 1.6 * k;
                    ctx.strokeStyle = "#ffffff";
                    ctx.strokeText(n.text, sx(n.at[0]), sy(n.at[1]));
                    ctx.fillStyle = "#000000";
                    ctx.fillText(n.text, sx(n.at[0]), sy(n.at[1]));
                });
            }
        }
    }
}
