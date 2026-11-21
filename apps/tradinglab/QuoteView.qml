import QtQuick
import Win95Ui

// Quote Details: the short quote, key statistics, the latest aftermarket trade and bid/ask, and price performance
// over each period for the chart's symbol. Port of upstream renderer/src/views/QuoteView.tsx (FMP fmp:overview);
// without an FMP key it shows deterministic sample numbers with the sample-data note.
Item {
    id: view
    property var app

    readonly property string symbol: app ? app.symbol : ""
    readonly property bool live: app ? app.live : false
    property var result: null
    property string error: ""
    property bool loading: false
    property int seq: 0

    readonly property var periods: [["1D", "1D"], ["5D", "5D"], ["1M", "1M"], ["3M", "3M"], ["6M", "6M"], ["YTD", "ytd"], ["1Y", "1Y"], ["3Y", "3Y"], ["5Y", "5Y"], ["10Y", "10Y"], ["Max", "max"]]

    // FMP fields vary a little by endpoint/version; take the first one present (upstream data/fmp.ts pick/pickStr)
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

    function buildQuote(o) {
        const quote = o.quote, profile = o.profile, qs = o.quoteShort, at = o.afterTrade, aq = o.afterQuote, pc = o.priceChange;
        const price = pick(qs, "price") ?? pick(quote, "price");
        const change = pick(qs, "change") ?? pick(quote, "change");
        const prev = pick(quote, "previousClose") ?? (price !== null && change !== null ? price - change : null);
        const div = pick(profile, "lastDividend", "lastDiv");
        const ms = function (v) { return v === null ? null : v > 1e11 ? v / 1000 : v; }; // aftermarket timestamps are in milliseconds
        return {
            name: pickStr(profile, "companyName") || pickStr(quote, "name"),
            sub: [pickStr(quote, "exchange") || pickStr(profile, "exchange"), pickStr(profile, "sector"), pickStr(profile, "industry")].filter(Boolean).join(" · "),
            price: price, change: change, changePct: prev && change !== null ? (change / prev) * 100 : pick(quote, "changePercentage", "changesPercentage"),
            volume: pick(qs, "volume") ?? pick(quote, "volume"),
            prev: prev, open: pick(quote, "open"), dayLow: pick(quote, "dayLow"), dayHigh: pick(quote, "dayHigh"), low52: pick(quote, "yearLow"), high52: pick(quote, "yearHigh"),
            avgVolume: pick(quote, "avgVolume") ?? pick(profile, "averageVolume", "volAvg"), marketCap: pick(quote, "marketCap") ?? pick(profile, "marketCap", "mktCap"),
            pe: pick(quote, "pe"), eps: pick(quote, "eps"), beta: pick(profile, "beta"), divYield: div !== null && price ? (div / price) * 100 : null,
            trade: at ? { price: pick(at, "price"), size: pick(at, "tradeSize"), time: ms(pick(at, "timestamp")) } : null,
            book: aq ? { bid: pick(aq, "bidPrice"), bidSize: pick(aq, "bidSize"), ask: pick(aq, "askPrice"), askSize: pick(aq, "askSize"), volume: pick(aq, "volume"), time: ms(pick(aq, "timestamp")) } : null,
            performance: pc ? periods.map(function (p) { return { label: p[0], pct: pick(pc, p[1]) }; }) : [],
            asOf: o.asOf
        };
    }

    function fromSample(sym) {
        const s = sample.quote(sym);
        const now = Date.now() / 1000;
        const perf = [s.changePct, 1.2, 4.1, 9.8, 15.2, 12.4, 22.3, 61, 98, 240, 5200];
        return {
            name: "", sub: "", price: s.price, change: s.change, changePct: s.changePct, volume: s.volume, prev: s.prev, open: s.open, dayLow: s.dayLow, dayHigh: s.dayHigh,
            low52: s.low52, high52: s.high52, avgVolume: s.avgVolume, marketCap: s.marketCap, pe: s.pe, eps: s.eps, beta: s.beta, divYield: s.dividendYield,
            trade: { price: s.price + 0.01, size: 100, time: now }, book: { bid: s.price - 0.02, bidSize: 3, ask: s.price + 0.03, askSize: 5, volume: s.volume, time: now },
            performance: periods.map(function (p, i) { return { label: p[0], pct: perf[i] }; }), asOf: null
        };
    }

    function load(force) {
        if (!live || !symbol) return;
        const id = ++seq;
        const want = symbol;
        loading = true;
        error = "";
        api.invoke("fmp:overview", [want, !!force], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            result = buildQuote(r.data);
        });
    }
    onSymbolChanged: { result = null; load(false); }
    onLiveChanged: load(false)
    // re-read while open: the cache serves it unless the data has aged out, so this stays cheap
    Timer { interval: 20000; running: view.live; repeat: true; onTriggered: view.load(false) }

    readonly property var q: live ? result : (symbol ? fromSample(symbol) : null)
    readonly property var spread: q && q.book && q.book.ask !== null && q.book.bid !== null ? q.book.ask - q.book.bid : null
    readonly property var mid: q && q.book && q.book.ask !== null && q.book.bid !== null ? (q.book.ask + q.book.bid) / 2 : null
    readonly property real totalSize: q && q.book ? (q.book.bidSize ?? 0) + (q.book.askSize ?? 0) : 0
    readonly property var tradeVsQuote: q && q.trade && q.trade.price !== null && q.price !== null ? q.trade.price - q.price : null

    function dash(v, fmt) { return v === null || v === undefined ? "—" : fmt(v); }
    function pctFmt(n) { return Math.abs(n) >= 10000 ? (n >= 0 ? "+" : "") + Util.compact(n) + "%" : Util.pct(n); }
    function clockAt(t) { return t === null || t === undefined ? "—" : Util.dateTime(t); }

    SvSample { id: sample }

    SymbolHeader { id: header; app: view.app; width: view.width }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        sample: view.live ? "" : "Sample data — add your FMP key in File → Settings."
        loading: view.live && view.loading && !view.q
        error: view.error
        asOf: view.live && view.q && view.q.asOf ? view.q.asOf : 0
        refreshing: view.loading
        onRetry: view.load(false)
        onRefresh: view.load(true)

        Row {
            visible: !!view.q && view.q.name !== ""
            spacing: 6
            Heading95 { id: heroName; text: view.q ? view.q.name : "" }
            Label95 { anchors.baseline: heroName.baseline; dim: true; text: view.q && view.q.sub ? "— " + view.q.sub : "" }
        }

        Grid {
            id: grid
            visible: !!view.q
            width: page.contentWidth
            columns: 2
            spacing: 8
            readonly property real cardWidth: (width - spacing) / 2

            SvCard {
                width: grid.cardWidth
                height: Math.max(implicitHeight, statsCard.implicitHeight)
                title: "Short quote"
                sub: "price, change, volume"
                Row {
                    spacing: 10
                    Heading95 { level: 1; text: view.q ? view.dash(view.q.price, Util.usd) : "" }
                    Label95 {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 2
                        visible: !!view.q && view.q.change !== null
                        color: view.q ? Util.tone(view.q.change) : Theme.text
                        text: view.q && view.q.change !== null ? (view.q.change >= 0 ? "+" : "") + view.q.change.toFixed(2) + (view.q.changePct !== null ? " (" + Util.pct(view.q.changePct) + ")" : "") : ""
                    }
                }
                SvKV { width: parent.width; entries: view.q ? { "Volume": view.dash(view.q.volume, Util.compact), "Previous close": view.dash(view.q.prev, Util.usd) } : {} }
                SvRangeBar {
                    width: parent.width
                    visible: !!view.q && view.q.price !== null && view.q.dayLow !== null && view.q.dayHigh !== null
                    label: "Day range"
                    low: view.q && view.q.dayLow !== null ? view.q.dayLow : 0
                    high: view.q && view.q.dayHigh !== null ? view.q.dayHigh : 1
                    value: view.q && view.q.price !== null ? view.q.price : 0
                }
                SvRangeBar {
                    width: parent.width
                    visible: !!view.q && view.q.price !== null && view.q.low52 !== null && view.q.high52 !== null
                    label: "52-week range"
                    low: view.q && view.q.low52 !== null ? view.q.low52 : 0
                    high: view.q && view.q.high52 !== null ? view.q.high52 : 1
                    value: view.q && view.q.price !== null ? view.q.price : 0
                }
            }

            SvCard {
                id: statsCard
                width: grid.cardWidth
                title: "Key statistics"
                SvKV {
                    width: parent.width
                    entries: view.q ? {
                        "Open": view.dash(view.q.open, Util.usd), "Avg volume": view.dash(view.q.avgVolume, Util.compact), "Market cap": view.dash(view.q.marketCap, Util.compact),
                        "P/E (TTM)": view.dash(view.q.pe, function (n) { return n.toFixed(2); }), "EPS (TTM)": view.dash(view.q.eps, Util.usd),
                        "Dividend yield": view.dash(view.q.divYield, function (n) { return n.toFixed(2) + "%"; }), "Beta": view.dash(view.q.beta, function (n) { return n.toFixed(2); })
                    } : {}
                }
            }

            SvCard {
                id: tradeCard
                visible: !!view.app && view.app.hasCap("aftermarket")
                width: grid.cardWidth
                height: Math.max(implicitHeight, bookCard.implicitHeight)
                title: "Aftermarket trade"
                sub: "latest trade, including extended hours"
                Label95 { visible: !!view.q && !view.q.trade; dim: true; text: "No trade data available." }
                Row {
                    visible: !!view.q && !!view.q.trade
                    spacing: 10
                    Heading95 { text: view.q && view.q.trade ? view.dash(view.q.trade.price, Util.usd) : "" }
                    Label95 {
                        anchors.bottom: parent.bottom
                        visible: view.tradeVsQuote !== null
                        color: Util.tone(view.tradeVsQuote)
                        text: view.tradeVsQuote !== null ? (view.tradeVsQuote >= 0 ? "+" : "") + view.tradeVsQuote.toFixed(2) + " vs quote" : ""
                    }
                }
                SvKV {
                    visible: !!view.q && !!view.q.trade
                    width: parent.width
                    entries: view.q && view.q.trade ? { "Trade size": view.q.trade.size !== null ? Util.num(view.q.trade.size, 0) + " sh" : "—", "Traded at": view.clockAt(view.q.trade.time) } : {}
                }
            }

            SvCard {
                id: bookCard
                visible: tradeCard.visible
                width: grid.cardWidth
                title: "Aftermarket quote"
                sub: "latest bid and ask"
                Label95 { visible: !!view.q && !view.q.book; dim: true; text: "No quote data available." }
                Row {
                    visible: !!view.q && !!view.q.book
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: view.q && view.q.book ? [["Bid", view.q.book.bid, view.q.book.bidSize, "#007000"], ["Ask", view.q.book.ask, view.q.book.askSize, "#b00000"]] : []
                        delegate: Bevel {
                            required property var modelData
                            style: "thinSunken"
                            face: Theme.window
                            width: (parent.width - 8) / 2
                            height: 24
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Label95 { dim: true; text: modelData[0] }
                                Label95 { bold: true; color: modelData[3]; text: view.dash(modelData[1], Util.usd) }
                                Label95 { dim: true; text: "× " + (modelData[2] ?? "—") }
                            }
                        }
                    }
                }
                Bevel {
                    visible: view.totalSize > 0
                    width: parent.width
                    height: 8
                    style: "thinSunken"
                    face: "#e0b0b0"
                    Rectangle {
                        x: 1; y: 1
                        height: parent.height - 2
                        width: view.q && view.q.book && view.totalSize > 0 ? (parent.width - 2) * (view.q.book.bidSize ?? 0) / view.totalSize : 0
                        color: "#80c080"
                    }
                    ToolTipArea { text: "Share of displayed size on each side" }
                }
                SvKV {
                    visible: !!view.q && !!view.q.book
                    width: parent.width
                    entries: view.q && view.q.book ? {
                        "Spread": view.spread !== null ? Util.usd(view.spread) + (view.mid ? " (" + ((view.spread / view.mid) * 100).toFixed(3) + "%)" : "") : "—",
                        "Midpoint": view.dash(view.mid, Util.usd), "Volume": view.dash(view.q.book.volume, Util.compact), "Quoted at": view.clockAt(view.q.book.time)
                    } : {}
                }
            }
        }

        SvCard {
            visible: !!view.q
            width: page.contentWidth
            title: "Price performance"
            sub: "% change over each period"
            Label95 { visible: !!view.q && view.q.performance.length === 0; dim: true; text: "No performance data available." }
            Flow {
                width: parent.width
                spacing: 4
                Repeater {
                    model: view.q ? view.q.performance : []
                    delegate: Bevel {
                        required property var modelData
                        style: "thinSunken"
                        face: modelData.pct === null ? Theme.window : modelData.pct >= 0 ? "#e2f4e2" : "#f8e0e0"
                        width: 62
                        height: 36
                        Column {
                            anchors.centerIn: parent
                            spacing: 1
                            Label95 { anchors.horizontalCenter: parent.horizontalCenter; dim: true; text: modelData.label }
                            Label95 {
                                anchors.horizontalCenter: parent.horizontalCenter
                                bold: true
                                color: Util.tone(modelData.pct)
                                text: modelData.pct === null ? "—" : view.pctFmt(modelData.pct)
                            }
                        }
                    }
                }
            }
        }
    }
}
