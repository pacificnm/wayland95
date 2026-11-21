import QtQuick
import Win95Ui

// Analyst Reports: the analysts' consensus, price targets and their trend, FMP's ratings snapshot and its history,
// consensus estimates against reported results, and recent rating changes. Port of upstream
// renderer/src/views/AnalystView.tsx (fmp:analyst + fmp:overview); sample numbers without an FMP key.
Item {
    id: view
    property var app

    readonly property string symbol: app ? app.symbol : ""
    readonly property bool live: app ? app.live : false
    property var result: null
    property string error: ""
    property bool loading: false
    property int seq: 0

    readonly property var colors: ({ "Strong Buy": "#1b9e77", "Buy": "#26a69a", "Hold": "#f5a623", "Sell": "#ef5350", "Strong Sell": "#b71c1c" })
    readonly property var order: ["Strong Buy", "Buy", "Hold", "Sell", "Strong Sell"]
    readonly property var scoreRows: [["dcf", "Discounted cash flow"], ["roe", "Return on equity"], ["roa", "Return on assets"], ["de", "Debt to equity"], ["pe", "Price to earnings"], ["pb", "Price to book"]]
    function letterColor(l) { return ({ A: "#1b9e77", B: "#5aa858", C: "#d08a10", D: "#d8702a", F: "#d84a48" })[String(l || "").charAt(0).toUpperCase()] || "#9e9e9e"; }
    function pctVs(target, price) { return target !== null && target !== undefined && price ? ((target - price) / price) * 100 : null; }
    function pctText(n) { return n === null || n === undefined ? "" : (n >= 0 ? "+" : "") + n.toFixed(1) + "%"; }
    function compactMoney(n) {
        const a = Math.abs(n), s = n < 0 ? "-" : "", f = function (v) { return String(Math.round(v * 10) / 10); };
        return a >= 1e12 ? s + f(a / 1e12) + "T" : a >= 1e9 ? s + f(a / 1e9) + "B" : a >= 1e6 ? s + f(a / 1e6) + "M" : a >= 1e3 ? s + f(a / 1e3) + "K" : s + f(a);
    }

    function pick(o, ...names) {
        if (!o) return null;
        for (const n of names) { const v = o[n]; if (v !== undefined && v !== null && v !== "" && isFinite(Number(v))) return Number(v); }
        return null;
    }
    function pickStr(o, ...names) {
        if (!o) return "";
        for (const n of names) { const v = o[n]; if (v !== undefined && v !== null && v !== "") return String(v); }
        return "";
    }

    function buildAnalystView(a, o) {
        const sm = a.targetSummary, sn = a.snapshot;
        const history = (a.ratingsHistory || []).slice().reverse().map(function (h) { return { date: pickStr(h, "date").slice(0, 10), score: pick(h, "overallScore") ?? 0, letter: pickStr(h, "rating") }; }).filter(function (h) { return h.score > 0; });
        let publishers = [];
        try { publishers = JSON.parse(pickStr(sm, "publishers") || "[]"); } catch (e) { /* ignore */ }
        return {
            counts: { "Strong Buy": pick(a.consensus, "strongBuy") ?? 0, "Buy": pick(a.consensus, "buy") ?? 0, "Hold": pick(a.consensus, "hold") ?? 0, "Sell": pick(a.consensus, "sell") ?? 0, "Strong Sell": pick(a.consensus, "strongSell") ?? 0 },
            consensus: pickStr(a.consensus, "consensus"), price: pick(o.quote, "price"),
            low: pick(a.targets, "targetLow"), avg: pick(a.targets, "targetConsensus"), high: pick(a.targets, "targetHigh"), median: pick(a.targets, "targetMedian"),
            summary: [["Last month", "lastMonth"], ["Last quarter", "lastQuarter"], ["Last year", "lastYear"], ["All time", "allTime"]].map(function (p) { return { label: p[0], count: pick(sm, p[1] + "Count"), avg: pick(sm, p[1] + "AvgPriceTarget") }; }),
            publishers: Array.isArray(publishers) ? publishers : [],
            snapshot: sn ? { letter: pickStr(sn, "rating"), overall: pick(sn, "overallScore") ?? 0,
                             scores: { dcf: pick(sn, "discountedCashFlowScore"), roe: pick(sn, "returnOnEquityScore"), roa: pick(sn, "returnOnAssetsScore"), de: pick(sn, "debtToEquityScore"), pe: pick(sn, "priceToEarningsScore"), pb: pick(sn, "priceToBookScore") } } : null,
            history: history,
            reports: (a.grades || []).map(function (g) { return { date: pickStr(g, "date").slice(0, 10), firm: pickStr(g, "gradingCompany"), action: pickStr(g, "action"), from: pickStr(g, "previousGrade"), to: pickStr(g, "newGrade") }; }),
            estimates: buildEstimates(a, Date.now()),
            asOf: a.asOf
        };
    }

    // Fiscal years from the last reported one onward. Reported years show the actual and how it compared with consensus.
    function buildEstimates(a, now) {
        const byDate = {};
        (a.actuals || []).forEach(function (r) { byDate[pickStr(r, "date").slice(0, 10)] = r; });
        const rows = (a.estimates || []).slice().sort(function (x, y) { return pickStr(x, "date").localeCompare(pickStr(y, "date")); });
        const cutoff = new Date(now - 420 * 86400000).toISOString().slice(0, 10);
        const beat = function (actual, est) { return actual !== null && est ? ((actual - est) / Math.abs(est)) * 100 : null; };
        return rows.filter(function (r) { return pickStr(r, "date").slice(0, 10) >= cutoff; }).map(function (r) {
            const date = pickStr(r, "date").slice(0, 10);
            const act = byDate[date] || null;
            const estRev = pick(r, "revenueAvg"), estEps = pick(r, "epsAvg");
            const aRev = pick(act, "revenue"), aEps = pick(act, "epsDiluted", "epsdiluted", "eps");
            return {
                year: date.slice(0, 4), actual: act !== null,
                revenue: aRev ?? estRev, epsVal: aEps ?? estEps,
                revLow: pick(r, "revenueLow"), revHigh: pick(r, "revenueHigh"), epsLow: pick(r, "epsLow"), epsHigh: pick(r, "epsHigh"),
                nRev: pick(r, "numAnalystsRevenue"), nEps: pick(r, "numAnalystsEps"),
                revBeat: beat(aRev, estRev), epsBeat: beat(aEps, estEps)
            };
        }).slice(0, 5);
    }

    function fromSample(sym) {
        const s = sample.analyst(sym);
        const letters = s.history.map(function (h) { return ["F", "D", "C", "B", "A"][h.score - 1]; });
        return {
            counts: s.counts, consensus: "Buy", price: s.price, low: s.low, avg: s.avg, high: s.high, median: s.median,
            summary: [["Last month", s.summary.month], ["Last quarter", s.summary.quarter], ["Last year", s.summary.year], ["All time", s.summary.all]].map(function (p) { return { label: p[0], count: p[1].count, avg: p[1].avg }; }),
            publishers: [], snapshot: s.snapshot,
            history: s.history.map(function (h, i) { return { date: h.date, score: h.score, letter: letters[i] }; }).reverse(),
            reports: s.reports.map(function (r) { return { date: r.date, firm: r.firm, action: r.action, from: "", to: r.rating }; }),
            estimates: s.estimates, asOf: null
        };
    }

    function load(force) {
        if (!live || !symbol) return;
        const id = ++seq;
        const want = symbol;
        loading = true;
        error = "";
        let a = null, o = null, failed = "";
        const done = function () {
            if (id !== seq || (a === null && !failed) || (o === null && !failed)) return;
            loading = false;
            if (failed) { error = failed; return; }
            result = buildAnalystView(a, o);
        };
        api.invoke("fmp:analyst", [want, !!force], function (r) { if (r && r.ok) a = r.data; else if (!failed) failed = r ? r.error : "No answer from the data service."; done(); });
        api.invoke("fmp:overview", [want, !!force], function (r) { if (r && r.ok) o = r.data; else if (!failed) failed = r ? r.error : "No answer from the data service."; done(); });
    }
    onSymbolChanged: { result = null; load(false); }
    onLiveChanged: load(false)

    readonly property var v: live ? result : (symbol ? fromSample(symbol) : null)
    readonly property int total: v ? Object.keys(v.counts).reduce(function (s, k) { return s + v.counts[k]; }, 0) : 0
    readonly property real maxAvg: v ? Math.max.apply(null, v.summary.map(function (x) { return x.avg ?? 0; }).concat([v.price ?? 0])) : 0

    SvSample { id: sample }

    SymbolHeader { id: header; app: view.app; width: view.width }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        sample: view.live ? "" : "Sample data — add your FMP key in File → Settings."
        loading: view.live && view.loading && !view.v
        error: view.error
        asOf: view.live && view.v && view.v.asOf ? view.v.asOf : 0
        refreshing: view.loading
        onRetry: view.load(false)
        onRefresh: view.load(true)

        Grid {
            id: grid
            visible: !!view.v
            width: page.contentWidth
            columns: 2
            spacing: 8
            readonly property real cardWidth: (width - spacing) / 2

            SvCard {
                id: consensusCard
                width: grid.cardWidth
                height: Math.max(implicitHeight, targetCard.implicitHeight)
                title: "Analyst consensus"
                sub: view.total > 0 ? view.total + " analysts" : ""
                Label95 { visible: view.total === 0; dim: true; text: "No analyst rating summary available." }
                Heading95 {
                    visible: view.total > 0 && !!view.v && view.v.consensus !== ""
                    level: 1
                    text: view.v ? view.v.consensus : ""
                    color: view.v && view.colors[view.v.consensus] ? view.colors[view.v.consensus] : Theme.text
                }
                Row {
                    visible: view.total > 0
                    width: parent.width
                    height: 20
                    Repeater {
                        model: view.v ? view.order.filter(function (k) { return view.v.counts[k] > 0; }) : []
                        delegate: Rectangle {
                            required property string modelData
                            width: view.total ? parent.width * view.v.counts[modelData] / view.total : 0
                            height: 20
                            color: view.colors[modelData]
                            border.color: Theme.dark
                            Label95 { anchors.centerIn: parent; color: "white"; bold: true; text: view.v.counts[parent.modelData]; visible: parent.width > 14 }
                            ToolTipArea { text: parent.modelData + ": " + view.v.counts[parent.modelData] }
                        }
                    }
                }
                Flow {
                    visible: view.total > 0
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: view.order
                        delegate: Row {
                            required property string modelData
                            spacing: 4
                            Rectangle { width: 10; height: 10; anchors.verticalCenter: parent.verticalCenter; color: view.colors[parent.modelData]; border.color: Theme.dark }
                            Label95 { text: parent.modelData }
                        }
                    }
                }
            }

            SvCard {
                id: targetCard
                width: grid.cardWidth
                title: "Price target consensus"
                readonly property bool has: !!view.v && view.v.low !== null && view.v.high !== null && view.v.price !== null
                Label95 { visible: !targetCard.has; dim: true; text: "No price targets available." }
                SvRangeBar {
                    visible: targetCard.has
                    width: parent.width
                    label: targetCard.has ? "Range " + Util.usd(view.v.low) + " – " + Util.usd(view.v.high) + " · current " + Util.usd(view.v.price) : ""
                    low: targetCard.has ? Math.min(view.v.low, view.v.price) : 0
                    high: targetCard.has ? Math.max(view.v.high, view.v.price) : 1
                    value: targetCard.has ? view.v.price : 0
                    fmt: function (n) { return n.toFixed(0); }
                }
                SvKV {
                    visible: targetCard.has
                    width: parent.width
                    entries: targetCard.has ? {
                        "Consensus": view.v.avg !== null ? Util.usd(view.v.avg) + "  (" + view.pctText(view.pctVs(view.v.avg, view.v.price)) + ")" : "—",
                        "Median": view.v.median !== null ? Util.usd(view.v.median) + "  (" + view.pctText(view.pctVs(view.v.median, view.v.price)) + ")" : "—",
                        "High": Util.usd(view.v.high) + "  (" + view.pctText(view.pctVs(view.v.high, view.v.price)) + ")",
                        "Low": Util.usd(view.v.low) + "  (" + view.pctText(view.pctVs(view.v.low, view.v.price)) + ")"
                    } : {}
                }
            }

            SvCard {
                id: trendCard
                width: grid.cardWidth
                height: Math.max(implicitHeight, snapCard.implicitHeight)
                title: "Price target trend"
                sub: "average target by period"
                readonly property bool has: !!view.v && view.v.summary.some(function (x) { return x.avg !== null; })
                Label95 { visible: !trendCard.has; dim: true; text: "No price target history available." }
                SvTable {
                    visible: trendCard.has
                    width: parent.width
                    model: view.v ? view.v.summary : []
                    columns: [
                        { title: "Period", flex: true, min: 80, text: function (x) { return x.label; } },
                        { title: "Targets", w: 52, right: true, text: function (x) { return x.count ?? "—"; } },
                        { title: "Average", w: 70, right: true, text: function (x) { return x.avg !== null ? Util.usd(x.avg) : "—"; } },
                        { title: "vs price", w: 56, right: true, text: function (x) { return view.pctText(view.pctVs(x.avg, view.v.price)); }, color: function (x) { return Util.tone(view.pctVs(x.avg, view.v.price)); } },
                        { title: "", w: 60, bar: function (x) { return view.maxAvg && x.avg ? x.avg / view.maxAvg : 0; } }
                    ]
                }
                Label95 {
                    visible: !!view.v && view.v.publishers.length > 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    dim: true
                    text: view.v ? "Sources: " + view.v.publishers.join(", ") : ""
                }
            }

            SvCard {
                id: snapCard
                width: grid.cardWidth
                title: "Ratings snapshot"
                sub: "FMP's own score, not an analyst opinion"
                Label95 { visible: !!view.v && !view.v.snapshot; dim: true; text: "No rating snapshot available." }
                Row {
                    visible: !!view.v && !!view.v.snapshot
                    spacing: 12
                    width: parent.width
                    Rectangle {
                        width: 70
                        height: 70
                        color: Theme.window
                        border.width: 3
                        border.color: view.v && view.v.snapshot ? view.letterColor(view.v.snapshot.letter) : Theme.shadow
                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: view.v && view.v.snapshot ? (view.v.snapshot.letter || "—") : ""
                                textFormat: Text.PlainText
                                font.family: Theme.fontFamily
                                font.pixelSize: 26
                                font.bold: true
                                color: view.v && view.v.snapshot ? view.letterColor(view.v.snapshot.letter) : Theme.text
                            }
                            Label95 { anchors.horizontalCenter: parent.horizontalCenter; small: true; text: view.v && view.v.snapshot ? "overall " + view.v.snapshot.overall + "/5" : "" }
                        }
                    }
                    Column {
                        width: parent.width - 82
                        spacing: 1
                        Repeater {
                            model: view.scoreRows
                            delegate: Item {
                                required property var modelData
                                readonly property var n: view.v && view.v.snapshot ? (view.v.snapshot.scores[modelData[0]] ?? null) : null
                                width: parent.width
                                height: 16
                                Label95 { anchors.verticalCenter: parent.verticalCenter; text: parent.modelData[1] }
                                Row {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 2
                                    Repeater {
                                        model: 5
                                        delegate: Rectangle {
                                            required property int index
                                            width: 10
                                            height: 10
                                            border.color: Theme.shadow
                                            readonly property var lvl: parent.parent.n
                                            color: lvl !== null && index + 1 <= lvl ? (lvl >= 4 ? "#1b9e77" : lvl === 3 ? "#f5a623" : "#ef5350") : Theme.window
                                        }
                                    }
                                }
                                ToolTipArea { text: parent.n === null ? "n/a" : parent.n + " out of 5" }
                            }
                        }
                    }
                }
                SvNote { text: "Each factor is scored 1 (weak) to 5 (strong) from the company's financials and valuation." }
            }

            SvCard {
                id: historyCard
                width: grid.cardWidth
                height: Math.max(implicitHeight, estCard.visible ? estCard.implicitHeight : 0)
                title: "Rating history"
                sub: view.v && view.v.history.length ? Util.plain(view.v.history[0].date + " to " + view.v.history[view.v.history.length - 1].date) : ""
                Label95 { visible: !!view.v && view.v.history.length <= 1; dim: true; text: "No rating history available." }
                Canvas {
                    id: histCanvas
                    visible: !!view.v && view.v.history.length > 1
                    width: parent.width
                    height: 150
                    readonly property var hist: view.v ? view.v.history : []
                    onHistChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(0, 0, width, height);
                        const data = hist;
                        if (data.length < 2) return;
                        const W = width, H = height, L = 24, R = 8, T = 8, B = 20;
                        const x = function (i) { return L + (i / (data.length - 1)) * (W - L - R); };
                        const y = function (v) { return T + (1 - (v - 1) / 4) * (H - T - B); };
                        ctx.font = "10px 'Liberation Sans'";
                        ctx.strokeStyle = "#d8d8d8";
                        ctx.fillStyle = "#606060";
                        ctx.lineWidth = 1;
                        for (let v = 1; v <= 5; ++v) {
                            ctx.beginPath(); ctx.moveTo(L, Math.round(y(v)) + 0.5); ctx.lineTo(W - R, Math.round(y(v)) + 0.5); ctx.stroke();
                            ctx.textAlign = "right"; ctx.fillText(String(v), L - 6, y(v) + 3);
                        }
                        ctx.strokeStyle = "#000080";
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        ctx.moveTo(x(0), y(data[0].score));
                        for (let i = 1; i < data.length; ++i) { ctx.lineTo(x(i), y(data[i - 1].score)); ctx.lineTo(x(i), y(data[i].score)); }
                        ctx.stroke();
                        for (let i = 1; i < data.length; ++i) {
                            if (data[i].letter === data[i - 1].letter) continue;
                            ctx.fillStyle = view.letterColor(data[i].letter);
                            ctx.beginPath(); ctx.arc(x(i), y(data[i].score), 3.5, 0, Math.PI * 2); ctx.fill();
                            ctx.strokeStyle = "#000000"; ctx.lineWidth = 1; ctx.stroke();
                        }
                        ctx.fillStyle = "#606060";
                        ctx.textAlign = "left"; ctx.fillText(data[0].date, L, H - 4);
                        ctx.textAlign = "right"; ctx.fillText(data[data.length - 1].date, W - R, H - 4);
                    }
                }
                SvNote { visible: histCanvas.visible; text: "Overall score (1–5) over time. Dots mark days the letter grade changed." }
            }

            SvCard {
                id: estCard
                visible: !!view.app && view.app.hasCap("estimates")
                width: grid.cardWidth
                title: "Analyst estimates"
                sub: "consensus by fiscal year"
                Label95 { visible: !!view.v && view.v.estimates.length === 0; dim: true; text: "No estimates available." }
                SvTable {
                    id: estTable
                    visible: !!view.v && view.v.estimates.length > 0
                    width: parent.width
                    model: view.v ? view.v.estimates : []
                    function growth(i, key) {
                        const e = view.v.estimates[i], prev = view.v.estimates[i - 1];
                        return prev && e[key] !== null && prev[key] ? ((e[key] - prev[key]) / Math.abs(prev[key])) * 100 : null;
                    }
                    columns: [
                        { title: "FY", w: 62, bold: true, text: function (e) { return e.year; }, sub: function (e) { return e.actual ? "reported" : "est."; } },
                        { title: "Revenue", flex: true, min: 70, right: true, text: function (e) { return e.revenue !== null ? "$" + view.compactMoney(e.revenue) : "—"; },
                          sub: function (e) { return e.revBeat !== null ? (e.revBeat >= 0 ? "beat " : "missed ") + view.pctText(e.revBeat) : (e.revLow !== null && e.revHigh !== null ? "$" + view.compactMoney(e.revLow) + "–" + view.compactMoney(e.revHigh) : ""); } },
                        { title: "Growth", w: 52, right: true, text: function (e, i) { return view.pctText(estTable.growth(i, "revenue")); }, color: function (e) { return Util.tone(estTable.growth(view.v.estimates.indexOf(e), "revenue")); } },
                        { title: "EPS", w: 70, right: true, text: function (e) { return e.epsVal !== null ? "$" + e.epsVal.toFixed(2) : "—"; },
                          sub: function (e) { return e.epsBeat !== null ? (e.epsBeat >= 0 ? "beat " : "missed ") + view.pctText(e.epsBeat) : (e.epsLow !== null && e.epsHigh !== null ? "$" + e.epsLow.toFixed(2) + "–" + e.epsHigh.toFixed(2) : ""); } },
                        { title: "Growth", w: 52, right: true, text: function (e, i) { return view.pctText(estTable.growth(i, "epsVal")); }, color: function (e) { return Util.tone(estTable.growth(view.v.estimates.indexOf(e), "epsVal")); } },
                        { title: "Analysts", w: 56, right: true, text: function (e) { return e.nEps ?? "—"; }, color: function () { return Theme.shadow; } }
                    ]
                    rowFill: function (e) { return e.actual ? "#eef6ee" : ""; }
                }
                SvNote {
                    visible: estTable.visible
                    text: "Reported years show actual results and how they compared with what analysts expected. Under each estimate is its low–high range. Few analysts cover the far years, so treat them loosely."
                }
            }
        }

        SvCard {
            visible: !!view.v
            width: page.contentWidth
            title: "Recent rating changes"
            SvTable {
                width: parent.width
                model: view.v ? view.v.reports : []
                emptyText: "None available."
                columns: [
                    { title: "Date", w: 90, text: function (r) { return r.date; } },
                    { title: "Firm", flex: true, min: 140, text: function (r) { return r.firm; } },
                    { title: "Action", w: 110, text: function (r) { return r.action; } },
                    { title: "Rating", flex: true, min: 140, text: function (r) { return r.from && r.from !== r.to ? r.from + " → " + r.to : r.to; } }
                ]
            }
        }
    }
}
