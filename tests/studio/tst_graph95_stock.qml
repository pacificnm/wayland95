import QtQuick
import QtTest
import Win95Ui

// Graph95's stock graphs (tests/studio/run.sh runs it with qmltestrunner): what the indicators
// work out to, a bar's item and tip, tick() and addBar() keeping maxBars, the wheel, dragging
// and a double-click, and each type drawn
Item {
    width: 1300; height: 900
    // a made-up walk: 120 bars
    function walk(n) {
        var seed = 7, labels = [], o = [], h = [], l = [], c = [], v = [], p = 100;
        function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
        for (var i = 0; i < n; ++i) {
            var op = p, cl = Math.max(1, op + (rnd() - 0.48) * 4);
            o.push(op); c.push(cl); h.push(Math.max(op, cl) + rnd() * 2); l.push(Math.min(op, cl) - rnd() * 2);
            v.push(Math.round(1000 + rnd() * 5000)); p = cl;
            labels.push("D" + (i + 1));
        }
        return { labels: labels, series: [{ name: "ACME", open: o, high: h, low: l, close: c, volume: v }] };
    }
    property var d: walk(120)
    Graph95 { id: g1; x: 0; y: 0; width: 640; height: 440; type: "Candlestick"; labels: d.labels; series: d.series
              overlays: ["SMA 20", "Bollinger 20 2"]; studies: ["Volume", "RSI 14"]; title: "ACME"; legend: "Top"; showLast: 60 }
    Graph95 { id: g2; x: 650; y: 0; width: 640; height: 440; type: "OHLC"; labels: d.labels; series: d.series
              candleColors: "Green and red"; overlays: ["EMA 12", "VWAP"]; studies: ["Volume", "MACD"]; legend: "None"; showLast: 50 }
    Graph95 { id: g3; x: 0; y: 450; width: 640; height: 440; type: "Heikin-Ashi"; labels: d.labels; series: d.series
              candleColors: "Green and red"; studies: ["Stochastic 14 3"]; legend: "Right" }
    Graph95 { id: g4; x: 650; y: 450; width: 300; height: 200; type: "Price"; labels: d.labels; series: d.series; legend: "None"; title: "Price" }
    Graph95 { id: g5; x: 960; y: 450; width: 160; height: 100; type: "HLC"; labels: d.labels.slice(0, 20); series: [{ name: "x", values: [3,5,2,4,6,5,7,8,6,5,4,6,7,9,8,7,8,10,9,11] }]; legend: "None"; axisLabels: false }
    Graph95 { id: live; x: 650; y: 660; width: 400; height: 230; type: "Candlestick"; legend: "None"; studies: ["Volume"]; maxBars: 5 }

    TestCase {
        name: "Stock"
        when: windowShown
        function test_all() {
            wait(300);
            // indicators
            var g = g1;
            var sma = g.indicator("SMA 3");
            compare(sma[0], null); compare(sma[1], null);
            fuzzyCompare(sma[2], (d.series[0].close[0] + d.series[0].close[1] + d.series[0].close[2]) / 3, 1e-9);
            var bb = g.indicator("Bollinger 20 2");
            verify(bb.upper[19] > bb.middle[19] && bb.middle[19] > bb.lower[19]);
            compare(bb.upper[18], null);
            var r = g.indicator("RSI 14");
            compare(r[13], null); verify(r[14] > 0 && r[14] < 100);
            var m = g.indicator("MACD");
            compare(m.MACD[24], null); verify(m.MACD[25] !== null); compare(m.signal[32], null); verify(m.signal[33] !== null);
            fuzzyCompare(m.histogram[40], m.MACD[40] - m.signal[40], 1e-9);
            var st = g.indicator("Stoch 14 3");
            verify(st.K[13] >= 0 && st.K[13] <= 100); compare(st.D[14], null); verify(st.D[15] !== null);
            var e = g.indicator({ kind: "EMA", period: 3 });
            fuzzyCompare(e[2], sma[2], 1e-9);
            fuzzyCompare(e[3], e[2] + 0.5 * (d.series[0].close[3] - e[2]), 1e-9);
            compare(g.indicator("Volume")[0], d.series[0].volume[0]);
            compare(g.indicator("nope"), null);
            // itemAt: a bar in view
            var it = null;
            for (var x = 0; x < g.width && !it; x += 2) it = g.itemAt(x, 200);
            verify(it !== null); compare(it.index, 60);
            compare(it.close, d.series[0].close[60]);
            var tip = g.stockTip(it).split("\n");
            compare(tip[0], "D61");
            verify(tip.some(function (l) { return l.indexOf("RSI 14: ") === 0; }));
            // live
            live.tick("10:00", 10, 100);
            live.tick("10:00", 12, 50);
            live.tick("10:00", 9, 25);
            live.tick("10:00", 11, 25);
            compare(live.labels.length, 1);
            compare(JSON.stringify([live.series[0].open, live.series[0].high, live.series[0].low, live.series[0].close, live.series[0].volume]),
                    JSON.stringify([[10],[12],[9],[11],[200]]));
            var px = 11;
            for (var k = 1; k <= 7; ++k) { px += (k % 3 === 0 ? -1.5 : 1); live.tick("10:0" + k, px, 100 * k); live.tick("10:0" + k, px + 0.7, 10); }
            compare(live.labels.length, 5);
            compare(live.labels[0], "10:03");
            live.addBar("10:08", 14, 16, 13, 15.5, 900);
            compare(live.labels[4], "10:08"); compare(live.series[0].high[4], 16);
            // the wheel zooms in (fewer bars), dragging goes back, a double-click: the latest again
            compare(g1.viewRange(120).count, 60);
            mouseWheel(g1, 300, 150, 0, 120);
            verify(g1.viewRange(120).count < 60);
            var zoomed = g1.viewRange(120).count;
            mouseWheel(g1, 300, 150, 0, -120); mouseWheel(g1, 300, 150, 0, -120);
            verify(g1.viewRange(120).count > zoomed);
            mouseDrag(g1, 300, 150, 120, 0);
            verify(g1.panBack > 5);
            compare(g1.viewRange(120).end, 120 - Math.round(g1.panBack));
            var clicks = 0; g1.clicked.connect(function () { clicks++; });
            mouseDrag(g1, 300, 150, 40, 0);
            compare(clicks, 0);   // (a drag isn't a click)
            mouseDoubleClickSequence(g1, 300, 150);
            compare(g1.panBack, 0); compare(g1.zoomCount, 0);
            // new bars while scrolled back don't move what's in view
            mouseDrag(g1, 300, 150, 100, 0);
            var endBefore = g1.viewRange(g1.labels.length).end;
            g1.tick("new", 160, 10);
            compare(g1.viewRange(g1.labels.length).end, endBefore);
            mouseDoubleClickSequence(g1, 300, 150);
            // the crosshair over a bar
            mouseMove(g1, 300, 120);
            wait(100);
            verify(g1.children[g1.children.length - 1].visible);   // (the tip)
            // every type, every study, drawn without an error
            [g1, g2, g3, g4, g5, live].forEach(function (gg) { verify(grabImage(gg).width > 0); });
        }
    }
}
