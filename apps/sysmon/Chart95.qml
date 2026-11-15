import QtQuick
import Win95Ui

// A System Monitor chart: a title, the current value(s) on the right, and a
// sunken white panel with grid lines and one or two lines of history,
// newest at the right.
//
// series: [{ values: [...], color: "#..." }]
// maxValue: fixed top of the scale (percent charts), or 0 to fit the data
Item {
    id: chart

    property string title: ""
    property string valueText: ""   // may contain <font color=...>
    property var series: []
    property real maxValue: 100
    property int historyLength: 60
    // for automatic scales: how to label the top line
    property var formatScale: function (v) { return v; }

    // the top of the scale: fixed, or the smallest "nice" value that fits
    readonly property real scaleTop: {
        if (maxValue > 0)
            return maxValue;
        var m = 0;
        for (var s = 0; s < series.length; ++s)
            for (var i = 0; i < series[s].values.length; ++i)
                m = Math.max(m, series[s].values[i]);
        // 1, 2 or 5 times a power of ten, in KB/s below 1 MB/s and MB/s above
        var need = Math.max(1024, m * 1.1);
        var unit = need < 1048576 ? 1024 : 1048576;
        for (var p = 1; ; p *= 10) {
            for (var k of [1, 2, 5]) {
                if (k * p * unit >= need)
                    return k * p * unit;
            }
        }
    }
    onScaleTopChanged: canvas.requestPaint()
    onSeriesChanged: canvas.requestPaint()

    Text {
        id: titleText
        text: chart.title
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        font.bold: true
    }
    Text {
        anchors.right: parent.right
        text: chart.valueText
        textFormat: Text.StyledText
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Bevel {
        id: panel
        y: titleText.height + 3
        width: parent.width
        height: parent.height - y
        style: "sunken"
        face: Theme.window

        Canvas {
            id: canvas
            anchors.fill: parent
            anchors.margins: 2
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var w = width, h = height;
                // grid: quarters across, a line every 10 samples down
                ctx.strokeStyle = "#d8d8d8";
                ctx.lineWidth = 1;
                for (var g = 1; g < 4; ++g) {
                    var gy = Math.round(h * g / 4) + 0.5;
                    ctx.beginPath(); ctx.moveTo(0, gy); ctx.lineTo(w, gy); ctx.stroke();
                }
                for (var v = 10; v < chart.historyLength; v += 10) {
                    var gx = Math.round(w - w * v / (chart.historyLength - 1)) + 0.5;
                    ctx.beginPath(); ctx.moveTo(gx, 0); ctx.lineTo(gx, h); ctx.stroke();
                }
                var top = chart.scaleTop;
                ctx.lineWidth = 1.5;
                for (var s = 0; s < chart.series.length; ++s) {
                    var values = chart.series[s].values;
                    if (values.length < 2)
                        continue;
                    ctx.strokeStyle = chart.series[s].color;
                    ctx.beginPath();
                    for (var i = 0; i < values.length; ++i) {
                        // newest sample at the right edge
                        var x = w - (values.length - 1 - i) * w / (chart.historyLength - 1);
                        var y = h - 1 - Math.min(1, values[i] / top) * (h - 2);
                        if (i === 0)
                            ctx.moveTo(x, y);
                        else
                            ctx.lineTo(x, y);
                    }
                    ctx.stroke();
                }
            }
        }
        Text {
            x: 4
            y: 3
            visible: chart.maxValue <= 0
            text: chart.formatScale(chart.scaleTop)
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize - 1
        }
    }
}
