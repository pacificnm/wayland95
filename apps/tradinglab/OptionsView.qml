import QtQuick
import Win95Ui

// Option Stats: a learning view of the symbol's listed options (Cboe's public delayed feed): implied against realised
// volatility, the expected move, put/call activity, max pain, the IV term structure, open interest by strike, the
// busiest contracts and the chain with greeks. Port of upstream renderer/src/views/OptionsView.tsx (options:chain);
// the maths is the C++ port of shared/options.ts through core.callPure. The paper account trades stocks only.
Item {
    id: view
    property var app

    readonly property string symbol: app ? app.symbol : ""
    readonly property bool live: app ? app.live : false
    property var chain: null
    property string error: ""
    property bool loading: false
    property int seq: 0
    property var hv: null

    function load(force) {
        if (!symbol) return;
        if (String(testScript).startsWith("options-synthetic")) { chain = syntheticChain(symbol); error = ""; return; }
        const id = ++seq;
        loading = true;
        error = "";
        api.invoke("options:chain", [symbol, !!force], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the options feed."; return; }
            chain = r.data;
        });
    }
    // 30-day realised volatility from real daily closes, to compare with implied volatility. Null without market
    // data: sample prices would make the comparison meaningless. (upstream data/options.ts fetchHv30)
    function loadHv() {
        hv = null;
        if (!live || !symbol) return;
        const want = symbol;
        const from = new Date(Date.now() - 100 * 86400000).toISOString().slice(0, 10);
        api.invoke("fmp:bars", [{ symbol: want, interval: "1day", from: from }], function (r) {
            if (want !== symbol || !r || !r.ok || !r.data.length) return;
            const bars = r.data.slice(-core.barCount("3M", "1day"));
            hv = core.callPure("historicalVol", [bars.map(function (b) { return b.close; }), 30]);
        });
    }
    onSymbolChanged: { chain = null; exp = ""; load(false); loadHv(); }
    onLiveChanged: loadHv()
    Timer { interval: 60000; running: view.symbol !== "" && testScript !== "options-synthetic"; repeat: true; onTriggered: view.load(false) }

    // ------------------------------------------------------------------ the selected expiration and its numbers
    property string exp: ""
    property string span: "8"
    property bool greeks: String(testScript).startsWith("options-synthetic")
    function defaultExp(c) { return (c.expirations.find(function (e) { return e.dte >= 7; }) || c.expirations[0] || { date: "" }).date; }
    // a chain refresh can drop the expiration being viewed (e.g. it expired)
    onChainChanged: if (chain && !chain.expirations.some(function (e) { return e.date === exp; })) exp = defaultExp(chain)

    readonly property real price: chain ? chain.price : 0
    readonly property var cs: chain && exp ? core.callPure("contractsFor", [chain, exp]) : []
    readonly property real dte: chain ? ((chain.expirations.find(function (e) { return e.date === exp; }) || { dte: 0 }).dte) : 0
    readonly property var all: chain ? core.callPure("activity", [chain.contracts]) : null
    readonly property var one: core.callPure("activity", [cs])
    readonly property var iv: chain ? core.callPure("atmIv", [cs, price]) : null
    readonly property var sd: iv !== null && iv !== undefined ? core.callPure("expectedMove", [price, iv, dte]) : null
    readonly property var st: chain ? core.callPure("straddle", [cs, price]) : null
    readonly property var pain: core.callPure("maxPain", [cs])
    readonly property var rows: chain ? core.callPure("strikeRows", [cs, price, span === "all" ? null : Number(span)]) : []
    readonly property var atm: chain ? core.callPure("atmStrike", [cs, price]) : null
    readonly property var busiest: chain ? chain.contracts.filter(function (c) { return c.volume > 0; }).sort(function (a, b) { return b.volume - a.volume; }).slice(0, 8) : []
    readonly property var ivHv: chain && chain.iv30 !== null && hv !== null && hv > 0 ? chain.iv30 / hv : null
    readonly property var term: chain ? core.callPure("termStructure", [chain]) : []

    function pc(n, d) { return n === null || n === undefined ? "—" : (n * 100).toFixed(d === undefined ? 1 : d) + "%"; }
    function f2(n) { return n === null || n === undefined ? "—" : n.toFixed(2); }
    function f3(n) { return n === null || n === undefined ? "—" : n.toFixed(3); }
    function n0(n) { return n === null || n === undefined ? "—" : Util.num(n, 0); }
    function fx(n, d) { return n === null || n === undefined ? "—" : n.toFixed(d); }
    function itm(c) { return !!c && (c.type === "C" ? c.strike < price : c.strike > price); }
    function midOf(c) { return c.bid > 0 && c.ask > 0 ? (c.bid + c.ask) / 2 : (c.last ?? c.theo ?? 0); }

    function explain() {
        app.askClaude("Explain the " + symbol + " options chain to me as a beginner. Use get_options_chain (and get_quote) to read the real data, then walk me through what the implied volatility, expected move, put/call ratios and the near-the-money contracts are telling us, and what I should be careful about. Remember I can only paper-trade stocks here, so keep it educational.");
    }

    // W95_TRADINGLAB_TEST=options-synthetic: a made-up chain (the Cboe feed refuses some networks), to check the screen
    function syntheticChain(sym) {
        const S = 184.3, now = Date.now() / 1000;
        const dtes = [3, 10, 17, 31, 59, 94, 185];
        const ncdf = function (x) { const t = 1 / (1 + 0.2316419 * Math.abs(x)); const d = 0.3989423 * Math.exp(-x * x / 2);
                                    const p = d * t * (0.3193815 + t * (-0.3565638 + t * (1.781478 + t * (-1.821256 + t * 1.330274)))); return x > 0 ? 1 - p : p; };
        const npdf = function (x) { return Math.exp(-x * x / 2) / Math.sqrt(2 * Math.PI); };
        const expirations = dtes.map(function (d) { return { date: new Date((now + d * 86400) * 1000).toISOString().slice(0, 10), dte: d }; });
        const contracts = [];
        let k = 0;
        expirations.forEach(function (e, ei) {
            const T = e.dte / 365;
            for (let K = 150; K <= 220; K += 2.5) {
                const m = Math.log(K / S);
                const sigma = 0.24 + 0.03 * Math.exp(-e.dte / 20) + 0.5 * m * m + (ei === 1 ? 0.04 : 0); // an "earnings" bump on the second expiry
                const d1 = (Math.log(S / K) + 0.5 * sigma * sigma * T) / (sigma * Math.sqrt(T)), d2 = d1 - sigma * Math.sqrt(T);
                ["C", "P"].forEach(function (type) {
                    const theo = type === "C" ? S * ncdf(d1) - K * ncdf(d2) : K * ncdf(-d2) - S * ncdf(-d1);
                    const spread = Math.max(0.02, theo * 0.04);
                    const near = Math.exp(-m * m * 120);
                    k = (k * 1103515245 + 12345) % 2147483648;
                    const noise = 0.6 + (k / 2147483648) * 0.8;
                    contracts.push({
                        exp: e.date, type: type, strike: K, bid: Math.max(0, theo - spread / 2), ask: theo + spread / 2, last: theo, theo: theo, change: null,
                        volume: Math.round(4000 * near * noise / (1 + ei * 0.6)), oi: Math.round(15000 * near * noise / (1 + ei * 0.4) + (K % 10 === 0 ? 3000 : 0)),
                        iv: sigma, delta: type === "C" ? ncdf(d1) : ncdf(d1) - 1, gamma: npdf(d1) / (S * sigma * Math.sqrt(T)),
                        theta: -(S * npdf(d1) * sigma) / (2 * Math.sqrt(T)) / 365, vega: S * npdf(d1) * Math.sqrt(T) / 100
                    });
                });
            }
        });
        return { symbol: sym, price: S, change: 1.22, changePct: 0.66, iv30: 0.262, fetchedAt: now, expirations: expirations, contracts: contracts, source: "synthetic test chain" };
    }


    SymbolHeader {
        id: header
        app: view.app
        width: view.width
    }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        loading: view.loading && !view.chain
        error: view.error ? "Could not load the option chain: " + view.error : ""
        asOf: view.chain ? view.chain.fetchedAt : 0
        asOfLabel: "prices are delayed about 15 minutes · " + (view.chain ? view.chain.source : "Cboe")
        refreshing: view.loading
        onRetry: view.load(false)
        onRefresh: view.load(true)

        SvNote {
            visible: view.error !== "" && !view.chain
            text: "Option chains come from Cboe's free delayed-quote feed (FMP has no options data), not from your FMP key. The feed sometimes turns requests away, for example from some networks or VPNs, or when it is busy. Try again in a while; everything else in Trading Lab keeps working."
        }

        Notice {
            visible: !!view.chain
            width: page.contentWidth
            kind: "info"
            text: view.chain ? "Learning view: the paper account trades stocks, not options. One contract covers 100 shares, and quoted prices are per share." : ""
            buttons: [{ text: "Ask Claude to explain this", action: function () { view.explain(); } }]
        }

        Grid {
            id: grid
            visible: !!view.chain
            width: page.contentWidth
            columns: 2
            spacing: 8
            readonly property real cardWidth: (width - spacing) / 2

            SvCard {
                id: volCard
                width: grid.cardWidth
                height: Math.max(implicitHeight, actCard.implicitHeight)
                title: "Volatility & expected move"
                sub: view.exp + " · " + view.dte + " days"
                SvKV {
                    width: parent.width
                    entries: view.chain ? {
                        "Stock price": Util.usd(view.price) + (view.chain.changePct !== null ? "  (" + (view.chain.changePct >= 0 ? "+" : "") + view.chain.changePct.toFixed(2) + "%)" : ""),
                        "Implied vol (30-day)": view.pc(view.chain.iv30), "Historical vol (30-day)": view.pc(view.hv),
                        "IV vs realised": view.ivHv !== null ? view.ivHv.toFixed(2) + "×  " + (view.ivHv > 1.15 ? "(options look pricey)" : view.ivHv < 0.85 ? "(options look cheap)" : "(in line)") : "—",
                        "ATM implied vol": view.pc(view.iv),
                        "Expected move": view.sd !== null ? "±" + Util.usd(view.sd) + " (" + ((view.sd / view.price) * 100).toFixed(1) + "%)" : "—",
                        "Range by expiry": view.sd !== null ? Util.usd(view.price - view.sd) + " – " + Util.usd(view.price + view.sd) : "—",
                        "ATM straddle": view.st ? Util.usd(view.st.cost) + " @ " + view.st.strike : "—"
                    } : {}
                }
                SvNote { text: "Expected move is the one-standard-deviation range the options market implies by the selected expiration; price finishes inside it about two times in three." }
            }

            SvCard {
                id: actCard
                width: grid.cardWidth
                title: "Activity"
                sub: "whole chain · selected expiration"
                SvKV {
                    width: parent.width
                    entries: view.chain && view.all ? {
                        "Put/call ratio (volume)": view.fx(view.all.pcVolume, 2) + "  ·  " + view.fx(view.one.pcVolume, 2),
                        "Put/call ratio (open interest)": view.fx(view.all.pcOi, 2) + "  ·  " + view.fx(view.one.pcOi, 2),
                        "Call volume": Util.compact(view.all.callVolume) + "  ·  " + Util.compact(view.one.callVolume),
                        "Put volume": Util.compact(view.all.putVolume) + "  ·  " + Util.compact(view.one.putVolume),
                        "Call open interest": Util.compact(view.all.callOi) + "  ·  " + Util.compact(view.one.callOi),
                        "Put open interest": Util.compact(view.all.putOi) + "  ·  " + Util.compact(view.one.putOi),
                        "Max pain (this expiry)": view.pain !== null && view.pain !== undefined ? Util.usd(view.pain) : "—"
                    } : {}
                }
                SvNote { text: "A put/call ratio above 1 means more put than call activity, often read as caution. It is a crowd gauge, not a forecast." }
            }

            SvCard {
                width: grid.cardWidth
                height: Math.max(implicitHeight, oiCard.implicitHeight)
                title: "Implied volatility by expiration"
                sub: "at-the-money"
                Label95 { visible: view.term.length < 2; dim: true; text: "Not enough expirations." }
                Canvas {
                    id: termCanvas
                    visible: view.term.length >= 2
                    width: parent.width
                    height: 170
                    readonly property var pts: view.term
                    readonly property string selected: view.exp
                    onPtsChanged: requestPaint()
                    onSelectedChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(0, 0, width, height);
                        if (pts.length < 2) return;
                        const W = width, H = height, L = 40, R = 12, T = 12, B = 24;
                        const maxD = Math.max.apply(null, pts.map(function (p) { return p.dte; })) || 1;
                        const lo = Math.min.apply(null, pts.map(function (p) { return p.iv; })), hi = Math.max.apply(null, pts.map(function (p) { return p.iv; }));
                        const pad = (hi - lo) * 0.15 || 0.02;
                        const x = function (d) { return L + (d / maxD) * (W - L - R); };
                        const y = function (iv) { return T + (1 - (iv - (lo - pad)) / (hi - lo + 2 * pad)) * (H - T - B); };
                        ctx.font = "10px 'Liberation Sans'";
                        ctx.lineWidth = 1;
                        [lo, (lo + hi) / 2, hi].forEach(function (v) {
                            ctx.strokeStyle = "#d8d8d8";
                            ctx.beginPath(); ctx.moveTo(L, Math.round(y(v)) + 0.5); ctx.lineTo(W - R, Math.round(y(v)) + 0.5); ctx.stroke();
                            ctx.fillStyle = "#606060"; ctx.textAlign = "right"; ctx.fillText((v * 100).toFixed(0) + "%", L - 6, y(v) + 3);
                        });
                        ctx.strokeStyle = "#000080";
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        pts.forEach(function (p, i) { if (i) ctx.lineTo(x(p.dte), y(p.iv)); else ctx.moveTo(x(p.dte), y(p.iv)); });
                        ctx.stroke();
                        ctx.lineWidth = 1;
                        pts.forEach(function (p) {
                            const sel = p.date === selected;
                            ctx.fillStyle = sel ? "#ff8000" : "#000080";
                            ctx.beginPath(); ctx.arc(x(p.dte), y(p.iv), sel ? 5 : 3, 0, Math.PI * 2); ctx.fill();
                        });
                        ctx.fillStyle = "#606060";
                        ctx.textAlign = "left"; ctx.fillText(pts[0].dte + "d", L, H - 6);
                        ctx.textAlign = "right"; ctx.fillText(maxD + "d", W - R, H - 6);
                    }
                }
                SvNote { text: "Rising to the right is normal. A curve sloping down (near-term above later) often signals expected news, such as earnings." }
            }

            SvCard {
                id: oiCard
                width: grid.cardWidth
                title: "Open interest by strike"
                sub: view.exp
                readonly property var oiRows: view.chain ? core.callPure("strikeRows", [view.cs, view.price, 14]) : []
                Label95 { visible: oiCard.oiRows.length < 2; dim: true; text: "No open interest data." }
                Canvas {
                    id: oiCanvas
                    visible: oiCard.oiRows.length >= 2
                    width: parent.width
                    height: 170
                    readonly property var rows: oiCard.oiRows
                    onRowsChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        ctx.fillStyle = "#ffffff";
                        ctx.fillRect(0, 0, width, height);
                        if (rows.length < 2) return;
                        const W = width, H = height, L = 8, R = 8, T = 10, B = 22;
                        const oi = function (c) { return c ? c.oi : 0; };
                        const max = Math.max(1, Math.max.apply(null, rows.map(function (r) { return Math.max(oi(r.call), oi(r.put)); })));
                        const step = (W - L - R) / rows.length, bw = Math.max(2, step * 0.38);
                        const xc = function (i) { return L + i * step + step / 2; };
                        const yb = function (v) { return T + (1 - v / max) * (H - T - B); };
                        const posOf = function (k) {
                            const i = rows.findIndex(function (r) { return r.strike >= k; });
                            if (i < 0) return xc(rows.length - 1);
                            return i === 0 ? xc(0) : xc(i - 1) + ((k - rows[i - 1].strike) / (rows[i].strike - rows[i - 1].strike)) * step;
                        };
                        const labelEvery = Math.ceil(rows.length / 8);
                        ctx.font = "10px 'Liberation Sans'";
                        ctx.textAlign = "center";
                        rows.forEach(function (r, i) {
                            ctx.fillStyle = "#26a69a"; ctx.fillRect(xc(i) - bw - 0.5, yb(oi(r.call)), bw, H - B - yb(oi(r.call)));
                            ctx.fillStyle = "#ef5350"; ctx.fillRect(xc(i) + 0.5, yb(oi(r.put)), bw, H - B - yb(oi(r.put)));
                            if (i % labelEvery === 0) { ctx.fillStyle = "#606060"; ctx.fillText(String(r.strike), xc(i), H - 6); }
                        });
                        ctx.strokeStyle = "#000000"; ctx.beginPath(); ctx.moveTo(L, H - B + 0.5); ctx.lineTo(W - R, H - B + 0.5); ctx.stroke();
                        const mark = function (k, color, label, ty, dash) {
                            const px = Math.round(posOf(k)) + 0.5;
                            ctx.strokeStyle = color; ctx.setLineDash(dash);
                            ctx.beginPath(); ctx.moveTo(px, T); ctx.lineTo(px, H - B); ctx.stroke();
                            ctx.setLineDash([]);
                            ctx.fillStyle = color; ctx.textAlign = "left"; ctx.fillText(label, px + 4, ty);
                        };
                        mark(view.price, "#0050d0", "price", T + 8, [4, 3]);
                        if (view.pain !== null && view.pain !== undefined) mark(view.pain, "#c07000", "max pain", T + 20, [2, 3]);
                    }
                }
                Row {
                    spacing: 6
                    Rectangle { width: 10; height: 10; anchors.verticalCenter: parent.verticalCenter; color: "#26a69a" }
                    Label95 { dim: true; text: "calls" }
                    Rectangle { width: 10; height: 10; anchors.verticalCenter: parent.verticalCenter; color: "#ef5350" }
                    Label95 { dim: true; text: "puts." }
                }
                SvNote { text: "Big clusters of open interest can act as magnets or barriers into expiration." }
            }
        }

        SvCard {
            visible: !!view.chain
            width: page.contentWidth
            title: "Most active contracts"
            sub: "by volume today, all expirations · click one to show its expiration"
            SvTable {
                width: parent.width
                model: view.busiest
                emptyText: "No contracts traded today."
                columns: [
                    { title: "Contract", flex: true, min: 150, bold: true, text: function (c) { return core.callPure("contractLabel", [c]); }, color: function (c) { return c.type === "C" ? "#007000" : "#b00000"; } },
                    { title: "Days", w: 44, right: true, text: function (c) { return (view.chain.expirations.find(function (e) { return e.date === c.exp; }) || { dte: "" }).dte; } },
                    { title: "Last", w: 56, right: true, text: function (c) { return view.f2(c.last ?? view.midOf(c)); } },
                    { title: "Volume", w: 64, right: true, text: function (c) { return view.n0(c.volume); } },
                    { title: "Open int.", w: 70, right: true, text: function (c) { return view.n0(c.oi); } },
                    { title: "Vol / OI", w: 60, right: true, tip: "Volume above open interest means new positions are being opened today",
                      text: function (c) { return c.oi ? (c.volume / c.oi).toFixed(2) : "—"; }, color: function (c) { return c.oi && c.volume / c.oi > 1 ? "#007000" : Theme.text; } },
                    { title: "IV", w: 56, right: true, text: function (c) { return view.pc(c.iv); } }
                ]
                onCellClicked: (row, c) => view.exp = c.exp
            }
        }

        SvCard {
            visible: !!view.chain
            width: page.contentWidth
            title: "Option chain"
            Row {
                spacing: 8
                ComboBox95 {
                    width: 150
                    popupMenu: popup
                    model: view.chain ? view.chain.expirations.map(function (e) { return e.date + " · " + e.dte + "d"; }) : []
                    currentIndex: view.chain ? view.chain.expirations.findIndex(function (e) { return e.date === view.exp; }) : -1
                    onActivated: (i) => view.exp = view.chain.expirations[i].date
                }
                ComboBox95 {
                    width: 110
                    popupMenu: popup
                    readonly property var spans: ["6", "8", "15", "all"]
                    model: ["±6 strikes", "±8 strikes", "±15 strikes", "All strikes"]
                    currentIndex: spans.indexOf(view.span)
                    onActivated: (i) => view.span = spans[i]
                }
                CheckBox95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Greeks"
                    checked: view.greeks
                    onToggled: (c) => view.greeks = c
                }
            }
            SvTable {
                id: chainTable
                width: parent.width
                model: view.rows
                readonly property var callFill: function (r) { return view.itm(r.call) ? "#fff4cc" : ""; }
                readonly property var putFill: function (r) { return view.itm(r.put) ? "#fff4cc" : ""; }
                function col(title, side, f, tip) {
                    return { title: title, w: 52, right: true, tip: tip || "", fill: side === "call" ? callFill : putFill, text: function (r) { return f(r[side]); } };
                }
                readonly property var greekCols: view.greeks
                columns: {
                    const g = greekCols;
                    const c = [];
                    if (g) c.push(col("Vega", "call", function (x) { return x ? view.f3(x.vega) : "—"; }, "Vega: change in price per 1-point change in IV"),
                                  col("Theta", "call", function (x) { return x ? view.f3(x.theta) : "—"; }, "Theta: price lost per day from time passing"),
                                  col("Gamma", "call", function (x) { return x ? view.f3(x.gamma) : "—"; }, "Gamma: how fast delta changes"));
                    c.push(col("Delta", "call", function (x) { return x ? view.f2(x.delta) : "—"; }), col("IV", "call", function (x) { return x ? view.pc(x.iv) : "—"; }),
                           col("OI", "call", function (x) { return x ? view.n0(x.oi) : "—"; }), col("Vol", "call", function (x) { return x ? view.n0(x.volume) : "—"; }),
                           col("Ask", "call", function (x) { return x ? view.f2(x.ask) : "—"; }), col("Bid", "call", function (x) { return x ? view.f2(x.bid) : "—"; }));
                    c.push({ title: "Strike", w: 60, right: true, bold: true, text: function (r) { return r.strike; }, fill: function () { return "#e8e8e8"; } });
                    c.push(col("Bid", "put", function (x) { return x ? view.f2(x.bid) : "—"; }), col("Ask", "put", function (x) { return x ? view.f2(x.ask) : "—"; }),
                           col("Vol", "put", function (x) { return x ? view.n0(x.volume) : "—"; }), col("OI", "put", function (x) { return x ? view.n0(x.oi) : "—"; }),
                           col("IV", "put", function (x) { return x ? view.pc(x.iv) : "—"; }), col("Delta", "put", function (x) { return x ? view.f2(x.delta) : "—"; }));
                    if (g) c.push(col("Gamma", "put", function (x) { return x ? view.f3(x.gamma) : "—"; }), col("Theta", "put", function (x) { return x ? view.f3(x.theta) : "—"; }),
                                  col("Vega", "put", function (x) { return x ? view.f3(x.vega) : "—"; }));
                    return c;
                }
                rowFill: function (r) { return r.strike === view.atm ? "#dde8ff" : ""; }
            }
            Row {
                spacing: 8
                Label95 { bold: true; color: "#007000"; text: "◀ Calls" }
                Label95 { dim: true; text: "strike in the middle, the at-the-money row in blue" }
                Label95 { bold: true; color: "#b00000"; text: "Puts ▶" }
            }
            SvNote { text: "Shaded cells are in the money. Delta is roughly the share exposure per contract (0.50 ≈ 50 shares) and a rough chance of finishing in the money. Wide bid–ask gaps mean expensive trading." }
        }
    }
}
