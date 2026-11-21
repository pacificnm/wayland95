import QtQuick
import Win95Ui

// The Portfolio screen: plan an investment portfolio (stocks, ETFs and mutual funds, each with a target percentage of
// the money to invest), see how many shares that buys, record what you hold and what it is worth, and work out a sale
// of a percentage of it. A portfolio can follow a paper account: then its holdings and cash come from that account and
// two buttons send the plan's paper market orders, after a confirmation (the only orders this screen can send; the
// backend checks them again, and Claude can never call them). Unlinked, it is record keeping only.
// The maths is shared/portfolio.ts (core.callPure). Port of renderer/src/views/PortfolioView.tsx.
Item {
    id: view
    property var app

    readonly property bool live: app ? app.live : false
    property var lists: null
    property int activeId: -1
    property string tab: "plan"            // plan | sell | holdings
    property real sellPct: 10
    property string sellMethod: "proportional"
    property bool sellProfit: false
    property string addKind: "auto"
    property var prices: ({})
    property var names: ({})
    property var updated: null
    property string priceErr: ""
    property var accts: null               // {list, activeId}
    property var link: null
    property string linkErr: ""

    readonly property var kinds: core.callPure("kinds", [])
    readonly property var kindLabel: ({ stock: "Stock", etf: "ETF", fund: "Fund" })

    function shares(n) { return Number.isInteger(n) ? String(n) : n.toFixed(3).replace(/0+$/, "").replace(/\.$/, ""); }
    function signed(n) { return (n >= 0 ? "+" : "-") + core.money(Math.abs(n)); }
    function money(n) { return core.money(n); }
    function up(n) { return Util.tone(n >= 0 ? 1 : -1); }
    function plural(n, word) { return n + " " + word + (n === 1 ? "" : "s"); }

    // ---------------------------------------------------------------- data
    function load() {
        api.invoke("pf:list", [], function (l) {
            view.lists = l || [];
            // W95_TRADINGLAB_TEST=px-pf-<tab>-<n> (tests only): show that tab of the n-th portfolio for a screenshot
            if (testScript.indexOf("px-pf-") === 0 && view.lists.length > Number(testScript.split("-")[3] || 0))
                view.activeId = view.lists[Number(testScript.split("-")[3] || 0)].id;
        });
    }
    function loadAccts() { api.invoke("acct:list", [], function (r) { if (r && r.list) view.accts = { list: r.list, activeId: r.activeId }; }); }
    Component.onCompleted: {
        load();
        loadAccts();
        if (testScript.indexOf("px-pf-") === 0) view.tab = testScript.split("-")[2];
        else api.invoke("settings:get", ["pfActive"], function (id) { if (typeof id === "number") view.activeId = id; });
    }
    Connections {
        target: api
        function onEvent(channel, payload) {
            if (channel === "portfolio:changed") view.load();
            else if (channel === "trade:update") { view.loadAccts(); view.getLink(); }
        }
    }

    readonly property var active: {
        if (!lists) return null;
        for (let i = 0; i < lists.length; ++i) if (lists[i].id === activeId) return lists[i];
        return lists.length ? lists[0] : null;
    }
    readonly property var linkedId: active && active.accountId !== null && active.accountId !== undefined ? active.accountId : null

    // a linked portfolio follows its paper account: balances and positions now, then whenever orders fill and every 15 seconds
    onLinkedIdChanged: { link = null; linkErr = ""; getLink(); }
    function getLink() {
        const id = linkedId;
        if (id === null) return;
        api.invoke("pf:accountView", [id], function (r) {
            if (id !== view.linkedId) return;
            if (r.ok) { view.link = r.data; view.linkErr = ""; } else view.linkErr = r.error;
        });
    }
    Timer { interval: 15000; repeat: true; running: view.linkedId !== null; onTriggered: view.getLink() }

    // live prices: now, then every minute (fund prices only change once a day, but a stock can move any time)
    readonly property var symbols: active ? active.items.map(function (i) { return i.symbol; }) : []
    readonly property string symKey: symbols.join(",")
    onSymKeyChanged: getPrices()
    onLiveChanged: getPrices()
    Timer { interval: 60000; repeat: true; running: view.live && view.symbols.length > 0; onTriggered: view.getPrices() }
    function getPrices() {
        if (!live || symbols.length === 0) return;
        const want = symKey;
        api.invoke("fmp:quotes", [symbols], function (r) {
            if (want !== view.symKey) return;
            if (!r.ok) { view.priceErr = r.error; return; }
            view.priceErr = "";
            const p = Object.assign({}, view.prices), n = Object.assign({}, view.names);
            r.data.forEach(function (x) {
                const s = core.pickStr(x, ["symbol"]), v = core.pick(x, ["price"]);
                if (s && v !== null && v !== undefined && v > 0) p[s] = v;
                const nm = core.pickStr(x, ["name"]);
                if (s && nm) n[s] = nm;
            });
            view.prices = p;
            view.names = n;
            view.updated = Date.now();
        });
    }
    // remember each holding's name once the feed has told us
    onNamesChanged: rememberNames()
    onActiveChanged: rememberNames()
    function rememberNames() {
        (active ? active.items : []).forEach(function (it) { if (!it.name && names[it.symbol]) api.invoke("pf:updateItem", [it.id, { name: names[it.symbol] }]); });
    }

    // a linked portfolio is planned and valued from its account; an unlinked one from the numbers typed here
    readonly property var eff: active ? core.callPure("effectivePortfolio", [active, link, prices]) : null
    readonly property bool linked: active !== null && linkedId !== null && link !== null
    readonly property var pfv: eff ? eff.pf : active
    readonly property var plan: pfv ? core.callPure("planPortfolio", [pfv, prices]) : null
    readonly property var hold: pfv ? core.callPure("summarizeHoldings", [pfv.items, prices]) : null
    readonly property var sell: pfv ? core.callPure("planSell", [pfv, prices, { percent: sellPct, method: sellMethod, onlyProfit: sellProfit, sellable: linked && eff ? eff.sellable : null }]) : null
    readonly property var linkedAcct: {
        if (!accts || !active) return null;
        for (let i = 0; i < accts.list.length; ++i) if (accts.list[i].id === active.accountId) return accts.list[i];
        return null;
    }
    readonly property bool isActiveAcct: linkedAcct !== null && accts.activeId === linkedAcct.id
    readonly property string linkedName: linkedAcct ? Util.plain(linkedAcct.name) : ""
    // the i-th row of each calculation (null while it is not there), for the table cells
    function planRow(i) { return plan && i < plan.rows.length ? plan.rows[i] : null; }
    function sellRow(i) { return sell && i < sell.rows.length ? sell.rows[i] : null; }
    function holdRow(i) { return hold && i < hold.rows.length ? hold.rows[i] : null; }
    function eItem(i) { return pfv && i < pfv.items.length ? pfv.items[i] : null; }
    function nameOf(it) { return it.name || names[it.symbol] || ""; }
    readonly property bool needsPrice: active ? active.items.some(function (it) { return core.callPure("priceOf", [it, view.prices]) == null; }) : false
    readonly property int heldCount: hold ? hold.rows.filter(function (r) { return r.shares > 0; }).length : 0

    // ---------------------------------------------------------------- actions
    function fail(r) { if (!r.ok) app.toast("error", r.error || "That did not work."); return r.ok; }
    function call(channel, args, then) { api.invoke(channel, args, function (r) { if (view.fail(r) && then) then(r); }); }
    function choose(id) { activeId = id; api.invoke("settings:set", ["pfActive", id]); }

    function newPortfolio() {
        app.promptText({ title: "New portfolio", label: "Name", placeholder: "e.g. Retirement", okLabel: "Create", validate: function (v) { return v.trim() ? null : "Give it a name."; } }, function (name) {
            if (!name) return;
            view.call("pf:create", [name], function (r) { view.choose(r.id); });
        });
    }
    function rename() {
        if (!active) return;
        const pf = active;
        app.promptText({ title: "Rename portfolio", label: "Name", initial: pf.name, okLabel: "Rename", validate: function (v) { return v.trim() ? null : "Give it a name."; } }, function (name) {
            if (name) view.call("pf:update", [pf.id, { name: name }]);
        });
    }
    function remove() {
        if (!active) return;
        const pf = active;
        app.confirm("Delete the portfolio “" + pf.name + "” with its " + plural(pf.items.length, "holding") + "? This only removes your plan and records here, not anything at a broker.", function () {
            view.call("pf:remove", [pf.id]);
        });
    }
    function add(symbol) {
        if (!active) return;
        const kind = addKind === "auto" ? core.callPure("guessKind", [symbol.toUpperCase()]) : addKind;
        call("pf:addItem", [active.id, { symbol: symbol, kind: kind }]);
    }
    function setTargets(w) {
        if (!active) return;
        call("pf:setTargets", [active.id, active.items.map(function (it, i) { return { id: it.id, pct: w[i] ?? 0 }; })]);
    }
    function updatePf(patch) { if (active) call("pf:update", [active.id, patch]); }
    function updateItem(id, patch) { call("pf:updateItem", [id, patch]); }

    function recordBuys() {
        if (!active || !plan) return;
        const pf = active;
        const buys = plan.rows.filter(function (r) { return r.shares > 0 && r.price !== null; }).map(function (r) { return { id: r.id, shares: r.shares, price: r.price }; });
        if (buys.length === 0) return;
        const total = buys.reduce(function (s, b) { return s + b.shares * b.price; }, 0);
        app.confirm("Add " + plural(buys.length, "purchase") + " (about " + money(total) + ") to your holdings at today's prices?\n\nThis only updates the records in Trading Lab. It does not buy anything. Do it after you have actually bought them.", function () {
            view.call("pf:recordBuys", [pf.id, buys], function (r) {
                app.toast("success", "Recorded " + view.plural(r.recorded, "purchase") + " in your holdings.");
                view.tab = "holdings";
            });
        });
    }
    function placeResult(r, word, title) {
        if (!r.ok) { app.toast("error", r.error); return; }
        const done = r.outcomes.filter(function (o) { return o.ok; }), failed = r.outcomes.filter(function (o) { return !o.ok; });
        if (done.length)
            app.toast("success", "Sent " + done.length + " paper " + word + (done.length === 1 ? "" : "s")
                      + (done.some(function (o) { return o.filledNow; }) ? "" : ": they fill when the market data allows (see Active Trades)") + ".", { title: title });
        failed.forEach(function (f) { app.toast("error", f.symbol + ": " + f.error, { title: "Order not sent" }); });
    }
    function buyOrders() {
        if (!active || !plan || !linkedAcct) return;
        const pf = active, acct = linkedAcct;
        const buys = plan.rows.filter(function (r, i) { return r.shares > 0 && r.price !== null && !view.eItem(i).noBuy; })
                              .map(function (r) { return { symbol: r.symbol, qty: r.shares, price: r.price }; });
        if (buys.length === 0) return;
        const total = buys.reduce(function (s, b) { return s + b.qty * b.price; }, 0);
        app.confirm("Send " + plural(buys.length, "paper market order") + " to “" + acct.name + "” for about " + money(total) + "?\n\n"
                    + buys.map(function (b) { return "Buy " + b.qty + " " + b.symbol; }).join("\n")
                    + "\n\nThese are simulated orders in your paper account, filled from market data. They are not real trades.", function () {
            api.invoke("pf:placeOrders", [pf.id, "buy", buys.map(function (b) { return { symbol: b.symbol, qty: b.qty }; })], function (r) { view.placeResult(r, "order", "Portfolio plan"); });
        });
    }
    function sellOrders() {
        if (!active || !sell || !linkedAcct) return;
        const pf = active, acct = linkedAcct, s = sell;
        const rows = s.rows.filter(function (r) { return r.shares > 0 && r.price !== null; });
        if (rows.length === 0) return;
        app.confirm("Send " + plural(rows.length, "paper market sell order") + " to “" + acct.name + "” to raise about " + money(s.proceeds) + "?\n\n"
                    + rows.map(function (r) { return "Sell " + r.shares + " " + r.symbol; }).join("\n")
                    + "\n\nEstimated profit on these shares: " + signed(s.gain) + " (before any tax).\nThese are simulated orders in your paper account, filled from market data. They are not real trades.", function () {
            api.invoke("pf:placeOrders", [pf.id, "sell", rows.map(function (x) { return { symbol: x.symbol, qty: x.shares }; })], function (r) { view.placeResult(r, "sell order", "Portfolio sale"); });
        });
    }
    function recordSells() {
        if (!active || !sell) return;
        const pf = active, s = sell;
        const rows = s.rows.filter(function (r) { return r.shares > 0; });
        if (rows.length === 0) return;
        app.confirm("Take " + plural(rows.length, "sale") + " (about " + money(s.proceeds) + ") out of your holdings?\n\nThis only updates the records in Trading Lab. It does not sell anything. Do it after you have actually sold them.", function () {
            view.call("pf:recordSells", [pf.id, rows.map(function (x) { return { id: x.id, shares: x.shares }; })], function (r) {
                app.toast("success", "Recorded " + view.plural(r.recorded, "sale") + " in your holdings.");
                view.tab = "holdings";
            });
        });
    }
    function makeActive() { if (linkedAcct) api.invoke("acct:setActive", [linkedAcct.id], function (r) { if (!r.ok) app.toast("error", r.error); app.refreshTrading(); }); }
    function ask() {
        if (active) app.askClaude("Review my portfolio \"" + active.name + "\". Use get_portfolio to read it. Tell me how closely my holdings match my targets, whether it is concentrated or well spread, what stands out in the gains and losses, and what you would double-check before investing more. Be clear that this is education, not personal financial advice.");
    }
    function openChart(sym) { app.setSymbol(sym); app.setView("chart"); }

    // ---------------------------------------------------------------- layout
    PxTitle {
        id: title
        width: parent.width
        title: "Portfolio"
        PxButton { icon: "help"; text: "Ask Claude to review"; enabled: view.active !== null && view.active.items.length > 0; onClicked: view.ask() }
    }

    // the portfolios, as tabs
    Item {
        id: tabRow
        visible: view.lists !== null
        y: title.height + 4
        width: parent.width
        height: 24
        Row {
            spacing: 2
            Repeater {
                model: view.lists || []
                delegate: PxButton {
                    required property var modelData
                    height: 22
                    text: modelData.name + "  " + modelData.items.length
                    checked: view.active !== null && modelData.id === view.active.id
                    onClicked: view.choose(modelData.id)
                }
            }
            PxButton { width: 24; height: 22; icon: "add"; onClicked: view.newPortfolio(); ToolTipArea { text: "New portfolio" } }
        }
        Row {
            visible: view.active !== null
            anchors.right: parent.right
            spacing: 2
            PxButton { width: 24; height: 22; flat: true; icon: "edit"; onClicked: view.rename(); ToolTipArea { text: "Rename this portfolio" } }
            PxButton { width: 24; height: 22; flat: true; icon: "delete"; onClicked: view.remove(); ToolTipArea { text: "Delete this portfolio" } }
        }
    }

    PxPage {
        id: page
        visible: view.lists !== null
        y: tabRow.y + tabRow.height + 2
        width: parent.width
        height: parent.height - y

        // ------------------------------------------------ no portfolio yet
        Column {
            visible: view.active === null
            width: Math.min(page.contentWidth, 560)
            spacing: 8
            Item { width: 1; height: 20 }
            Heading95 { text: "Plan and track an investment portfolio" }
            Label95 { width: parent.width; wrapMode: Text.Wrap; dim: true; text: "Add stocks, ETFs and mutual funds, give each a percentage of the money you plan to invest, and Trading Lab works out how many shares that buys. Then record what you hold to see what it is worth today." }
            Label95 { width: parent.width; wrapMode: Text.Wrap; dim: true; text: "It is a planning and record-keeping tool. It never buys or sells anything, and it is separate from your paper trading accounts." }
            Button95 { text: "Create your first portfolio"; isDefault: true; width: 190; onClicked: view.newPortfolio() }
        }

        // ------------------------------------------------ notes
        Notice {
            width: page.contentWidth
            visible: view.active !== null && !view.live
            text: visible ? "No market data key is set, so prices cannot load. Type a price next to each holding to use the calculator, or add your key in Settings." : ""
            buttons: [{ text: "Settings", action: function () { view.app.setView("settings"); } }]
        }
        Notice { width: page.contentWidth; kind: "error"; text: view.active && view.live && view.priceErr ? "Prices could not be updated: " + Util.plain(view.priceErr) + ". Showing the last ones, or type a price by hand." : "" }
        Notice { width: page.contentWidth; kind: "error"; text: view.linkedId !== null && view.linkErr ? "Could not read the paper account: " + Util.plain(view.linkErr) : "" }
        Notice {
            width: page.contentWidth
            visible: view.linked && view.linkedAcct !== null && !view.isActiveAcct
            text: visible ? "This portfolio follows <b>" + view.linkedName + "</b>, which is not the active account. The values below come from it, but paper orders always go to the active account, so make " + view.linkedName + " active before buying." : ""
            buttons: [{ text: "Make active", action: function () { view.makeActive(); } }]
        }
        Notice { width: page.contentWidth; text: view.linked && view.eff && view.eff.shorts.length > 0 ? "The account holds a short position in " + view.eff.shorts.join(", ") + ". Shorts are not counted here." : "" }
        Notice {
            width: page.contentWidth
            text: view.linked && view.eff && Object.keys(view.eff.pendingSold).length > 0
                  ? "Paper market sells waiting to fill (" + Object.keys(view.eff.pendingSold).map(function (s) { return view.eff.pendingSold[s] + " " + s; }).join(", ") + ") are counted as already sold, so the plan will not sell them twice." : ""
        }
        Notice {
            width: page.contentWidth
            text: view.linked && view.eff && Object.keys(view.eff.pending).length > 0
                  ? "Paper market buys waiting to fill (" + Object.keys(view.eff.pending).map(function (s) { return view.eff.pending[s] + " " + s; }).join(", ") + ") are counted as already held, so the plan will not buy them twice." : ""
        }

        // ------------------------------------------------ totals
        Grid {
            id: cards
            visible: view.active !== null && view.plan !== null
            width: page.contentWidth
            columns: width >= 640 ? 4 : 2
            spacing: 6
            readonly property real cw: (width - (columns - 1) * spacing) / columns
            PxCard {
                width: cards.cw
                label: "Value of what you hold"
                value: view.hold ? view.money(view.hold.totalValue) : ""
                sub: view.plural(view.heldCount, "holding") + " with shares"
            }
            PxCard { width: cards.cw; label: "Total paid"; value: view.hold ? view.money(view.hold.totalCost) : ""; sub: "cost of those shares" }
            PxCard {
                width: cards.cw
                label: "Gain / loss"
                value: view.hold && (view.hold.totalCost > 0 || view.hold.totalValue > 0) ? view.signed(view.hold.gain) : "—"
                valueColor: view.hold ? view.up(view.hold.gain) : Theme.text
                sub: view.hold && view.hold.gainPct !== null ? (view.hold.gainPct >= 0 ? "+" : "") + view.hold.gainPct.toFixed(2) + "%" : " "
                subColor: view.hold ? view.up(view.hold.gain) : Theme.shadow
            }
            PxCard {
                width: cards.cw
                label: !view.active ? "" : view.linked ? "Cash to invest (" + (view.linkedAcct ? view.linkedAcct.name : "account") + ")" : "Plan for " + view.money(view.active.amount)
                value: view.plan ? view.money(view.plan.spent) : ""
                sub: view.plan ? "to spend · " + view.money(view.plan.leftover) + " left over" : ""
            }
        }
        Notice {
            width: page.contentWidth
            text: view.active && view.hold && view.hold.missingPrice.length > 0
                  ? "No price for " + view.hold.missingPrice.join(", ") + ", so " + (view.hold.missingPrice.length === 1 ? "it is" : "they are") + " left out of these totals." : ""
        }

        // ------------------------------------------------ settings of the plan
        GroupBox95 {
            visible: view.active !== null && view.plan !== null
            width: page.contentWidth
            height: controls.implicitHeight + 30
            title: "Plan"
            Flow {
                id: controls
                width: parent.width
                spacing: 14
                Row {
                    spacing: 6
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Paper account" }
                    ComboBox95 {
                        width: 230
                        popupMenu: menu
                        readonly property var ids: [null].concat(view.accts ? view.accts.list.map(function (a) { return a.id; }) : [])
                        model: ["Not linked (I type the numbers)"].concat(view.accts ? view.accts.list.map(function (a) { return a.name + (a.id === view.accts.activeId ? " (active)" : ""); }) : [])
                        currentIndex: Math.max(0, ids.indexOf(view.linkedId))
                        onActivated: (i) => view.updatePf({ accountId: ids[i] })
                        ToolTipArea { text: "Link this portfolio to a paper account so its balance, holdings and value come from that account" }
                    }
                }
                // linked: the account's cash is what gets invested
                Row {
                    visible: view.linked && view.eff !== null
                    spacing: 6
                    height: 21
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Invests the account's available cash" }
                    Label95 { anchors.verticalCenter: parent.verticalCenter; bold: true; text: view.eff ? view.money(view.eff.availableCash) : "" }
                    Label95 {
                        anchors.verticalCenter: parent.verticalCenter
                        dim: true
                        text: view.plan && view.pfv ? "holdings " + view.money(view.plan.holdingsValue) + " + cash = " + view.money(view.pfv.amount) : ""
                    }
                }
                // unlinked: the amount typed here
                Row {
                    visible: !view.linked
                    spacing: 6
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Investment amount" }
                    PxNum { value: view.active ? view.active.amount : null; prefix: "$"; fieldWidth: 100; onCommitted: (n) => view.updatePf({ amount: n }) }
                }
                Row {
                    visible: !view.linked
                    spacing: 6
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: "The amount is" }
                    PxSeg {
                        options: [{ id: "new", text: "New money", tip: "New money to invest now, split by your percentages" },
                                  { id: "total", text: "Total size", tip: "The size you want the whole portfolio to reach: what you hold now counts toward it" }]
                        current: view.active ? view.active.mode : ""
                        onPicked: (id) => view.updatePf({ mode: id })
                    }
                }
                Row {
                    visible: !view.linked
                    spacing: 4
                    height: 21
                    CheckBox95 {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Allow fractional shares of stocks and ETFs"
                        checked: view.active ? view.active.fractional : false
                        onToggled: (c) => view.updatePf({ fractional: c })
                    }
                    Label95 { anchors.verticalCenter: parent.verticalCenter; dim: true; text: "(funds always can)" }
                }
                CheckBox95 {
                    height: 21
                    text: "Spend leftover cash on extra shares"
                    checked: view.active ? view.active.leftover : false
                    onToggled: (c) => view.updatePf({ leftover: c })
                }
                Row {
                    spacing: 6
                    Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Targets add up to" }
                    Label95 {
                        anchors.verticalCenter: parent.verticalCenter
                        bold: true
                        text: view.plan ? view.plan.pctTotal + "%" : ""
                        color: view.plan && Math.abs(view.plan.pctTotal - 100) < 0.005 ? Util.tone(1) : Util.tone(-1)
                    }
                    PxButton {
                        text: "Equal weight"
                        enabled: view.active !== null && view.active.items.length > 0
                        onClicked: view.setTargets(core.callPure("equalWeights", [view.active.items.length]))
                    }
                    PxButton {
                        text: "Make it 100%"
                        enabled: view.active !== null && view.active.items.length > 0 && view.plan.pctTotal !== 0
                        onClicked: view.setTargets(core.callPure("normalizeWeights", [view.active.items.map(function (i) { return i.targetPct; })]))
                    }
                }
            }
        }

        // ------------------------------------------------ add a holding, choose the tab
        Item {
            visible: view.active !== null && view.plan !== null
            z: 10
            width: page.contentWidth
            height: 24
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                PxSymbolSearch {
                    id: pfSearch
                    width: 190
                    height: 22
                    live: view.live
                    onPicked: (s) => view.add(s)
                    // W95_TRADINGLAB_TEST=px-search (tests only): show the suggestions for a screenshot
                    Timer { running: testScript === "px-search"; interval: 2500; onTriggered: pfSearch.typeText("A") }
                }
                ComboBox95 {
                    width: 130
                    height: 22
                    popupMenu: menu
                    readonly property var ids: ["auto"].concat(view.kinds.map(function (k) { return k.id; }))
                    model: ["Auto-detect type"].concat(view.kinds.map(function (k) { return k.label; }))
                    currentIndex: Math.max(0, ids.indexOf(view.addKind))
                    onActivated: (i) => view.addKind = ids[i]
                    ToolTipArea { text: "What kind of holding to add. Auto-detect treats a five-letter ticker ending in X as a mutual fund." }
                }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(0, page.contentWidth - 190 - 130 - tabSeg.width - 30)
                    elide: Text.ElideRight
                    dim: true
                    text: "Search for a stock, ETF or mutual fund, and press Enter to add it"
                }
            }
            PxSeg {
                id: tabSeg
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                options: [{ id: "plan", text: "Plan & calculator" }, { id: "sell", text: "Sell", tip: "Sell a percentage of the portfolio to take profits" }, { id: "holdings", text: "Holdings" }]
                current: view.tab
                onPicked: (id) => view.tab = id
            }
        }

        Label95 {
            visible: view.active !== null && view.active.items.length === 0
            dim: true
            text: "Nothing here yet. Search above to add your first stock, ETF or mutual fund."
        }

        // ------------------------------------------------ Plan & calculator
        Column {
            visible: view.active !== null && view.active.items.length > 0 && view.tab === "plan" && view.plan !== null
            width: page.contentWidth
            spacing: 6
            PxTable {
                width: parent.width
                autoHeight: true
                rowHeight: 34
                rows: view.active ? view.active.items : []
                columns: [
                    { title: "Holding", flex: true, min: 140, delegate: holdingCell },
                    { title: "Target %", w: 82, right: true, num: { value: function (it) { return it.targetPct; }, suffix: "%", width: 50, commit: function (it, i, n) { view.updateItem(it.id, { targetPct: n }); } } },
                    { title: "Price", w: 96, right: true, delegate: priceCell },
                    { title: view.pfv && view.pfv.mode === "new" ? "Amount for it" : "Needs", w: 86, right: true,
                      text: function (it, i) { const r = view.planRow(i); return !r || r.price === null || view.eItem(i).noBuy ? "—" : view.money(r.dollars); } },
                    { title: "Shares to buy", w: 96, right: true,
                      text: function (it, i) {
                          const r = view.planRow(i);
                          if (!r) return "";
                          const t = view.eItem(i).noBuy ? "can't buy here" : r.price === null ? "no price" : view.shares(r.shares);
                          return t + (r.extra > 0 ? " (+" + view.shares(r.extra) + ")" : "");
                      },
                      bold: function (it, i) { const r = view.planRow(i); return r && !view.eItem(i).noBuy && r.price !== null; },
                      color: function (it, i) { const r = view.planRow(i); return r && (view.eItem(i).noBuy || r.price === null) ? Theme.shadow : Theme.text; },
                      sub: function (it, i) { const r = view.planRow(i); return r && r.overValue > 0 ? "over target by " + view.money(r.overValue) : ""; },
                      tip: function (it, i) {
                          const r = view.planRow(i);
                          if (!r) return "";
                          if (view.eItem(i).noBuy) return "Mutual funds cannot be traded in the paper account, so the plan does not buy them. Buy the fund yourself and type its shares on the Holdings tab.";
                          if (r.price === null) return "Needs a price";
                          if (r.overValue > 0) return "Total-size mode never sells for you. This is only a comparison.";
                          return r.extra > 0 ? "Includes shares bought with leftover cash" : "";
                      } },
                    { title: "Cost", w: 82, right: true, text: function (it, i) { const r = view.planRow(i); return r && r.shares > 0 ? view.money(r.cost) : "—"; } },
                    { title: "You hold", w: 72, right: true,
                      text: function (it, i) { const e = view.eItem(i); return e && e.shares > 0 ? view.shares(e.shares) : "—"; },
                      sub: function (it) { return view.eff && view.eff.pending[it.symbol] ? "incl. " + view.eff.pending[it.symbol] + " waiting to fill" : ""; } },
                    { title: "After buying", w: 92, right: true,
                      text: function (it, i) {
                          const r = view.planRow(i), e = view.eItem(i);
                          if (!r || r.price === null) return "—";
                          const after = e.shares + r.shares, afterTotal = view.plan.holdingsValue + view.plan.spent;
                          const w = afterTotal > 0 ? after * r.price / afterTotal * 100 : null;
                          return view.shares(after) + " · " + (w !== null ? w.toFixed(1) : "0") + "%";
                      } },
                    { title: "", w: 28, button: "×", flat: true, tip: function (it) { return "Remove " + it.symbol; }, onClick: function (it) { view.call("pf:removeItem", [it.id]); } }
                ]
                footer: view.plan ? [
                    { text: "Total", bold: true },
                    { text: view.plan.pctTotal + "%", color: Math.abs(view.plan.pctTotal - 100) < 0.005 ? Theme.text : Util.tone(-1) },
                    null,
                    view.money(view.plan.rows.reduce(function (s, r) { return s + (r.price !== null ? r.dollars : 0); }, 0)),
                    null,
                    { text: view.money(view.plan.spent), bold: true },
                    { text: view.money(view.plan.leftover) + " of " + view.money(view.plan.budget) + " left over", color: Theme.shadow, left: true, span: 3 },
                    null, null
                ] : null
            }
            Notice {
                width: parent.width
                kind: "error"
                text: view.plan && view.plan.pctTotal > 100.005 ? "Your targets add up to " + view.plan.pctTotal + "%, more than 100%, so the plan wants more than the amount. Shares are rounded down, so you may overspend nothing, but fix the percentages or use “Make it 100%”." : ""
            }
            Notice {
                width: parent.width
                text: view.plan && view.active && view.plan.pctTotal < 99.995 && view.active.items.length > 0
                      ? "Your targets add up to " + view.plan.pctTotal + "%, so about " + view.money(Math.max(0, view.active.amount * (100 - view.plan.pctTotal) / 100)) + " is not assigned to anything." : ""
            }
            Notice {
                width: parent.width
                text: {
                    if (!view.linked || !view.linkedAcct || !view.plan) return "";
                    let t = "Linked to <b>" + view.linkedName + "</b>: the plan tops up whatever is under target using only that account's available cash (" + view.money(view.eff ? view.eff.availableCash : 0)
                            + "). Your holdings are the account's positions, so once the orders fill the plan has nothing left to buy. Mutual funds cannot trade in the paper account: their shares are the ones you typed, they count toward the totals, and they are never bought here.";
                    view.plan.rows.forEach(function (r, i) {
                        const e = view.eItem(i);
                        const under = e.noBuy && e.targetPct > 0 ? view.pfv.amount * e.targetPct / 100 - r.currentValue : 0;
                        if (under > 0.5) t += " <b>" + r.symbol + "</b> is " + view.money(under) + " under its target: buy it at your fund company, outside the paper account.";
                    });
                    return t;
                }
            }
            Notice {
                width: parent.width
                text: !view.linked && view.active && view.plan && view.active.mode === "total"
                      ? "Total-size mode: the plan tops up whatever is under target using only the extra cash needed (" + view.money(view.plan.budget) + " = " + view.money(view.active.amount) + " minus the " + view.money(view.plan.holdingsValue) + " you hold). It never tells you to sell." : ""
            }
            Notice { width: parent.width; text: view.needsPrice ? "Some holdings have no price yet. Type a price in the Price column for those." : "" }
            Row {
                spacing: 8
                Button95 {
                    visible: view.linked
                    width: 230
                    isDefault: true
                    text: "Buy these in the paper account"
                    enabled: view.isActiveAcct && view.plan !== null && view.plan.rows.some(function (r, i) { return r.shares > 0 && !view.eItem(i).noBuy; })
                    onClicked: view.buyOrders()
                    ToolTipArea { text: view.isActiveAcct ? "Sends paper market orders to the account, after you confirm" : "Make the linked account active first" }
                }
                Button95 {
                    visible: !view.linked
                    width: 270
                    isDefault: true
                    text: "I bought these: add them to my holdings"
                    enabled: view.plan !== null && view.plan.rows.some(function (r) { return r.shares > 0; })
                    onClicked: view.recordBuys()
                    ToolTipArea { text: "Adds these shares and their cost to your Holdings, after you have bought them" }
                }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    width: page.contentWidth - 290
                    wrapMode: Text.Wrap
                    dim: true
                    text: view.linked ? "Sends simulated market orders after you confirm. They fill from market data like any paper order. Claude can never send them."
                                      : "The calculator only does the arithmetic. It does not place any order, and Claude cannot either."
                }
            }
        }

        // ------------------------------------------------ Sell
        Column {
            visible: view.active !== null && view.active.items.length > 0 && view.tab === "sell" && view.sell !== null
            width: page.contentWidth
            spacing: 6
            GroupBox95 {
                width: parent.width
                height: sellControls.implicitHeight + 30
                title: "Sell"
                Flow {
                    id: sellControls
                    width: parent.width
                    spacing: 14
                    Row {
                        spacing: 6
                        Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Sell" }
                        PxNum { value: view.sellPct; suffix: "%"; fieldWidth: 50; onCommitted: (n) => view.sellPct = Math.min(100, Math.max(0, n)) }
                        Label95 { anchors.verticalCenter: parent.verticalCenter; text: "of the portfolio" }
                        Label95 { anchors.verticalCenter: parent.verticalCenter; dim: true; text: view.sell ? "(" + view.money(view.sell.baseValue) + " can be sold from here)" : "" }
                    }
                    Row {
                        spacing: 6
                        Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Take it from" }
                        PxSeg {
                            options: [{ id: "proportional", text: "Every holding equally", tip: "The same share of every holding, so your mix stays as it is" },
                                      { id: "overweight", text: "Overweight first", tip: "Trim holdings that are above their target first, then the rest" }]
                            current: view.sellMethod
                            onPicked: (id) => view.sellMethod = id
                        }
                    }
                    CheckBox95 { height: 22; text: "Only holdings that are in profit"; checked: view.sellProfit; onToggled: (c) => view.sellProfit = c }
                    Row {
                        spacing: 2
                        Repeater {
                            model: [5, 10, 25, 50]
                            PxButton { required property int modelData; text: modelData + "%"; onClicked: view.sellPct = modelData }
                        }
                    }
                }
            }
            PxTable {
                width: parent.width
                autoHeight: true
                rowHeight: 34
                rows: view.active ? view.active.items : []
                columns: [
                    { title: "Holding", flex: true, min: 140, delegate: holdingCell },
                    { title: "You hold", w: 82, right: true,
                      text: function (it, i) { const e = view.eItem(i); return e && e.shares > 0 ? view.shares(e.shares) : "—"; },
                      sub: function (it, i) { const e = view.eItem(i), r = view.sellRow(i); return e && r && view.linked && e.shares > 0 && r.sellable !== e.shares ? view.shares(r.sellable) + " free to sell" : ""; } },
                    { title: "Price", w: 96, right: true, delegate: priceCell },
                    { title: "Gain now", w: 70, right: true,
                      text: function (it, i) { const r = view.sellRow(i); return r && r.gainPctNow !== null ? (r.gainPctNow >= 0 ? "+" : "") + r.gainPctNow.toFixed(1) + "%" : "—"; },
                      color: function (it, i) { const r = view.sellRow(i); return r && r.gainPctNow !== null ? view.up(r.gainPctNow) : Theme.shadow; } },
                    { title: "Shares to sell", w: 90, right: true,
                      text: function (it, i) { const r = view.sellRow(i); return !r ? "" : r.shares > 0 ? view.shares(r.shares) : r.skip ? r.skip : "—"; },
                      bold: function (it, i) { const r = view.sellRow(i); return r && r.shares > 0; },
                      color: function (it, i) { const r = view.sellRow(i); return r && r.shares > 0 ? Theme.text : Theme.shadow; },
                      tip: function (it, i) { const r = view.sellRow(i); return r && r.skip ? r.skip : ""; } },
                    { title: "Proceeds", w: 82, right: true, text: function (it, i) { const r = view.sellRow(i); return r && r.shares > 0 ? view.money(r.proceeds) : "—"; } },
                    { title: "Profit on those", w: 90, right: true,
                      text: function (it, i) { const r = view.sellRow(i); return r && r.shares > 0 && r.gain !== null ? view.signed(r.gain) : "—"; },
                      color: function (it, i) { const r = view.sellRow(i); return r && r.gain !== null && r.shares > 0 ? view.up(r.gain) : Theme.shadow; } },
                    { title: "Left", w: 55, right: true, text: function (it, i) { const e = view.eItem(i), r = view.sellRow(i); return e && r && e.shares > 0 ? view.shares(r.left) : "—"; } },
                    { title: "Weight after", w: 80, right: true,
                      text: function (it, i) { const e = view.eItem(i), r = view.sellRow(i); return e && r && r.weightAfter !== null && e.shares > 0 ? r.weightAfter.toFixed(1) + "%" : "—"; } }
                ]
                footer: view.sell ? [
                    { text: "Total", bold: true },
                    { text: view.sell.proceeds > 0 ? view.sell.actualPct + "% of " + view.money(view.sell.baseValue)
                             + (view.sell.actualPct < view.sell.percent - 0.005 ? ": a little under the " + view.sell.percent + "% asked for, because only whole shares can be sold" : "") : "",
                      color: Theme.shadow, left: true, span: 4 },
                    null, null, null,
                    { text: view.money(view.sell.proceeds), bold: true },
                    { text: view.sell.proceeds > 0 ? view.signed(view.sell.gain) : "—", bold: true, color: view.up(view.sell.gain) },
                    null, null
                ] : null
            }
            Repeater {
                model: view.sell ? view.sell.notes : []
                Notice { required property string modelData; width: page.contentWidth; text: Util.plain(modelData) }
            }
            Notice {
                width: parent.width
                text: view.sell && view.sell.missingPrice.length > 0
                      ? "No price for " + view.sell.missingPrice.join(", ") + ", so " + (view.sell.missingPrice.length === 1 ? "it is" : "they are") + " left out. Type a price in the Price column." : ""
            }
            Notice {
                width: parent.width
                text: view.linked && view.linkedAcct ? "Linked to <b>" + view.linkedName + "</b>: only the shares the account holds can be sold, less any sells already waiting. Mutual funds cannot be traded in the paper account, so they are not touched here." : ""
            }
            Row {
                spacing: 8
                Button95 {
                    visible: view.linked
                    width: 230
                    isDefault: true
                    text: "Sell these in the paper account"
                    enabled: view.isActiveAcct && view.sell !== null && view.sell.rows.some(function (r) { return r.shares > 0; })
                    onClicked: view.sellOrders()
                    ToolTipArea { text: view.isActiveAcct ? "Sends paper market sell orders to the account, after you confirm" : "Make the linked account active first" }
                }
                Button95 {
                    visible: !view.linked
                    width: 290
                    isDefault: true
                    text: "I sold these: remove them from my holdings"
                    enabled: view.sell !== null && view.sell.rows.some(function (r) { return r.shares > 0; })
                    onClicked: view.recordSells()
                    ToolTipArea { text: "Takes these shares out of your Holdings, after you have sold them" }
                }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    width: page.contentWidth - 310
                    wrapMode: Text.Wrap
                    dim: true
                    text: view.linked ? "Sends simulated market sell orders after you confirm. Claude can never send them."
                                      : "The calculator only does the arithmetic. It does not sell anything, and Claude cannot either."
                }
            }
        }

        // ------------------------------------------------ Holdings
        Column {
            visible: view.active !== null && view.active.items.length > 0 && view.tab === "holdings" && view.hold !== null
            width: page.contentWidth
            spacing: 6
            PxTable {
                width: parent.width
                autoHeight: true
                rowHeight: 34
                rows: view.active ? view.active.items : []
                columns: [
                    { title: "Holding", flex: true, min: 130, delegate: holdingCell },
                    { title: "Shares", w: 82, right: true, delegate: sharesCell },
                    { title: "Total paid", w: 100, right: true, delegate: costCell },
                    { title: "Avg cost", w: 72, right: true, text: function (it, i) { const h = view.holdRow(i); return h && h.avgCost !== null ? view.money(h.avgCost) : "—"; } },
                    { title: "Price", w: 96, right: true, delegate: priceCell },
                    { title: "Value now", w: 82, right: true,
                      text: function (it, i) { const h = view.holdRow(i), e = view.eItem(i); return !h || !e ? "" : h.value !== null ? (e.shares > 0 ? view.money(h.value) : "—") : "no price"; },
                      color: function (it, i) { const h = view.holdRow(i), e = view.eItem(i); return h && e && h.value !== null && e.shares > 0 ? Theme.text : Theme.shadow; } },
                    { title: "Gain / loss", w: 86, right: true,
                      text: function (it, i) { const h = view.holdRow(i), e = view.eItem(i); return h && e && h.gain !== null && (e.shares > 0 || e.cost > 0) ? view.signed(h.gain) : "—"; },
                      color: function (it, i) { const h = view.holdRow(i); return h && h.gain !== null ? view.up(h.gain) : Theme.shadow; },
                      sub: function (it, i) { const h = view.holdRow(i), e = view.eItem(i); return h && e && h.gain !== null && h.gainPct !== null && (e.shares > 0 || e.cost > 0) ? (h.gainPct >= 0 ? "+" : "") + h.gainPct.toFixed(2) + "%" : ""; } },
                    { title: "Weight vs target", w: 120, delegate: weightCell },
                    { title: "", w: 28, button: "×", flat: true, tip: function (it) { return "Remove " + it.symbol; }, onClick: function (it) { view.call("pf:removeItem", [it.id]); } }
                ]
                footer: view.hold ? [
                    { text: "Total", bold: true }, null,
                    { text: view.money(view.hold.totalCost), bold: true }, null, null,
                    { text: view.money(view.hold.totalValue), bold: true },
                    { text: view.signed(view.hold.gain) + (view.hold.gainPct !== null ? "  " + (view.hold.gainPct >= 0 ? "+" : "") + view.hold.gainPct.toFixed(2) + "%" : ""), bold: true, color: view.up(view.hold.gain), left: true, span: 2 },
                    null, null
                ] : null
            }
            Notice {
                width: parent.width
                text: view.linked ? "Shares and cost for stocks and ETFs come from the paper account <b>" + view.linkedName + "</b> and update as orders fill. Mutual funds cannot be held in a paper account, so type those yourself." : ""
            }
            Notice {
                width: parent.width
                text: view.linked ? "" : "Type the shares you own and the total you paid for them (the plan's “I bought these” button fills both in for you). Value uses " + (view.live ? "the latest price" : "the price you typed")
                                         + ". Mutual funds are priced once a day after the market closes, and the price may be missing for some funds, so you can type one."
                                         + (view.updated && view.live ? " Prices updated " + new Date(view.updated).toLocaleTimeString(Qt.locale("en_US"), Util.hour12 ? "h:mm:ss AP" : "HH:mm:ss") + "." : "")
            }
        }
    }

    // ---------------------------------------------------------------- table cells
    // the holding: symbol (opens the chart), its kind, and its name under it
    Component {
        id: holdingCell
        Item {
            id: hc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            clip: true
            Text {
                id: sym
                x: 4
                y: 2
                text: hc.row ? hc.row.symbol : ""
                textFormat: Text.PlainText
                font.bold: true
                font.underline: true
                color: "#0000c0"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: view.openChart(hc.row.symbol) }
                ToolTipArea { text: "Open the chart" }
            }
            Rectangle {
                x: sym.x + sym.implicitWidth + 6
                y: 3
                width: kindText.implicitWidth + 10
                height: 14
                radius: 7
                color: "#e4e4e4"
                border.color: Theme.shadow
                Text {
                    id: kindText
                    anchors.centerIn: parent
                    text: hc.row ? view.kindLabel[hc.row.kind] || "" : ""
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize - 2
                    color: Theme.text
                }
            }
            Text {
                x: 4
                y: 18
                width: parent.width - 8
                elide: Text.ElideRight
                text: hc.row ? view.nameOf(hc.row) : ""
                textFormat: Text.PlainText
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
            }
        }
    }
    // the price: the live one, else a box to type one
    Component {
        id: priceCell
        Item {
            id: pc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property var lp: row ? view.prices[row.symbol] : undefined
            readonly property bool hasLive: view.live && lp !== undefined && lp !== null
            Text {
                visible: pc.hasLive
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: pc.hasLive ? view.money(pc.lp) : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
            PxNum {
                visible: !pc.hasLive
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                value: pc.row ? pc.row.manualPrice : null
                prefix: "$"
                fieldWidth: 66
                placeholder: "price"
                tip: view.live ? "No live price came back for this symbol. Type one to use it in the calculator." : "No market data key, so type a price to use it in the calculator."
                onCommitted: (n) => view.updateItem(pc.row.id, { manualPrice: n })
            }
        }
    }
    // shares and cost on the Holdings tab: from the paper account when linked (not for funds), else typed
    Component {
        id: sharesCell
        Item {
            id: sc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property var e: row ? view.eItem(rowIndex) : null
            readonly property bool fromAccount: view.linked && e !== null && !e.noBuy
            Text {
                visible: sc.fromAccount
                anchors.right: parent.right
                anchors.rightMargin: 4
                y: 3
                text: sc.e ? view.shares(sc.e.shares) : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
            Text {
                visible: sc.fromAccount && sc.row !== null && !!view.eff && !!view.eff.pending[sc.row.symbol]
                anchors.right: parent.right
                anchors.rightMargin: 4
                y: 18
                text: visible ? "incl. " + view.eff.pending[sc.row.symbol] + " waiting" : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
                color: Theme.shadow
            }
            ToolTipArea { text: sc.fromAccount && view.linkedAcct ? "From the paper account “" + view.linkedAcct.name + "”" : "" }
            PxNum {
                visible: !sc.fromAccount
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                value: sc.row ? sc.row.shares : null
                fieldWidth: 66
                onCommitted: (n) => view.updateItem(sc.row.id, { shares: n })
            }
        }
    }
    Component {
        id: costCell
        Item {
            id: cc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property var e: row ? view.eItem(rowIndex) : null
            readonly property bool fromAccount: view.linked && e !== null && !e.noBuy
            Text {
                visible: cc.fromAccount
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: cc.e ? view.money(cc.e.cost) : ""
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                color: Theme.text
            }
            ToolTipArea { text: cc.fromAccount && view.linkedAcct ? "From the paper account “" + view.linkedAcct.name + "”" : "" }
            PxNum {
                visible: !cc.fromAccount
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                value: cc.row ? cc.row.cost : null
                prefix: "$"
                fieldWidth: 74
                tip: "The total you paid for these shares, including any fees"
                onCommitted: (n) => view.updateItem(cc.row.id, { cost: n })
            }
        }
    }
    // weight now against the target: a bar with a mark at the target, the numbers and the drift under it
    Component {
        id: weightCell
        Item {
            id: wc
            property var col: null
            property var row: null
            property int rowIndex: 0
            property bool sel: false
            readonly property var h: view.hold && row ? view.hold.rows[rowIndex] : null
            readonly property bool shown: h !== null && h !== undefined && h.weight !== null && view.hold.totalValue > 0
            Text {
                visible: !wc.shown
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: "—"
                color: Theme.shadow
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Bevel {
                visible: wc.shown
                x: 4
                y: 4
                width: parent.width - 8
                height: 10
                style: "thinSunken"
                face: Theme.window
                Rectangle { x: 1; y: 1; height: parent.height - 2; width: wc.shown ? (parent.width - 2) * Math.min(100, wc.h.weight) / 100 : 0; color: Theme.selection }
                Rectangle { x: wc.shown ? 1 + (parent.width - 3) * Math.min(100, wc.h.targetPct) / 100 : 0; y: -2; width: 2; height: parent.height + 4; color: "#c00000" }
            }
            Text {
                visible: wc.shown
                x: 4
                y: 17
                width: parent.width - 8
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: !wc.shown ? "" : wc.h.weight.toFixed(1) + "% <font color='" + Theme.shadow + "'>of " + wc.h.targetPct + "%</font>"
                      + (wc.h.drift !== null && Math.abs(wc.h.drift) >= 0.05 ? " <font color='" + (wc.h.drift > 0 ? "#a06000" : Theme.shadow) + "'>" + (wc.h.drift > 0 ? "+" : "") + wc.h.drift.toFixed(1) + " pts</font>" : "")
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize - 1
                color: Theme.text
            }
            ToolTipArea { text: wc.shown ? "Now " + wc.h.weight.toFixed(1) + "% · target " + wc.h.targetPct + "%" : "" }
        }
    }

    Menu95 { id: menu }
}
