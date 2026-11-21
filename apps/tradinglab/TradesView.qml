import QtQuick
import Win95Ui

// The Active Trades screen: the active paper account's open positions (close at market), working orders (cancel),
// order history and fills. Clicking a symbol opens its chart. Orders are only ever changed by these buttons (and the
// chart's order lines), never by Claude. Port of renderer/src/views/TradesView.tsx.
Item {
    id: view
    property var app

    readonly property var snap: app ? app.snap : null
    readonly property var working: snap ? snap.orders.filter(function (o) { return o.status === "working" || o.status === "pending"; }) : []
    readonly property var history: snap ? snap.orders.filter(function (o) { return o.status !== "working" && o.status !== "pending"; }) : []
    readonly property var role: ({ entry: "Entry", target: "Target", stop: "Stop", oco_a: "OCO", oco_b: "OCO", single: "" })

    function when(t) { return t ? Util.dateTime(t) : ""; }
    function priceText(o) {
        if (o.type === "market") return "MKT";
        if (o.type === "limit") return "LMT " + o.limit_price.toFixed(2);
        if (o.type === "stop") return "STP " + o.stop_price.toFixed(2);
        if (o.type === "stop_limit") return "STP " + o.stop_price.toFixed(2) + " / LMT " + o.limit_price.toFixed(2);
        return "TRAIL " + (o.trail_unit === "%" ? o.trail_amount + "%" : "$" + o.trail_amount);
    }
    function pl(n) { return n === null || n === undefined ? "—" : (n >= 0 ? "+" : "-") + core.money(Math.abs(n)); }
    function plColor(n) { return n === null || n === undefined ? Theme.text : Util.tone(n >= 0 ? 1 : -1); }
    function open(sym) { app.setSymbol(sym); app.setView("chart"); }

    function cancel(id) {
        api.invoke("trade:cancel", [id], function (r) {
            if (r.ok) app.toast("success", "Order cancelled");
            else app.toast("error", r.errors.join(" "), { title: "Could not cancel" });
            app.refreshTrading();
        });
    }
    function close(symbol) {
        app.confirm("Close your " + symbol + " position at market? This also cancels its working orders.", function () {
            api.invoke("trade:close", [symbol], function (r) {
                if (r.ok) app.toast("info", "Closing " + symbol + " at market…");
                else app.toast("error", r.errors.join(" "), { title: "Could not close " + symbol });
                app.refreshTrading();
            });
        });
    }

    // W95_TRADINGLAB_TEST=px-setup (tests only): paper orders, a second account, a deposit and two portfolios, so the
    // Account, Active Trades, Watchlists and Portfolio screens have something in them for the screenshots
    Timer { running: testScript === "px-setup"; interval: 1500; onTriggered: view.testSetup() }
    function testSetup() {
        const log = function (what) { return function (r) { console.log("px-setup", what, JSON.stringify(r)); }; };
        const o = function (symbol, side, qty, type, extra) { return Object.assign({ symbol: symbol, side: side, qty: qty, type: type, tif: "gtc", strategy: "single" }, extra || {}); };
        [o("AAPL", "buy", 50, "market"), o("MSFT", "buy", 20, "market"), o("TSLA", "sell_short", 10, "market"), o("AMD", "buy", 30, "market"),
         o("NVDA", "buy", 15, "limit", { limit: 95 }), o("AAPL", "sell", 50, "limit", { limit: 215 }), o("MSFT", "sell", 20, "stop", { stop: 120, tif: "day" }),
         o("META", "buy", 5, "limit", { limit: 100, strategy: "bracket", target: 140, protect: { type: "stop", stop: 90 } })]
            .forEach(function (spec) { api.invoke("trade:place", [spec], log("place " + spec.symbol)); });
        api.invoke("trade:place", [o("AMD", "sell", 30, "market")], log("place AMD sell"));
        const acct = app.snap ? app.snap.account : null;
        if (acct) {
            api.invoke("acct:update", [acct.id, { name: "Schwab brokerage (paper)", broker: "Charles Schwab", url: "https://client.schwab.com", type: "margin" }], log("acct:update"));
            api.invoke("acct:setBalance", [acct.id, 90000, "Balance set to match the real account"], log("acct:setBalance"));
        }
        api.invoke("acct:create", [{ name: "Fidelity IRA (paper)", broker: "Fidelity", url: "", type: "cash" }, 50000], log("acct:create"));
        api.invoke("wl:lists", [], function (l) { if (l.length) ["NVDA", "AMZN", "SPY"].forEach(function (s) { api.invoke("wl:add", [l[0].id, s]); }); });
        api.invoke("pf:create", ["Retirement", 25000], function (r) {
            if (!r.ok) return;
            [["VTI", "etf", 40, 30, 3410], ["AAPL", "stock", 20, 12, 2050], ["MSFT", "stock", 20, 8, 1985.6], ["FXAIX", "fund", 20, 0, 0]].forEach(function (x) {
                api.invoke("pf:addItem", [r.id, { symbol: x[0], kind: x[1] }], function (a) {
                    if (a.ok) api.invoke("pf:updateItem", [a.id, { targetPct: x[2], shares: x[3], cost: x[4] }], log("pf item " + x[0]));
                });
            });
        });
        api.invoke("pf:create", ["Growth (linked)", 10000], function (r) {
            if (!r.ok || !acct) return;
            api.invoke("pf:update", [r.id, { accountId: acct.id, mode: "total" }], log("pf link"));
            [["AAPL", 50], ["MSFT", 30], ["QQQ", 20]].forEach(function (x) {
                api.invoke("pf:addItem", [r.id, { symbol: x[0], kind: "stock" }], function (a) { if (a.ok) api.invoke("pf:updateItem", [a.id, { targetPct: x[1] }]); });
            });
        });
    }

    PxTitle { id: title; width: parent.width; title: "Active Trades" }

    Label95 {
        visible: !view.snap
        x: 8
        y: title.height + 8
        dim: true
        text: "Loading…"
    }

    Column {
        id: notes
        y: title.height + 4
        width: parent.width
        spacing: 4
        Notice {
            width: parent.width
            text: view.snap && view.snap.fillBars === null
                  ? "Your FMP plan has no intraday price bars, so only market orders can be filled (while the market is open). Limit, stop, bracket and OCO orders are not available." : ""
        }
        Notice {
            width: parent.width
            text: view.snap && view.snap.fillBars && view.snap.fillBars.seconds > 60
                  ? "Working orders are filled against " + view.snap.fillBars.label + " bars, because your FMP plan has no 1-minute bars. Fills are less exact than with 1-minute data." : ""
        }
    }

    TabWidget95 {
        id: tabs
        visible: view.snap !== null
        y: notes.y + notes.height + 4
        width: parent.width
        height: parent.height - y
        // W95_TRADINGLAB_TEST=px-tab-<n> (tests only): open that tab for a screenshot
        Component.onCompleted: if (testScript.indexOf("px-tab-") === 0) currentIndex = Number(testScript.slice(7))
        titles: view.snap ? ["Positions (" + view.snap.positions.length + ")", "Working orders (" + view.working.length + ")", "Order history", "Fills"] : []

        PxTable {
            rows: view.snap ? view.snap.positions : []
            emptyText: "No open positions."
            columns: [
                { title: "Symbol", w: 70, text: function (p) { return p.symbol; }, onClick: function (p) { view.open(p.symbol); } },
                { title: "Side", w: 50, text: function (p) { return p.qty > 0 ? "Long" : "Short"; } },
                { title: "Qty", w: 55, right: true, text: function (p) { return Math.abs(p.qty); } },
                { title: "Avg price", w: 80, right: true, text: function (p) { return p.avg.toFixed(2); } },
                { title: "Last", w: 75, right: true, text: function (p) { return p.mark !== null ? p.mark.toFixed(2) : "—"; } },
                { title: "Market value", flex: true, min: 100, right: true, text: function (p) { return p.marketValue !== null ? core.money(p.marketValue) : "—"; } },
                { title: "P/L", w: 100, right: true, text: function (p) { return view.pl(p.unrealized); }, color: function (p) { return view.plColor(p.unrealized); } },
                { title: "P/L %", w: 70, right: true, text: function (p) { return p.unrealizedPct !== null ? p.unrealizedPct.toFixed(2) + "%" : "—"; },
                  color: function (p) { return view.plColor(p.unrealizedPct); } },
                { title: "", w: 66, button: "Close", onClick: function (p) { view.close(p.symbol); } }
            ]
        }
        PxTable {
            rows: view.working
            emptyText: "No working orders."
            columns: [
                { title: "Symbol", w: 62, text: function (o) { return o.symbol; }, onClick: function (o) { view.open(o.symbol); } },
                { title: "Order", flex: true, min: 110,
                  text: function (o) { return core.sideLabel(o.side) + (view.role[o.role] ? " · " + view.role[o.role] : ""); },
                  color: function (o) { return Util.tone(o.side === "buy" || o.side === "buy_to_cover" ? 1 : -1); } },
                { title: "Qty", w: 45, right: true, text: function (o) { return o.qty; } },
                { title: "Price", w: 130, text: function (o) { return view.priceText(o); } },
                { title: "TIF", w: 40, text: function (o) { return o.tif.toUpperCase(); } },
                { title: "Status", w: 100, text: function (o) { return o.status === "pending" ? "Waits for entry" : "Working"; },
                  color: function (o) { return o.status === "pending" ? Theme.shadow : Theme.text; } },
                { title: "Placed", w: 125, text: function (o) { return view.when(o.created_at); } },
                { title: "By", w: 60, text: function (o) { return o.source === "claude" ? "Claude ✦" : "You"; } },
                { title: "", w: 30, button: "×", flat: true, tip: function () { return "Cancel order"; }, onClick: function (o) { view.cancel(o.id); } }
            ]
        }
        PxTable {
            rows: view.history
            emptyText: "No completed orders yet."
            columns: [
                { title: "Symbol", w: 62, text: function (o) { return o.symbol; } },
                { title: "Order", w: 170, text: function (o) { return core.sideLabel(o.side) + " " + core.typeLabel(o.type) + (view.role[o.role] ? " · " + view.role[o.role] : ""); } },
                { title: "Qty", w: 45, right: true, text: function (o) { return o.qty; } },
                { title: "Price", w: 120, text: function (o) { return view.priceText(o); } },
                { title: "Status", flex: true, min: 120, text: function (o) { return o.status + (o.reason ? " — " + o.reason : ""); }, tip: function (o) { return o.reason || ""; } },
                { title: "Fill", w: 65, right: true, text: function (o) { return o.fill_price !== null ? o.fill_price.toFixed(2) : ""; } },
                { title: "Time", w: 125, text: function (o) { return view.when(o.filled_at ?? o.created_at); } }
            ]
        }
        PxTable {
            rows: view.snap ? view.snap.fills : []
            emptyText: "No fills yet."
            columns: [
                { title: "Time", w: 135, text: function (f) { return view.when(f.time); } },
                { title: "Symbol", w: 70, text: function (f) { return f.symbol; } },
                { title: "Side", flex: true, min: 90, text: function (f) { return core.sideLabel(f.side); } },
                { title: "Qty", w: 55, right: true, text: function (f) { return f.qty; } },
                { title: "Price", w: 80, right: true, text: function (f) { return f.price.toFixed(2); } },
                { title: "Realized P/L", w: 110, right: true, text: function (f) { return f.realized_pl ? view.pl(f.realized_pl) : ""; },
                  color: function (f) { return view.plColor(f.realized_pl); } }
            ]
        }
    }
}
