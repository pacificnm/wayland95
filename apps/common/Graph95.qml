import QtQuick

// Graph95: Wayland 95's own graphs, drawn on a Canvas the way a Windows 95
// spreadsheet drew them: a white chart with a grey plot, black outlines,
// a boxed legend.
//
//   Graph95 {
//       type: "Column"            // Column, Bar, Line, Area, Pie, Doughnut, Scatter
//       labels: ["Jan", "Feb", "Mar"]                    // the categories (Scatter: x values)
//       series: [{ name: "Sales", values: [3, 5, 2] },   // one or more; color optional
//                { name: "Costs", values: [2, 1, 4], color: "#800000" }]
//       title: "Sales"; legend: "Right"   // None, Right, Bottom, Top
//       onClicked: (item) => console.log(item.label, item.value)
//   }
//
// Pie and Doughnut show the first series, a slice for each label. Stacked
// stacks Column, Bar and Area. itemAt(x, y) is what's there: { series,
// index, label, value, name } or null; hovering one shows its value.
//
// Stock graphs (Candlestick, Heikin-Ashi, OHLC, HLC, Price) draw a series
// with open, high, low and close lists (volume too, if it has one), a value
// for each label; a series of just values is a line over the prices:
//
//   Graph95 {
//       type: "Candlestick"
//       labels: ["Nov 3", "Nov 4", "Nov 5"]
//       series: [{ name: "ACME", open: [10, 11, 10.5], high: [11.2, 11.5, 11],
//                  low: [9.8, 10.4, 10.1], close: [11, 10.5, 10.9], volume: [1200, 900, 1500] }]
//       overlays: ["SMA 20", "Bollinger 20 2"]     // SMA, EMA, Bollinger, VWAP
//       studies: ["Volume", "RSI 14"]              // a pane each: Volume, RSI, MACD, Stochastic
//   }
//
// The values axis is on the right, with the last close on it. The wheel
// zooms, dragging goes back in time, a double-click shows the latest again.
// tick(label, price, volume) adds a trade as it happens (the same label:
// the same bar); addBar(label, open, high, low, close, volume) a whole bar.
// indicator("RSI 14") is what one works out to.
Item {
    id: graph

    property string type: "Column"
    property var labels: []
    property var series: []
    property string title: ""
    property string legend: "Right"
    property string xTitle: ""
    property string yTitle: ""
    property bool gridLines: true
    property bool showValues: false
    property bool stacked: false
    property bool markers: true
    property bool smooth: false
    property string palette: "Windows"
    // the value axis' ends ("" or null: from the data)
    property var yMin: null
    property var yMax: null
    // how values are written: -1 decimals is "as few as it takes"
    property int decimals: -1
    property string prefix: ""
    property string suffix: ""
    property color backColor: "#ffffff"
    property color plotColor: "#c0c0c0"
    property color textColor: "#000000"
    property string fontFamily: "Liberation Sans"
    property int fontSize: 11
    // what's shown when there's nothing to draw
    property string emptyText: "No data"
    // hovering a bar, point or slice shows its value
    property bool tips: true
    // the numbers and labels along the axes (false: a small picture of a graph)
    property bool axisLabels: true

    // (stock graphs) lines over the prices, and panes under them
    property var overlays: []
    property var studies: []
    // Classic (white up, black down) or "Green and red"; upColor/downColor: your own
    property string candleColors: "Classic"
    property string upColor: ""
    property string downColor: ""
    // a line at the last close, and its price on the axis
    property bool lastPrice: true
    // the latest so many bars (0: all of them)
    property int showLast: 0
    // the wheel zooms, dragging scrolls back
    property bool zoomable: true
    // tick() and addBar() keep this many bars (0: all of them)
    property int maxBars: 0

    signal clicked(var item)

    readonly property var palettes: ({
        "Windows": ["#9999ff", "#993366", "#ffffcc", "#ccffff", "#660066", "#ff8080", "#0066cc", "#ccccff",
                    "#000080", "#ff00ff", "#ffff00", "#00ffff", "#800080", "#800000", "#008080", "#0000ff"],
        "Bright": ["#000080", "#ff0000", "#008000", "#ffff00", "#800080", "#00ffff", "#800000", "#0000ff",
                   "#808000", "#ff00ff", "#00ff00", "#008080", "#808080", "#ff8000"],
        "Pastel": ["#a6cee3", "#b2df8a", "#fb9a99", "#fdbf6f", "#cab2d6", "#ffff99", "#8dd3c7", "#bebada",
                   "#fccde5", "#d9d9d9", "#ccebc5", "#ffed6f"],
        "Grays": ["#404040", "#808080", "#ffffff", "#a0a0a0", "#202020", "#e0e0e0", "#606060", "#c0c0c0"]
    })
    readonly property var types: ["Column", "Bar", "Line", "Area", "Pie", "Doughnut", "Scatter",
                                  "Candlestick", "Heikin-Ashi", "OHLC", "HLC", "Price"]
    readonly property var stockTypes: ["Candlestick", "Heikin-Ashi", "OHLC", "HLC", "Price"]
    readonly property bool isStock: stockTypes.indexOf(type) >= 0

    function colorOf(index, own) {
        if (own)
            return own;
        var p = palettes[palette] || palettes.Windows;
        return p[index % p.length];
    }

    // a value as it's written on the graph: 1,234.5 (places: so many, whatever decimals says)
    function format(v, places) {
        if (v === null || v === undefined || !isFinite(v))
            return "";
        var exact = places !== undefined || decimals >= 0;
        var d = places !== undefined ? places : decimals >= 0 ? decimals
              : (Math.abs(v - Math.round(v)) < 1e-9 ? 0 : Math.abs(v) < 10 ? 2 : 1);
        var s = Math.abs(v).toFixed(d), parts = s.split(".");
        parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ",");
        if (d > 0 && !exact)
            parts[1] = parts[1].replace(/0+$/, "");
        s = parts[1] ? parts[0] + "." + parts[1] : parts[0];
        return (v < 0 ? "-" : "") + prefix + s + suffix;
    }

    // what's at a point of the graph (the last drawing's regions)
    property var regions: []
    function itemAt(x, y) {
        for (var i = regions.length - 1; i >= 0; --i) {
            var r = regions[i];
            if (r.kind === "rect") {
                if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h)
                    return r.item;
            } else if (r.kind === "point") {
                if ((x - r.x) * (x - r.x) + (y - r.y) * (y - r.y) <= r.r * r.r)
                    return r.item;
            } else if (r.kind === "slice") {
                var dx = x - r.cx, dy = y - r.cy, d = Math.sqrt(dx * dx + dy * dy);
                if (d < r.r0 || d > r.r1)
                    continue;
                var a = Math.atan2(dy, dx);
                while (a < r.a0)
                    a += 2 * Math.PI;
                if (a <= r.a1)
                    return r.item;
            }
        }
        return null;
    }

    // --- what to draw ------------------------------------------------------

    readonly property bool isPie: type === "Pie" || type === "Doughnut"
    // the series as numbers ({ name, values, color }); missing values are null
    readonly property var drawn: {
        var out = [];
        var list = series || [];
        if (list.length > 0 && typeof list[0] === "number")
            list = [{ name: "", values: list }];
        var n = (labels || []).length;
        function numbers(from) {
            var vals = [];
            for (var i = 0; i < n; ++i) {
                var v = from ? from[i] : undefined;
                v = v === null || v === undefined || v === "" ? null : Number(v);
                vals.push(v !== null && isFinite(v) ? v : null);
            }
            return vals;
        }
        for (var s = 0; s < list.length; ++s) {
            var one = list[s] || {}, ohlc = null;
            // (a stock series: its closes are its values)
            if (one.close || one.open || one.high || one.low) {
                ohlc = { open: one.open ? numbers(one.open) : null, high: one.high ? numbers(one.high) : null,
                         low: one.low ? numbers(one.low) : null, volume: one.volume ? numbers(one.volume) : null };
            }
            var vals = numbers(ohlc ? one.close : one.values);
            out.push({ name: one.name !== undefined ? String(one.name) : "Series " + (s + 1), values: vals, color: one.color || "", ohlc: ohlc });
        }
        return out;
    }
    readonly property string drawKey: JSON.stringify([type, labels, drawn, title, legend, xTitle, yTitle, gridLines, showValues,
                                                      stacked, markers, smooth, palette, yMin, yMax, decimals, prefix, suffix,
                                                      String(backColor), String(plotColor), String(textColor), fontFamily,
                                                      fontSize, emptyText, axisLabels, width, height,
                                                      overlays, studies, candleColors, upColor, downColor, lastPrice, showLast,
                                                      zoomCount, Math.round(panBack)])
    onDrawKeyChanged: canvas.requestPaint()

    // round steps for an axis from lo to hi: { lo, hi, step }
    function niceAxis(lo, hi, fixedLo, fixedHi) {
        if (lo === hi) {
            hi = lo + 1;
            if (lo > 0 && fixedLo === null)
                lo = 0;
        }
        var raw = (hi - lo) / 5, mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), f = raw / mag;
        var step = (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * mag;
        return { lo: fixedLo !== null ? fixedLo : Math.floor(lo / step + 1e-9) * step,
                 hi: fixedHi !== null ? fixedHi : Math.ceil(hi / step - 1e-9) * step, step: step };
    }
    function fixed(v) {
        if (v === null || v === undefined || v === "")
            return null;
        var n = Number(v);
        return isFinite(n) ? n : null;
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: graph.paint(getContext("2d"))
    }

    function paint(ctx) {
        var W = width, H = height, regs = [];
        ctx.reset();
        ctx.fillStyle = String(backColor);
        ctx.fillRect(0, 0, W, H);
        var font = fontSize + "px '" + fontFamily + "'";
        var bold = "bold " + (fontSize + 2) + "px '" + fontFamily + "'";
        ctx.font = font;
        ctx.textBaseline = "middle";
        var ink = String(textColor), lineH = fontSize + 4;
        function text(t, x, y, align) {
            ctx.fillStyle = ink;
            ctx.textAlign = align || "left";
            ctx.fillText(t, x, y);
        }
        function textWidth(t) { return ctx.measureText(String(t)).width; }

        var top = 6, left = 6, right = W - 6, bottom = H - 6;
        if (title !== "") {
            ctx.font = bold;
            text(title, W / 2, top + lineH / 2 + 1, "center");
            ctx.font = font;
            top += lineH + 6;
        }

        // the legend: the series, or (a pie) the labels
        var entries = [];
        if (isPie)
            entries = (labels || []).map(function (l, i) { return { name: String(l), color: colorOf(i, "") }; });
        else if (isStock)
            entries = stockLegend();
        else
            entries = drawn.map(function (s, i) { return { name: s.name, color: colorOf(i, s.color) }; });
        if (legend !== "None" && entries.length > 0 && (isPie || entries.length > 1 || entries[0].name !== "")) {
            var box = 8, pad = 6;
            if (legend === "Right") {
                var lw = 0;
                entries.forEach(function (e) { lw = Math.max(lw, textWidth(e.name)); });
                lw = Math.min(lw + box + pad * 3, W / 3);
                var lh = entries.length * lineH + pad * 2;
                var lx = right - lw, ly = Math.max(top, (top + bottom - lh) / 2);
                ctx.fillStyle = String(backColor); ctx.fillRect(lx, ly, lw, lh);
                ctx.strokeStyle = "#000000"; ctx.lineWidth = 1; ctx.strokeRect(lx + 0.5, ly + 0.5, lw - 1, lh - 1);
                entries.forEach(function (e, i) {
                    var y = ly + pad + i * lineH + lineH / 2;
                    ctx.fillStyle = e.color; ctx.fillRect(lx + pad, y - box / 2, box, box);
                    ctx.strokeRect(lx + pad + 0.5, y - box / 2 + 0.5, box - 1, box - 1);
                    ctx.save();
                    ctx.beginPath(); ctx.rect(lx, ly, lw - pad, lh); ctx.clip();
                    text(e.name, lx + pad * 2 + box, y);
                    ctx.restore();
                });
                right = lx - 8;
            } else {
                var widths = entries.map(function (e) { return textWidth(e.name) + box + pad * 3; });
                var total = widths.reduce(function (a, b) { return a + b; }, 0);
                var lx2 = Math.max(left, (W - total) / 2), lh2 = lineH + pad;
                var ly2 = legend === "Top" ? top : bottom - lh2;
                ctx.strokeStyle = "#000000"; ctx.lineWidth = 1;
                ctx.strokeRect(lx2 + 0.5, ly2 + 0.5, Math.min(total, W - 12) - 1, lh2 - 1);
                var x = lx2;
                entries.forEach(function (e, i) {
                    var y = ly2 + lh2 / 2;
                    ctx.fillStyle = e.color; ctx.fillRect(x + pad, y - box / 2, box, box);
                    ctx.strokeRect(x + pad + 0.5, y - box / 2 + 0.5, box - 1, box - 1);
                    text(e.name, x + pad * 2 + box, y);
                    x += widths[i];
                });
                if (legend === "Top")
                    top += lh2 + 6;
                else
                    bottom -= lh2 + 6;
            }
        }

        var n = (labels || []).length;
        var hasValue = drawn.some(function (s) { return s.values.some(function (v) { return v !== null; }); });
        if (n === 0 || !hasValue) {
            text(emptyText, (left + right) / 2, (top + bottom) / 2, "center");
            regions = regs;
            return;
        }

        if (isPie) {
            paintPie(ctx, left, top, right, bottom, regs, text);
            regions = regs;
            return;
        }
        if (isStock) {
            paintStock(ctx, left, top, right, bottom, regs, text, textWidth, lineH);
            regions = regs;
            return;
        }

        // --- the axes ---
        var horizontal = type === "Bar", scatter = type === "Scatter";
        var stack = stacked && (type === "Column" || type === "Bar" || type === "Area");
        var lo = Infinity, hi = -Infinity;
        for (var i = 0; i < n; ++i) {
            var pos = 0, neg = 0;
            for (var s = 0; s < drawn.length; ++s) {
                var v = drawn[s].values[i];
                if (v === null)
                    continue;
                if (stack) {
                    if (v >= 0) pos += v; else neg += v;
                } else {
                    lo = Math.min(lo, v);
                    hi = Math.max(hi, v);
                }
            }
            if (stack) {
                lo = Math.min(lo, neg);
                hi = Math.max(hi, pos);
            }
        }
        if (type !== "Line" && type !== "Scatter") {
            lo = Math.min(lo, 0);
            hi = Math.max(hi, 0);
        }
        var axis = niceAxis(lo, hi, fixed(yMin), fixed(yMax));
        var ticks = [];
        for (var t = axis.lo; t <= axis.hi + axis.step / 1000; t += axis.step)
            ticks.push(Math.abs(t) < axis.step / 1e6 ? 0 : t);

        // a scatter's x axis is numbers too
        var xs = [], xAxis = null;
        if (scatter) {
            for (var k = 0; k < n; ++k) {
                var xv = Number(labels[k]);
                xs.push(isFinite(xv) && String(labels[k]).trim() !== "" ? xv : k + 1);
            }
            xAxis = niceAxis(Math.min.apply(null, xs), Math.max.apply(null, xs), null, null);
        }

        var tickW = 0;
        ticks.forEach(function (tv) { tickW = Math.max(tickW, textWidth(format(tv))); });
        var catW = 0;
        if (horizontal)
            (labels || []).forEach(function (l) { catW = Math.max(catW, textWidth(String(l))); });

        var pl = left + (yTitle !== "" ? lineH + 2 : 0) + (horizontal ? Math.min(catW, W / 3) : tickW) + 6;
        var pb = bottom - lineH - 2 - (xTitle !== "" ? lineH + 2 : 0);
        var pt = top + (horizontal ? 0 : lineH / 2), pr = right - (horizontal ? textWidth(format(axis.hi)) / 2 : 4);
        if (!axisLabels) {
            pl = left;
            pb = bottom;
            pt = top;
            pr = right;
        }
        if (pr - pl < 20 || pb - pt < 20) {
            regions = regs;
            return;
        }
        if (xTitle !== "")
            text(xTitle, (pl + pr) / 2, bottom - lineH / 2, "center");
        if (yTitle !== "") {
            ctx.save();
            ctx.translate(left + lineH / 2, (pt + pb) / 2);
            ctx.rotate(-Math.PI / 2);
            text(yTitle, 0, 0, "center");
            ctx.restore();
        }

        ctx.fillStyle = String(plotColor);
        ctx.fillRect(pl, pt, pr - pl, pb - pt);
        // value -> pixels along the value axis
        function vpos(val) {
            var f = (val - axis.lo) / (axis.hi - axis.lo);
            return horizontal ? pl + f * (pr - pl) : pb - f * (pb - pt);
        }
        function xpos(val) { return pl + (val - xAxis.lo) / (xAxis.hi - xAxis.lo) * (pr - pl); }

        ctx.strokeStyle = "#000000";
        ctx.lineWidth = 1;
        ticks.forEach(function (tv) {
            var p = Math.round(vpos(tv)) + 0.5;
            if (gridLines && tv !== axis.lo) {
                ctx.beginPath();
                if (horizontal) { ctx.moveTo(p, pt); ctx.lineTo(p, pb); } else { ctx.moveTo(pl, p); ctx.lineTo(pr, p); }
                ctx.stroke();
            }
            if (!axisLabels)
                return;
            if (horizontal)
                text(format(tv), p, pb + lineH / 2 + 2, "center");
            else
                text(format(tv), pl - 4, p, "right");
        });
        // the category axis: a label under (or beside) each, as many as fit
        var slot = (horizontal ? pb - pt : pr - pl) / n;
        if (scatter) {
            for (var xt = xAxis.lo; xt <= xAxis.hi + xAxis.step / 1000; xt += xAxis.step) {
                var px = Math.round(xpos(xt)) + 0.5;
                if (gridLines && xt !== xAxis.lo) {
                    ctx.beginPath(); ctx.moveTo(px, pt); ctx.lineTo(px, pb); ctx.stroke();
                }
                if (axisLabels)
                    text(format(xt), px, pb + lineH / 2 + 2, "center");
            }
        } else {
            var every = 1;
            if (!horizontal) {
                var widest = 0;
                (labels || []).forEach(function (l) { widest = Math.max(widest, textWidth(String(l))); });
                every = Math.max(1, Math.ceil((widest + 6) / slot));
            } else {
                every = Math.max(1, Math.ceil(lineH / slot));
            }
            for (var c = 0; c < n && axisLabels; c += every) {
                var cm = (horizontal ? pt : pl) + slot * (c + 0.5);
                ctx.save();
                if (horizontal) {
                    ctx.beginPath(); ctx.rect(left, pt, pl - left - 4, pb - pt); ctx.clip();
                    text(String(labels[c]), pl - 6, cm, "right");
                } else {
                    text(String(labels[c]), cm, pb + lineH / 2 + 2, "center");
                }
                ctx.restore();
            }
        }
        ctx.strokeStyle = "#000000";
        ctx.strokeRect(Math.round(pl) + 0.5, Math.round(pt) + 0.5, Math.round(pr - pl) - 1, Math.round(pb - pt) - 1);
        var zero = Math.round(vpos(Math.max(axis.lo, Math.min(axis.hi, 0)))) + 0.5;

        function item(s, i) {
            return { series: s, index: i, label: labels[i], value: drawn[s].values[i], name: drawn[s].name };
        }
        ctx.save();
        ctx.beginPath(); ctx.rect(pl, pt, pr - pl, pb - pt); ctx.clip();
        var valueTexts = [];

        if (type === "Column" || type === "Bar") {
            var groupW = slot * 0.7, count = stack ? 1 : drawn.length, barW = groupW / count;
            var posBase = [], negBase = [];
            for (var b = 0; b < n; ++b) { posBase.push(0); negBase.push(0); }
            for (var s2 = 0; s2 < drawn.length; ++s2) {
                for (var i2 = 0; i2 < n; ++i2) {
                    var val = drawn[s2].values[i2];
                    if (val === null)
                        continue;
                    var from = 0, to = val;
                    if (stack) {
                        from = val >= 0 ? posBase[i2] : negBase[i2];
                        to = from + val;
                        if (val >= 0) posBase[i2] = to; else negBase[i2] = to;
                    }
                    var a = vpos(from), z = vpos(to);
                    var off = (horizontal ? pt : pl) + slot * i2 + (slot - groupW) / 2 + (stack ? 0 : barW * s2);
                    var r = horizontal ? { x: Math.min(a, z), y: off, w: Math.abs(z - a), h: barW }
                                       : { x: off, y: Math.min(a, z), w: barW, h: Math.abs(z - a) };
                    ctx.fillStyle = colorOf(s2, drawn[s2].color);
                    ctx.fillRect(r.x, r.y, r.w, r.h);
                    ctx.strokeStyle = "#000000";
                    ctx.strokeRect(Math.round(r.x) + 0.5, Math.round(r.y) + 0.5, Math.max(0, Math.round(r.w) - 1), Math.max(0, Math.round(r.h) - 1));
                    regs.push({ kind: "rect", x: r.x, y: r.y, w: Math.max(r.w, 2), h: Math.max(r.h, 2), item: item(s2, i2) });
                    if (showValues)
                        valueTexts.push(horizontal ? { t: format(val), x: Math.max(a, z) + 3, y: r.y + r.h / 2, align: "left" }
                                                   : { t: format(val), x: r.x + r.w / 2, y: Math.min(a, z) - lineH / 2, align: "center" });
                }
            }
        } else {
            // Line, Area, Scatter: a point for each value
            var base = [];
            for (var b2 = 0; b2 < n; ++b2) base.push(0);
            var order = [];
            for (var s3 = 0; s3 < drawn.length; ++s3) order.push(s3);
            var shapes = [];
            for (var oi = 0; oi < order.length; ++oi) {
                var si = order[oi], pts = [];
                for (var i3 = 0; i3 < n; ++i3) {
                    var vv = drawn[si].values[i3];
                    if (vv === null) {
                        pts.push(null);
                        continue;
                    }
                    var lowV = stack ? base[i3] : 0, highV = stack ? base[i3] + vv : vv;
                    if (stack)
                        base[i3] = highV;
                    pts.push({ x: scatter ? xpos(xs[i3]) : pl + slot * (i3 + 0.5), y: vpos(highV), y0: vpos(lowV), v: vv, i: i3 });
                }
                shapes.push({ s: si, pts: pts });
            }
            function path(pts, close) {
                var started = false, prev = null;
                for (var p = 0; p < pts.length; ++p) {
                    var q = pts[p];
                    if (!q) {
                        started = started && close;
                        continue;
                    }
                    if (!started) {
                        ctx.moveTo(q.x, q.y);
                        started = true;
                    } else if (smooth && prev) {
                        var mx = (prev.x + q.x) / 2;
                        ctx.bezierCurveTo(mx, prev.y, mx, q.y, q.x, q.y);
                    } else {
                        ctx.lineTo(q.x, q.y);
                    }
                    prev = q;
                }
            }
            if (type === "Area") {
                // (stacked: the top one first, so each shows above the one under it)
                for (var a2 = shapes.length - 1; a2 >= 0; --a2) {
                    var ps = shapes[a2].pts.filter(function (q) { return q !== null; });
                    if (ps.length === 0)
                        continue;
                    ctx.beginPath();
                    ctx.moveTo(ps[0].x, stack ? ps[0].y0 : zero);
                    ctx.lineTo(ps[0].x, ps[0].y);
                    path(ps, true);
                    for (var rr = ps.length - 1; rr >= 0; --rr)
                        ctx.lineTo(ps[rr].x, stack ? ps[rr].y0 : zero);
                    ctx.closePath();
                    ctx.fillStyle = colorOf(shapes[a2].s, drawn[shapes[a2].s].color);
                    ctx.fill();
                    ctx.strokeStyle = "#000000";
                    ctx.lineWidth = 1;
                    ctx.stroke();
                }
            }
            shapes.forEach(function (sh) {
                var col = colorOf(sh.s, drawn[sh.s].color);
                if (type === "Line") {
                    ctx.beginPath();
                    path(sh.pts, false);
                    ctx.strokeStyle = col;
                    ctx.lineWidth = 2;
                    ctx.stroke();
                    ctx.lineWidth = 1;
                }
                sh.pts.forEach(function (q) {
                    if (!q)
                        return;
                    if (type === "Scatter" || (type === "Line" && markers)) {
                        ctx.fillStyle = col;
                        ctx.fillRect(q.x - 3, q.y - 3, 7, 7);
                        ctx.strokeStyle = "#000000";
                        ctx.strokeRect(Math.round(q.x - 3) + 0.5, Math.round(q.y - 3) + 0.5, 6, 6);
                    }
                    regs.push({ kind: "point", x: q.x, y: q.y, r: 6, item: item(sh.s, q.i) });
                    if (showValues)
                        valueTexts.push({ t: format(q.v), x: q.x, y: q.y - lineH / 2 - 2, align: "center" });
                });
            });
        }
        // (the zero line, when the axis crosses it)
        if (axis.lo < 0 && axis.hi > 0 && !scatter) {
            ctx.strokeStyle = "#000000";
            ctx.beginPath();
            if (horizontal) { ctx.moveTo(zero, pt); ctx.lineTo(zero, pb); } else { ctx.moveTo(pl, zero); ctx.lineTo(pr, zero); }
            ctx.stroke();
        }
        ctx.restore();
        valueTexts.forEach(function (v) { text(v.t, v.x, v.y, v.align); });
        regions = regs;
    }

    function paintPie(ctx, left, top, right, bottom, regs, text) {
        var vals = drawn.length ? drawn[0].values : [];
        var total = 0;
        vals.forEach(function (v) { if (v !== null && v > 0) total += v; });
        if (total <= 0) {
            text(emptyText, (left + right) / 2, (top + bottom) / 2, "center");
            return;
        }
        var cx = (left + right) / 2, cy = (top + bottom) / 2;
        var r = Math.max(10, Math.min(right - left, bottom - top) / 2 - (showValues ? fontSize + 8 : 4));
        var r0 = type === "Doughnut" ? r * 0.5 : 0;
        var a = -Math.PI / 2;
        for (var i = 0; i < vals.length; ++i) {
            var v = vals[i];
            if (v === null || v <= 0)
                continue;
            var sweep = v / total * 2 * Math.PI, a1 = a + sweep;
            ctx.beginPath();
            if (r0 > 0) {
                ctx.arc(cx, cy, r, a, a1, false);
                ctx.arc(cx, cy, r0, a1, a, true);
            } else {
                ctx.moveTo(cx, cy);
                ctx.arc(cx, cy, r, a, a1, false);
            }
            ctx.closePath();
            ctx.fillStyle = colorOf(i, "");
            ctx.fill();
            ctx.strokeStyle = "#000000";
            ctx.lineWidth = 1;
            ctx.stroke();
            regs.push({ kind: "slice", cx: cx, cy: cy, r0: r0, r1: r, a0: a, a1: a1,
                        item: { series: 0, index: i, label: labels[i], value: v, name: drawn[0].name } });
            if (showValues) {
                var mid = (a + a1) / 2, tr = r + fontSize / 2 + 6;
                text(Math.round(v / total * 100) + "%", cx + Math.cos(mid) * tr, cy + Math.sin(mid) * tr, "center");
            }
            a = a1;
        }
    }

    // --- stock graphs ------------------------------------------------------

    // the colors of a bar that went up, or down, and what's drawn round them
    readonly property var candle: {
        var classic = candleColors !== "Green and red";
        return { up: upColor !== "" ? upColor : classic ? "#ffffff" : "#008000",
                 down: downColor !== "" ? downColor : classic ? "#000000" : "#ff0000",
                 edge: classic ? "#000000" : "" };
    }
    readonly property var overlayColors: ["#000080", "#800080", "#008080", "#808000", "#800000", "#0000ff"]

    // "SMA 20" or { kind: "SMA", period: 20, color } -> { kind, args, label, color, over } (null: not one)
    readonly property var indicatorKinds: ({
        SMA: [20], EMA: [20], Bollinger: [20, 2], VWAP: [], Volume: [], RSI: [14], MACD: [12, 26, 9], Stochastic: [14, 3]
    })
    function parseIndicator(spec) {
        var kind = "", args = [], color = "";
        if (spec && typeof spec === "object") {
            kind = String(spec.kind || "");
            args = spec.args ? spec.args.slice() : spec.period !== undefined ? [spec.period] : [];
            color = spec.color || "";
        } else {
            var words = String(spec || "").trim().split(/[\s,]+/);
            kind = words.shift() || "";
            args = words.filter(function (w) { return w !== "" && isFinite(Number(w)); });
        }
        var aliases = { ma: "SMA", sma: "SMA", ema: "EMA", bb: "Bollinger", bollinger: "Bollinger", vwap: "VWAP",
                        vol: "Volume", volume: "Volume", rsi: "RSI", macd: "MACD", stoch: "Stochastic", stochastic: "Stochastic" };
        kind = aliases[kind.toLowerCase()] || "";
        if (kind === "")
            return null;
        var out = indicatorKinds[kind].map(function (d, i) {
            var v = Number(args[i]);
            if (args[i] === undefined || args[i] === "" || !isFinite(v) || v <= 0)
                return d;
            // (periods are whole; a Bollinger's width needn't be)
            return kind === "Bollinger" && i === 1 ? v : Math.max(1, Math.round(v));
        });
        return { kind: kind, args: out, label: [kind].concat(out).join(" "), color: color,
                 over: kind === "SMA" || kind === "EMA" || kind === "Bollinger" || kind === "VWAP" };
    }

    // the price series' bars: { index (its series), bars: [{ o, h, l, c, v } or null], hasVolume }
    // (a series of just values: each opens where the one before closed)
    readonly property var priced: {
        var si = -1, list = drawn;
        for (var s = 0; s < list.length && si < 0; ++s)
            if (list[s].ohlc)
                si = s;
        if (si < 0 && list.length > 0)
            si = 0;
        var bars = [], prev = null, hasVolume = false;
        if (si >= 0) {
            var d = list[si], o = d.ohlc || {};
            for (var i = 0; i < d.values.length; ++i) {
                var c = d.values[i];
                if (c === null) {
                    bars.push(null);
                    continue;
                }
                var op = o.open && o.open[i] !== null ? o.open[i] : prev !== null ? prev : c;
                var hi = Math.max(o.high && o.high[i] !== null ? o.high[i] : -Infinity, op, c);
                var lo = Math.min(o.low && o.low[i] !== null ? o.low[i] : Infinity, op, c);
                var v = o.volume ? o.volume[i] : null;
                hasVolume = hasVolume || v !== null;
                bars.push({ o: op, h: hi, l: lo, c: c, v: v });
                prev = c;
            }
        }
        return { index: si, bars: bars, hasVolume: hasVolume };
    }

    // Heikin-Ashi bars: each the average of its own and the one before
    function heikinAshi(bars) {
        var out = [], prev = null;
        bars.forEach(function (b) {
            if (!b) {
                out.push(null);
                return;
            }
            var c = (b.o + b.h + b.l + b.c) / 4, o = prev ? (prev.o + prev.c) / 2 : (b.o + b.c) / 2;
            prev = { o: o, h: Math.max(b.h, o, c), l: Math.min(b.l, o, c), c: c, v: b.v };
            out.push(prev);
        });
        return out;
    }

    // --- what an indicator works out to ---
    function sma(a, n) {
        var out = [];
        for (var i = 0; i < a.length; ++i) {
            var sum = 0, ok = i >= n - 1;
            for (var j = i - n + 1; ok && j <= i; ++j) {
                if (a[j] === null)
                    ok = false;
                else
                    sum += a[j];
            }
            out.push(ok ? sum / n : null);
        }
        return out;
    }
    // (starts with the average of the first n)
    function ema(a, n) {
        var out = [], k = 2 / (n + 1), prev = null, run = 0, sum = 0;
        for (var i = 0; i < a.length; ++i) {
            if (a[i] === null) {
                out.push(null);
                run = 0;
                sum = 0;
                prev = null;
                continue;
            }
            if (prev === null) {
                run += 1;
                sum += a[i];
                if (run === n)
                    prev = sum / n;
                out.push(prev);
            } else {
                prev = prev + k * (a[i] - prev);
                out.push(prev);
            }
        }
        return out;
    }
    // (Wilder's)
    function rsi(a, n) {
        var out = a.map(function () { return null; });
        if (a.length <= n)
            return out;
        var gain = 0, loss = 0;
        for (var i = 1; i <= n; ++i) {
            var d = a[i] - a[i - 1];
            if (d > 0) gain += d; else loss -= d;
        }
        gain /= n;
        loss /= n;
        function value() { return loss === 0 ? (gain === 0 ? 50 : 100) : 100 - 100 / (1 + gain / loss); }
        out[n] = value();
        for (var j = n + 1; j < a.length; ++j) {
            var dd = a[j] - a[j - 1];
            gain = (gain * (n - 1) + Math.max(dd, 0)) / n;
            loss = (loss * (n - 1) + Math.max(-dd, 0)) / n;
            out[j] = value();
        }
        return out;
    }

    // ind (parseIndicator's) of these bars: { lines: [{ key, values, color }], histogram, range, guides }
    function computeIndicator(ind, bars) {
        // (worked out on the bars there are, then put back where they go)
        var at = [], i;
        for (i = 0; i < bars.length; ++i)
            if (bars[i])
                at.push(i);
        var C = at.map(function (k) { return bars[k].c; });
        function back(list) {
            var out = bars.map(function () { return null; });
            list.forEach(function (v, j) { out[at[j]] = v === null || !isFinite(v) ? null : v; });
            return out;
        }
        var a = ind.args, r = { lines: [], histogram: null, range: null, guides: [] };
        switch (ind.kind) {
        case "SMA":
        case "EMA":
            r.lines = [{ key: ind.kind, values: back(ind.kind === "SMA" ? sma(C, a[0]) : ema(C, a[0])) }];
            break;
        case "Bollinger":
            var mid = sma(C, a[0]), upper = [], lower = [];
            for (i = 0; i < C.length; ++i) {
                if (mid[i] === null) {
                    upper.push(null);
                    lower.push(null);
                    continue;
                }
                var sq = 0;
                for (var j = i - a[0] + 1; j <= i; ++j)
                    sq += (C[j] - mid[i]) * (C[j] - mid[i]);
                var sd = Math.sqrt(sq / a[0]);
                upper.push(mid[i] + a[1] * sd);
                lower.push(mid[i] - a[1] * sd);
            }
            r.lines = [{ key: "upper", values: back(upper) }, { key: "middle", values: back(mid) }, { key: "lower", values: back(lower) }];
            break;
        case "VWAP":
            var pv = 0, vol = 0;
            r.lines = [{ key: "VWAP", values: back(at.map(function (k) {
                var b = bars[k];
                if (b.v === null || b.v === undefined)
                    return null;
                pv += (b.h + b.l + b.c) / 3 * b.v;
                vol += b.v;
                return vol > 0 ? pv / vol : null;
            })) }];
            break;
        case "Volume":
            r.histogram = back(at.map(function (k) { return bars[k].v; }));
            r.range = [0, null];
            break;
        case "RSI":
            r.lines = [{ key: "RSI", values: back(rsi(C, a[0])) }];
            r.range = [0, 100];
            r.guides = [30, 70];
            break;
        case "MACD":
            var fast = ema(C, a[0]), slow = ema(C, a[1]);
            var line = C.map(function (c, k) { return fast[k] === null || slow[k] === null ? null : fast[k] - slow[k]; });
            var signal = ema(line, a[2]);
            r.lines = [{ key: "MACD", values: back(line) }, { key: "signal", values: back(signal) }];
            r.histogram = back(line.map(function (m, k) { return m === null || signal[k] === null ? null : m - signal[k]; }));
            r.guides = [0];
            break;
        case "Stochastic":
            var K = [];
            for (i = 0; i < C.length; ++i) {
                if (i < a[0] - 1) {
                    K.push(null);
                    continue;
                }
                var hh = -Infinity, ll = Infinity;
                for (var m = i - a[0] + 1; m <= i; ++m) {
                    hh = Math.max(hh, bars[at[m]].h);
                    ll = Math.min(ll, bars[at[m]].l);
                }
                K.push(hh === ll ? 50 : (C[i] - ll) / (hh - ll) * 100);
            }
            r.lines = [{ key: "K", values: back(K) }, { key: "D", values: back(sma(K, a[1])) }];
            r.range = [0, 100];
            r.guides = [20, 80];
            break;
        }
        return r;
    }

    // what an indicator ("SMA 20", "RSI 14", ...) works out to, a value for each label: one
    // list (SMA, EMA, VWAP, RSI, Volume), or { upper, middle, lower } (Bollinger),
    // { MACD, signal, histogram } (MACD) or { K, D } (Stochastic)
    function indicator(spec) {
        var ind = parseIndicator(spec);
        if (!ind)
            return null;
        var r = computeIndicator(ind, priced.bars);
        if (ind.kind === "Volume")
            return r.histogram;
        if (r.lines.length === 1 && !r.histogram)
            return r.lines[0].values;
        var out = {};
        r.lines.forEach(function (l) { out[l.key] = l.values; });
        if (r.histogram)
            out.histogram = r.histogram;
        return out;
    }

    function stockLegend() {
        var out = [];
        if (priced.index >= 0)
            out.push({ name: drawn[priced.index].name, color: type === "Price" ? (drawn[priced.index].color || "#000080") : candle.up });
        (overlays || []).forEach(function (spec, k) {
            var ind = parseIndicator(spec);
            if (ind && ind.over)
                out.push({ name: ind.label, color: ind.color || overlayColors[k % overlayColors.length] });
        });
        drawn.forEach(function (d, s) {
            if (s !== priced.index)
                out.push({ name: d.name, color: colorOf(s, d.color) });
        });
        return out;
    }

    // a price as it's written: 2 places (4 under 1), unless decimals says
    function priceText(v) {
        if (decimals >= 0)
            return format(v);
        return format(v, Math.abs(v) >= 1 || v === 0 ? 2 : 4);
    }
    // 1.2K, 3.4M
    function shortText(v) {
        var a = Math.abs(v);
        if (a >= 1e9) return (v / 1e9).toFixed(1).replace(/\.0$/, "") + "B";
        if (a >= 1e6) return (v / 1e6).toFixed(1).replace(/\.0$/, "") + "M";
        if (a >= 1e3) return (v / 1e3).toFixed(1).replace(/\.0$/, "") + "K";
        return format(v);
    }
    function niceStep(range, count) {
        if (!(range > 0))
            return 1;
        var raw = range / Math.max(1, count), mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10)), f = raw / mag;
        return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 2.5 ? 2.5 : f <= 5 ? 5 : 10) * mag;
    }

    // which bars are in view: the wheel's and dragging's, or showLast's
    property int zoomCount: 0
    property real panBack: 0
    property int seenBars: 0
    onLabelsChanged: {
        // (scrolled back: new bars don't move what's in view)
        var n = (labels || []).length;
        if (n < seenBars)
            panBack = 0;
        else if (panBack > 0)
            panBack += n - seenBars;
        seenBars = n;
    }
    function viewRange(n) {
        var count = zoomCount > 0 ? zoomCount : showLast > 0 ? showLast : n;
        count = Math.max(1, Math.min(n, count));
        var back = Math.max(0, Math.min(n - count, Math.round(panBack)));
        return { start: n - back - count, end: n - back, count: count };
    }
    // the last drawing's panes, for the crosshair: { pl, pr, slot, start, end, panes: [{ top, bottom, lo, hi }] }
    property var stockView: null

    function paintStock(ctx, left, top, right, bottom, regs, text, textWidth, lineH) {
        var n = labels.length, P = priced, bars = type === "Heikin-Ashi" ? heikinAshi(P.bars) : P.bars;
        var view = viewRange(n), start = view.start, end = view.end, count = view.count;
        stockView = null;

        // the lines over the prices, the panes under them
        var overs = [], unders = [];
        (overlays || []).forEach(function (spec, k) {
            var ind = parseIndicator(spec);
            if (ind && ind.over)
                overs.push({ ind: ind, color: ind.color || overlayColors[k % overlayColors.length], r: computeIndicator(ind, P.bars) });
        });
        if (axisLabels) {
            (studies || []).forEach(function (spec) {
                var ind = parseIndicator(spec);
                if (ind && !ind.over && (ind.kind !== "Volume" || P.hasVolume))
                    unders.push({ ind: ind, r: computeIndicator(ind, P.bars) });
            });
        }
        var extras = [];
        drawn.forEach(function (d, s) {
            if (s !== P.index)
                extras.push({ values: d.values, color: colorOf(s, d.color) });
        });

        // the prices' axis: what's in view
        var lo = Infinity, hi = -Infinity;
        function take(v) {
            if (v !== null && v !== undefined && isFinite(v)) {
                lo = Math.min(lo, v);
                hi = Math.max(hi, v);
            }
        }
        for (var i = start; i < end; ++i) {
            if (bars[i]) {
                take(type === "Price" ? bars[i].c : bars[i].l);
                take(type === "Price" ? bars[i].c : bars[i].h);
            }
            overs.forEach(function (o) { o.r.lines.forEach(function (l) { take(l.values[i]); }); });
            extras.forEach(function (e) { take(e.values[i]); });
        }
        if (lo === Infinity) {
            text(emptyText, (left + right) / 2, (top + bottom) / 2, "center");
            return;
        }
        var pad = (hi - lo) * 0.06 || Math.abs(hi) * 0.01 || 1;
        lo = fixed(yMin) !== null ? fixed(yMin) : lo - pad;
        hi = fixed(yMax) !== null ? fixed(yMax) : hi + pad;
        if (hi <= lo)
            hi = lo + 1;

        // the panes, top to bottom: the prices, then each study
        var pt = top + (axisLabels ? lineH / 2 : 0);
        var pb = axisLabels ? bottom - lineH - 2 - (xTitle !== "" ? lineH + 2 : 0) : bottom;
        var gap = 6, total = pb - pt;
        var studyH = unders.length ? Math.min(total * 0.24, (total * 0.5) / unders.length) : 0;
        while (unders.length > 0 && studyH < lineH * 2 + 4) {
            unders.pop();
            studyH = unders.length ? Math.min(total * 0.24, (total * 0.5) / unders.length) : 0;
        }
        var priceH = total - unders.length * (studyH + gap);
        if (priceH < 20) {
            text(emptyText, (left + right) / 2, (top + bottom) / 2, "center");
            return;
        }
        var panes = [{ top: pt, bottom: pt + priceH, lo: lo, hi: hi, price: true }];
        unders.forEach(function (u, k) {
            var t = pt + priceH + gap + k * (studyH + gap), plo = Infinity, phi = -Infinity;
            for (var j = start; j < end; ++j) {
                u.r.lines.forEach(function (l) {
                    if (l.values[j] !== null) { plo = Math.min(plo, l.values[j]); phi = Math.max(phi, l.values[j]); }
                });
                if (u.r.histogram && u.r.histogram[j] !== null) {
                    plo = Math.min(plo, u.r.histogram[j]);
                    phi = Math.max(phi, u.r.histogram[j]);
                }
            }
            if (u.r.range) {
                plo = u.r.range[0];
                phi = u.r.range[1] !== null ? u.r.range[1] : phi;
            }
            if (plo === Infinity) { plo = 0; phi = 1; }
            if (u.ind.kind === "MACD") {
                var m = Math.max(Math.abs(plo), Math.abs(phi)) || 1;
                plo = -m * 1.1;
                phi = m * 1.1;
            } else if (u.ind.kind === "Volume") {
                phi = phi > 0 ? phi * 1.08 : 1;
            }
            if (phi <= plo)
                phi = plo + 1;
            panes.push({ top: t, bottom: t + studyH, lo: plo, hi: phi, study: u });
        });

        // each pane's ticks, and how wide the axis is
        var tickH = lineH * 2.2;
        panes.forEach(function (p) {
            var list = [];
            if (p.study && p.study.r.guides.length)
                list = p.study.r.guides.slice();
            else {
                var step = niceStep(p.hi - p.lo, Math.max(1, Math.floor((p.bottom - p.top) / tickH)));
                for (var t = Math.ceil(p.lo / step - 1e-9) * step; t <= p.hi + step / 1e6; t += step)
                    list.push(Math.abs(t) < step / 1e6 ? 0 : t);
                if (p.study && p.study.ind.kind === "Volume")
                    list = list.filter(function (t) { return t > 0; }).slice(-1);
            }
            p.ticks = list;
            p.tickText = function (v) {
                if (p.price) return priceText(v);
                if (p.study.ind.kind === "Volume") return shortText(v);
                return format(v, p.study.ind.kind === "MACD" ? 2 : 0);
            };
        });
        var axisW = 0;
        if (axisLabels) {
            panes.forEach(function (p) { p.ticks.forEach(function (t) { axisW = Math.max(axisW, textWidth(p.tickText(t))); }); });
            var lastBar = null;
            for (var lb = n - 1; lb >= 0 && !lastBar; --lb)
                lastBar = P.bars[lb];
            if (lastBar)
                axisW = Math.max(axisW, textWidth(priceText(lastBar.c)));
            axisW += 10;
        }
        var pl = left + (yTitle !== "" && axisLabels ? lineH + 2 : 0);
        var pr = right - axisW;
        if (pr - pl < 20)
            return;
        var slot = (pr - pl) / count;
        function xAt(k) { return pl + slot * (k - start + 0.5); }
        function yAt(p, v) { return p.bottom - (v - p.lo) / (p.hi - p.lo) * (p.bottom - p.top); }

        if (xTitle !== "" && axisLabels)
            text(xTitle, (pl + pr) / 2, bottom - lineH / 2, "center");
        if (yTitle !== "" && axisLabels) {
            ctx.save();
            ctx.translate(left + lineH / 2, (panes[0].top + panes[0].bottom) / 2);
            ctx.rotate(-Math.PI / 2);
            text(yTitle, 0, 0, "center");
            ctx.restore();
        }

        // the last close: a dotted line across, and its price on the axis (worked out now: the
        // numbers it would cover aren't written)
        var last = -1, lastTag = null;
        for (var q = n - 1; q >= 0 && last < 0; --q)
            if (P.bars[q])
                last = q;
        if (lastPrice && axisLabels && last >= 0) {
            var lc = P.bars[last].c, prevC = null;
            for (var q2 = last - 1; q2 >= 0 && prevC === null; --q2)
                if (P.bars[q2])
                    prevC = P.bars[q2].c;
            var ly = Math.round(yAt(panes[0], lc)) + 0.5, rising = prevC === null || lc >= prevC;
            if (ly >= panes[0].top && ly <= panes[0].bottom)
                lastTag = { y: ly, text: priceText(lc), top: Math.max(panes[0].top, Math.min(panes[0].bottom - lineH, ly - lineH / 2)),
                            color: rising ? (candle.edge ? "#008000" : candle.up) : (candle.edge ? "#ff0000" : candle.down) };
        }

        // the panes: grey, their grid lines and numbers
        ctx.lineWidth = 1;
        panes.forEach(function (p) {
            ctx.fillStyle = String(plotColor);
            ctx.fillRect(pl, p.top, pr - pl, p.bottom - p.top);
            p.ticks.forEach(function (t) {
                var y = Math.round(yAt(p, t)) + 0.5;
                if (y < p.top || y > p.bottom)
                    return;
                if (gridLines || (p.study && p.study.r.guides.length)) {
                    ctx.strokeStyle = p.study && p.study.r.guides.length ? "#808080" : "#000000";
                    ctx.beginPath(); ctx.moveTo(pl, y); ctx.lineTo(pr, y); ctx.stroke();
                }
                if (axisLabels && !(p.price && lastTag && y > lastTag.top - lineH / 2 && y < lastTag.top + lineH * 1.5))
                    text(p.tickText(t), pr + 5, y, "left");
            });
        });
        // the labels across, under the last pane (in the same places as it scrolls)
        if (axisLabels) {
            var widest = 0;
            for (var w = start; w < end; ++w)
                widest = Math.max(widest, textWidth(String(labels[w])));
            var every = Math.max(1, Math.ceil((widest + 10) / slot));
            var lastY = panes[panes.length - 1].bottom;
            for (var c = Math.ceil(start / every) * every; c < end; c += every) {
                var cx = xAt(c);
                if (cx - textWidth(String(labels[c])) / 2 < left || cx + textWidth(String(labels[c])) / 2 > right)
                    continue;
                text(String(labels[c]), cx, lastY + lineH / 2 + 2, "center");
                ctx.strokeStyle = "#000000";
                ctx.beginPath(); ctx.moveTo(Math.round(cx) + 0.5, lastY); ctx.lineTo(Math.round(cx) + 0.5, lastY + 3); ctx.stroke();
            }
        }

        function line(values, p, color, width) {
            ctx.beginPath();
            var started = false;
            for (var k = Math.max(0, start - 1); k < Math.min(n, end + 1); ++k) {
                var v = values[k];
                if (v === null || v === undefined) {
                    started = false;
                    continue;
                }
                if (started) ctx.lineTo(xAt(k), yAt(p, v)); else ctx.moveTo(xAt(k), yAt(p, v));
                started = true;
            }
            ctx.strokeStyle = color;
            ctx.lineWidth = width;
            ctx.stroke();
            ctx.lineWidth = 1;
        }
        function columns(values, p, colorOfBar) {
            var base = yAt(p, Math.max(p.lo, Math.min(p.hi, 0))), bw = Math.max(1, Math.floor(slot * 0.7));
            for (var k = start; k < end; ++k) {
                var v = values[k];
                if (v === null || v === undefined)
                    continue;
                var x = Math.round(xAt(k) - bw / 2), y = yAt(p, v);
                ctx.fillStyle = colorOfBar(k, v);
                ctx.fillRect(x, Math.min(y, base), bw, Math.max(1, Math.abs(base - y)));
                if (bw >= 4) {
                    ctx.strokeStyle = "#000000";
                    ctx.strokeRect(x + 0.5, Math.round(Math.min(y, base)) + 0.5, bw - 1, Math.max(0, Math.round(Math.abs(base - y)) - 1));
                }
            }
        }
        function up(k) {
            var b = bars[k];
            return b.c >= b.o;
        }

        // --- the prices ---
        var price = panes[0];
        ctx.save();
        ctx.beginPath(); ctx.rect(pl, price.top, pr - pl, price.bottom - price.top); ctx.clip();
        overs.forEach(function (o) {
            if (o.ind.kind === "Bollinger") {
                // (the band shaded between its edges)
                var up_ = o.r.lines[0].values, low_ = o.r.lines[2].values, pts = [];
                for (var k = Math.max(0, start - 1); k < Math.min(n, end + 1); ++k)
                    if (up_[k] !== null)
                        pts.push(k);
                if (pts.length > 1) {
                    ctx.beginPath();
                    pts.forEach(function (k, j) { if (j) ctx.lineTo(xAt(k), yAt(price, up_[k])); else ctx.moveTo(xAt(k), yAt(price, up_[k])); });
                    for (var r = pts.length - 1; r >= 0; --r)
                        ctx.lineTo(xAt(pts[r]), yAt(price, low_[pts[r]]));
                    ctx.closePath();
                    ctx.fillStyle = "#d4d0c8";
                    ctx.fill();
                }
            }
        });
        if (gridLines) {
            // (the grid again, over the band)
            ctx.strokeStyle = "#000000";
            price.ticks.forEach(function (t) {
                var y = Math.round(yAt(price, t)) + 0.5;
                ctx.beginPath(); ctx.moveTo(pl, y); ctx.lineTo(pr, y); ctx.stroke();
            });
        }
        var bodyW = Math.max(1, Math.floor(slot * 0.7));
        if (bodyW >= 3 && bodyW % 2 === 0)
            bodyW -= 1;
        if (type === "Price") {
            line(bars.map(function (b) { return b ? b.c : null; }), price,
                 P.index >= 0 && drawn[P.index].color ? drawn[P.index].color : "#000080", 2);
        } else {
            for (var k = start; k < end; ++k) {
                var b = bars[k];
                if (!b)
                    continue;
                var isUp = up(k), fill = isUp ? candle.up : candle.down, edge = candle.edge || fill;
                var xc = Math.round(xAt(k)) + 0.5;
                var yh = Math.round(yAt(price, b.h)), yl = Math.round(yAt(price, b.l));
                var yo = Math.round(yAt(price, b.o)), yc = Math.round(yAt(price, b.c));
                ctx.strokeStyle = type === "Candlestick" || type === "Heikin-Ashi" ? edge : (candle.edge ? "#000000" : fill);
                ctx.beginPath(); ctx.moveTo(xc, yh); ctx.lineTo(xc, Math.max(yl, yh + 1)); ctx.stroke();
                if (type === "Candlestick" || type === "Heikin-Ashi") {
                    var bx = xc - 0.5 - Math.floor(bodyW / 2), by = Math.min(yo, yc), bh = Math.max(1, Math.abs(yo - yc));
                    ctx.fillStyle = fill;
                    ctx.fillRect(bx, by, bodyW, bh);
                    if (bodyW >= 3) {
                        ctx.strokeStyle = edge;
                        ctx.strokeRect(bx + 0.5, by + 0.5, bodyW - 1, Math.max(0, bh - 1));
                    }
                } else {
                    // OHLC: the open a tick to the left; both: the close a tick to the right
                    var tick = Math.max(2, Math.floor(slot * 0.35));
                    ctx.beginPath();
                    if (type === "OHLC") {
                        ctx.moveTo(xc - tick, yo + 0.5); ctx.lineTo(xc, yo + 0.5);
                    }
                    ctx.moveTo(xc, yc + 0.5); ctx.lineTo(xc + tick, yc + 0.5);
                    ctx.stroke();
                }
            }
        }
        overs.forEach(function (o) {
            o.r.lines.forEach(function (l) { line(l.values, price, o.color, l.key === "middle" ? 1 : o.ind.kind === "Bollinger" ? 1 : 2); });
        });
        extras.forEach(function (e) { line(e.values, price, e.color, 2); });

        if (lastTag) {
            ctx.strokeStyle = lastTag.color;
            for (var dx = pl; dx < pr; dx += 6) {
                ctx.beginPath(); ctx.moveTo(dx, lastTag.y); ctx.lineTo(Math.min(pr, dx + 3), lastTag.y); ctx.stroke();
            }
        }
        ctx.restore();

        // --- the studies ---
        panes.forEach(function (p) {
            if (!p.study)
                return;
            var u = p.study, kind = u.ind.kind;
            ctx.save();
            ctx.beginPath(); ctx.rect(pl, p.top, pr - pl, p.bottom - p.top); ctx.clip();
            if (u.r.histogram) {
                columns(u.r.histogram, p, function (k, v) {
                    if (kind === "MACD")
                        return v >= 0 ? (candle.edge ? "#008000" : candle.up) : (candle.edge ? "#ff0000" : candle.down);
                    if (candle.edge)
                        return colorOf(0, "");
                    return bars[k] && up(k) ? candle.up : candle.down;
                });
            }
            u.r.lines.forEach(function (l, j) { line(l.values, p, ["#000080", "#800000"][j % 2], 1); });
            // (its name, and its last value in view)
            var bits = [u.ind.label];
            var lastIn = -1;
            for (var k2 = end - 1; k2 >= start && lastIn < 0; --k2)
                if (bars[k2])
                    lastIn = k2;
            if (lastIn >= 0) {
                if (kind === "Volume" && u.r.histogram[lastIn] !== null)
                    bits.push(shortText(u.r.histogram[lastIn]));
                u.r.lines.forEach(function (l) { if (l.values[lastIn] !== null) bits.push(format(l.values[lastIn], 2)); });
            }
            var label = bits.join("  ");
            ctx.fillStyle = String(plotColor);
            ctx.fillRect(pl + 1, p.top + 1, textWidth(label) + 8, lineH);
            text(label, pl + 4, p.top + 1 + lineH / 2, "left");
            ctx.restore();
        });

        // each pane's frame
        ctx.strokeStyle = "#000000";
        ctx.lineWidth = 1;
        panes.forEach(function (p) {
            ctx.strokeRect(Math.round(pl) + 0.5, Math.round(p.top) + 0.5, Math.round(pr - pl) - 1, Math.round(p.bottom - p.top) - 1);
        });
        if (lastTag) {
            var tw = textWidth(lastTag.text) + 8, th = lineH, ty = lastTag.top;
            ctx.fillStyle = lastTag.color;
            ctx.fillRect(pr + 1, ty, tw, th);
            ctx.strokeStyle = "#000000";
            ctx.strokeRect(pr + 1.5, ty + 0.5, tw - 1, th - 1);
            ctx.fillStyle = lastTag.color === "#ffffff" || lastTag.color === "#ffff00" ? "#000000" : "#ffffff";
            ctx.textAlign = "left";
            ctx.fillText(lastTag.text, pr + 5, ty + th / 2);
        }

        // what's where: a bar's slot, all the way down
        var bottomAll = panes[panes.length - 1].bottom;
        for (var k3 = start; k3 < end; ++k3) {
            if (P.bars[k3])
                regs.push({ kind: "rect", x: pl + slot * (k3 - start), y: pt, w: slot, h: bottomAll - pt, item: stockItem(k3) });
        }
        stockView = { pl: pl, pr: pr, slot: slot, start: start, end: end,
                      panes: panes.map(function (p) { return { top: p.top, bottom: p.bottom, lo: p.lo, hi: p.hi, price: !!p.price,
                                                               volume: !!(p.study && p.study.ind.kind === "Volume") }; }),
                      overs: overs.map(function (o) { return { label: o.ind.label, lines: o.r.lines }; }),
                      unders: unders.map(function (u) { return { label: u.ind.label, lines: u.r.lines, histogram: u.r.histogram, kind: u.ind.kind }; }) };
    }

    // a bar, as itemAt says it: its values, and how far it moved from the one before
    function stockItem(k) {
        var P = priced, b = P.bars[k];
        if (!b)
            return null;
        var prev = null;
        for (var j = k - 1; j >= 0 && prev === null; --j)
            if (P.bars[j])
                prev = P.bars[j].c;
        return { series: P.index, index: k, label: labels[k], value: b.c, name: drawn[P.index].name,
                 open: b.o, high: b.h, low: b.l, close: b.c, volume: b.v,
                 change: prev === null ? null : b.c - prev, changePercent: prev ? (b.c - prev) / prev * 100 : null };
    }
    // and its tip: the bar, then each overlay's and study's value there
    function stockTip(item) {
        var lines = [String(item.label)];
        lines.push("Open " + priceText(item.open) + "   High " + priceText(item.high));
        lines.push("Low " + priceText(item.low) + "   Close " + priceText(item.close));
        if (item.change !== null)
            lines.push("Change " + (item.change >= 0 ? "+" : "") + priceText(item.change)
                       + (item.changePercent !== null ? " (" + (item.change >= 0 ? "+" : "") + item.changePercent.toFixed(2) + "%)" : ""));
        if (item.volume !== null && item.volume !== undefined)
            lines.push("Volume " + format(item.volume, 0));
        var v = stockView;
        if (v) {
            v.overs.concat(v.unders).forEach(function (o) {
                if (o.kind === "Volume")
                    return;
                var vals = o.lines.map(function (l) { return l.values[item.index]; }).filter(function (x) { return x !== null; });
                if (vals.length)
                    lines.push(o.label + ": " + vals.map(function (x) { return format(x, 2); }).join(", "));
            });
        }
        return lines.join("\n");
    }

    // --- live prices: a trade as it happens, or a whole bar ---

    // labels and series with a trade added: the same label as the last bar, it's that bar's
    // (its high, low, close and volume); a new label, a new bar. (pure: for those who keep their own)
    function withTick(list, data, label, price, volume, limit) {
        price = Number(price);
        if (!isFinite(price))
            return { labels: list, series: data };
        var v = volume === undefined || volume === null || volume === "" ? null : Number(volume);
        var out = copyStock(list, data), s = out.series[out.at];
        var n = out.labels.length;
        if (n > 0 && String(out.labels[n - 1]) === String(label) && s.close[n - 1] !== null && s.close[n - 1] !== undefined) {
            s.high[n - 1] = Math.max(s.high[n - 1] !== null && s.high[n - 1] !== undefined ? s.high[n - 1] : price, price);
            s.low[n - 1] = Math.min(s.low[n - 1] !== null && s.low[n - 1] !== undefined ? s.low[n - 1] : price, price);
            s.close[n - 1] = price;
            if (v !== null)
                s.volume[n - 1] = (Number(s.volume[n - 1]) || 0) + v;
        } else {
            out.labels.push(String(label));
            out.series.forEach(function (o, i) {
                if (i === out.at) {
                    o.open.push(price); o.high.push(price); o.low.push(price); o.close.push(price);
                    o.volume.push(v);
                } else {
                    o.values.push(null);
                }
            });
        }
        return trimStock(out, limit);
    }
    function withBar(list, data, label, open, high, low, close, volume, limit) {
        var out = copyStock(list, data), s = out.series[out.at], n = out.labels.length;
        function num(x) { var y = x === undefined || x === null || x === "" ? null : Number(x); return y !== null && isFinite(y) ? y : null; }
        var same = n > 0 && String(out.labels[n - 1]) === String(label);
        if (!same) {
            out.labels.push(String(label));
            out.series.forEach(function (o, i) {
                if (i === out.at) { o.open.push(null); o.high.push(null); o.low.push(null); o.close.push(null); o.volume.push(null); }
                else o.values.push(null);
            });
            n += 1;
        }
        s.open[n - 1] = num(open); s.high[n - 1] = num(high); s.low[n - 1] = num(low); s.close[n - 1] = num(close);
        s.volume[n - 1] = num(volume);
        return trimStock(out, limit);
    }
    // (copies, with a stock series, every list as long as the labels)
    function copyStock(list, data) {
        var lbls = (list || []).slice(), n = lbls.length, at = -1;
        var out = (data || []).map(function (one, i) {
            var o = Object.assign({}, one);
            ["open", "high", "low", "close", "volume", "values"].forEach(function (k) { if (o[k]) o[k] = o[k].slice(); });
            if (at < 0 && (o.close || o.open || o.high || o.low))
                at = i;
            return o;
        });
        if (at < 0) {
            out.unshift({ name: "", close: [] });
            at = 0;
        }
        var s = out[at];
        ["open", "high", "low", "close", "volume"].forEach(function (k) {
            s[k] = s[k] || [];
            while (s[k].length < n) s[k].push(null);
            s[k].length = n;
        });
        out.forEach(function (o, i) {
            if (i !== at) {
                o.values = o.values || [];
                while (o.values.length < n) o.values.push(null);
                o.values.length = n;
            }
        });
        return { labels: lbls, series: out, at: at };
    }
    function trimStock(out, limit) {
        var extra = limit > 0 ? out.labels.length - limit : 0;
        if (extra > 0) {
            out.labels.splice(0, extra);
            out.series.forEach(function (o) {
                ["open", "high", "low", "close", "volume", "values"].forEach(function (k) { if (o[k]) o[k].splice(0, extra); });
            });
        }
        return { labels: out.labels, series: out.series };
    }
    // (these change the graph's own labels and series: bound ones aren't bound any more)
    function tick(label, price, volume) {
        var r = withTick(labels, series, label, price, volume, maxBars);
        labels = r.labels;
        series = r.series;
    }
    function addBar(label, open, high, low, close, volume) {
        var r = withBar(labels, series, label, open, high, low, close, volume, maxBars);
        labels = r.labels;
        series = r.series;
    }

    // the value under the pointer, and clicks (a stock graph: the wheel zooms, dragging scrolls back)
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: graph.tips && graph.enabled
        enabled: graph.enabled
        cursorShape: graph.isStock && graph.stockView ? Qt.CrossCursor : Qt.ArrowCursor
        property var hovered: null
        property real pressX: 0
        property real pressBack: 0
        property bool dragged: false
        readonly property bool panning: graph.isStock && graph.zoomable && graph.stockView !== null
        onPositionChanged: (m) => {
            if (pressed && panning) {
                if (Math.abs(m.x - pressX) > 4)
                    dragged = true;
                if (dragged) {
                    var n = (graph.labels || []).length, v = graph.viewRange(n);
                    graph.panBack = Math.max(0, Math.min(n - v.count, pressBack + (m.x - pressX) / graph.stockView.slot));
                }
            }
            hovered = graph.itemAt(m.x, m.y);
        }
        onPressed: (m) => {
            pressX = m.x;
            pressBack = graph.panBack;
            dragged = false;
        }
        onExited: hovered = null
        onClicked: (m) => {
            if (!dragged)
                graph.clicked(graph.itemAt(m.x, m.y));
        }
        onDoubleClicked: {
            if (panning) {
                graph.zoomCount = 0;
                graph.panBack = 0;
            }
        }
        onWheel: (w) => {
            if (!panning || w.angleDelta.y === 0) {
                w.accepted = false;
                return;
            }
            var n = (graph.labels || []).length, cur = graph.viewRange(n).count;
            var next = w.angleDelta.y > 0 ? Math.min(cur - 1, Math.round(cur * 0.8)) : Math.max(cur + 1, Math.round(cur * 1.25));
            next = Math.max(Math.min(n, 5), Math.min(n, next));
            graph.zoomCount = next >= n && graph.showLast <= 0 ? 0 : next;
            graph.panBack = Math.max(0, Math.min(n - next, graph.panBack));
            hovered = graph.itemAt(w.x, w.y);
        }
    }

    // a stock graph's crosshair: down through the bar under the pointer, across at it, its value on the axis
    Item {
        id: cross
        anchors.fill: parent
        readonly property var view: graph.stockView
        readonly property var pane: {
            if (!view || !mouse.containsMouse)
                return null;
            for (var i = 0; i < view.panes.length; ++i)
                if (mouse.mouseY >= view.panes[i].top && mouse.mouseY <= view.panes[i].bottom)
                    return view.panes[i];
            return null;
        }
        visible: graph.isStock && graph.tips && view !== null && mouse.containsMouse && mouse.hovered !== null
        Rectangle {
            visible: cross.view !== null && mouse.hovered !== null
            x: cross.view && mouse.hovered ? Math.round(cross.view.pl + cross.view.slot * (mouse.hovered.index - cross.view.start + 0.5)) : 0
            y: cross.view ? cross.view.panes[0].top + 1 : 0
            width: 1
            height: cross.view ? cross.view.panes[cross.view.panes.length - 1].bottom - y - 1 : 0
            color: "#000000"
            opacity: 0.6
        }
        Rectangle {
            visible: cross.pane !== null
            x: cross.view ? cross.view.pl + 1 : 0
            y: Math.round(mouse.mouseY)
            width: cross.view ? cross.view.pr - cross.view.pl - 2 : 0
            height: 1
            color: "#000000"
            opacity: 0.6
        }
        Rectangle {
            visible: cross.pane !== null
            x: cross.view ? cross.view.pr + 1 : 0
            y: Math.round(mouse.mouseY - height / 2)
            width: crossText.implicitWidth + 8
            height: crossText.implicitHeight + 2
            color: "#000080"
            Text {
                id: crossText
                x: 4
                y: 1
                text: {
                    var p = cross.pane;
                    if (!p)
                        return "";
                    var v = p.hi - (mouse.mouseY - p.top) / (p.bottom - p.top) * (p.hi - p.lo);
                    return p.price ? graph.priceText(v) : p.volume ? graph.shortText(Math.max(0, v)) : graph.format(v, 2);
                }
                font.family: graph.fontFamily
                font.pixelSize: graph.fontSize
                color: "#ffffff"
            }
        }
    }
    Rectangle {
        visible: mouse.hovered !== null && graph.tips
        x: Math.min(Math.max(0, mouse.mouseX + 12), graph.width - width)
        y: Math.min(Math.max(0, mouse.mouseY + 16), graph.height - height)
        width: tipText.implicitWidth + 8
        height: tipText.implicitHeight + 4
        color: "#ffffe1"
        border.color: "#000000"
        Text {
            id: tipText
            x: 4
            y: 2
            text: !mouse.hovered ? "" : graph.isStock && mouse.hovered.open !== undefined ? graph.stockTip(mouse.hovered)
                  : (mouse.hovered.name !== "" && !graph.isPie ? mouse.hovered.name + ", " : "")
                    + mouse.hovered.label + ": " + graph.format(mouse.hovered.value)
            font.family: graph.fontFamily
            font.pixelSize: graph.fontSize
            color: "#000000"
        }
    }
}
