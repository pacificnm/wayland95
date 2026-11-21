import QtQuick
import QtQuick.Window
import Win95Ui

// Trading Lab: learning to trade with paper money (github.com/pacificnm/trading-labs.app, in the Wayland 95 look).
// This window is upstream's App.tsx: it owns the app-level state (symbol, chart settings, the paper account's
// snapshot, the order-ticket draft, the calculator, watchlists) and the screen switch. The screens are listed on the
// left like upstream's ribbon; the per-symbol screens (Chart, News, Quote...) share a header with their own tabs; the
// Claude panel is on the right. Screens get this window as `app` and call back into it.
Window {
    id: app

    title: symbol + " - Trading Lab"
    width: 1280
    height: 820
    minimumWidth: 900
    minimumHeight: 560
    visible: true
    color: Theme.face

    // ------------------------------------------------------------------ screens
    // the ribbon (upstream RIBBON), then Settings; per-symbol screens are reached from the Chart's header
    readonly property var ribbon: [
        { id: "chart", text: "Charts", icon: "chart" },
        { id: "account", text: "Account", icon: "money" },
        { id: "trades", text: "Active Trades", icon: "table" },
        { id: "watch", text: "Watchlists", icon: "star" },
        { id: "portfolio", text: "Portfolio", icon: "database" },
        { id: "market", text: "Market Performance", icon: "globe" },
        { id: "congress", text: "Senate & House Trades", icon: "people" },
        { id: "marketnews", text: "Market News", icon: "mail" },
        { id: "calculator", text: "Position Calculator", icon: "numbers" },
        { id: "screener", text: "Stock Screener", icon: "filter" },
        { id: "strategies", text: "Trading Strategies", icon: "book" },
        { id: "journal", text: "Trading Journal", icon: "edit" },
    ]
    readonly property var symbolViews: [
        { id: "chart", text: "Chart", icon: "chart" },
        { id: "news", text: "News", icon: "mail" },
        { id: "quote", text: "Quote Details", icon: "info" },
        { id: "analyst", text: "Analyst Reports", icon: "document" },
        { id: "fundamentals", text: "Fundamentals", icon: "table" },
        { id: "options", text: "Option Stats", icon: "numbers" },
    ]
    readonly property var viewFiles: ({
        chart: "ChartView.qml", news: "NewsView.qml", quote: "QuoteView.qml", analyst: "AnalystView.qml",
        fundamentals: "FundamentalsView.qml", options: "OptionsView.qml", account: "AccountView.qml",
        trades: "TradesView.qml", watch: "WatchlistView.qml", portfolio: "PortfolioView.qml", market: "MarketView.qml",
        congress: "CongressView.qml", marketnews: "MarketNewsView.qml", calculator: "CalculatorView.qml",
        screener: "ScreenerView.qml", strategies: "StrategiesView.qml", journal: "JournalView.qml",
        settings: "SettingsView.qml", chartSettings: "ChartSettingsView.qml", help: "HelpView.qml"
    })
    function isSymbolView(v) { return symbolViews.some(function (s) { return s.id === v; }); }

    property string view: "chart"
    property string helpTopic: "welcome"
    function setView(v) {
        if (!viewFiles[v]) return;
        view = v;
        refreshTrading();
    }
    function openHelp(topic) { if (topic) helpTopic = topic; setView("help"); }

    // ------------------------------------------------------------------ the FMP plan (shared/fmpCaps.ts)
    property var unavailable: []        // capability ids the user's plan does not include
    property bool hidePlanGaps: true    // the "Hide features my plan does not include" setting
    readonly property var screenCaps: ({ market: ["sectors", "movers"], congress: ["congress"], marketnews: ["news", "marketNews"],
                                         screener: ["screener"], news: ["news"], analyst: ["analyst", "estimates"], fundamentals: ["fundamentals"] })
    function hasScreen(id) {
        if (!hidePlanGaps) return true;
        const needs = screenCaps[id] || [];
        return !(needs.length > 0 && needs.every(function (n) { return unavailable.indexOf(n) >= 0; }));
    }
    function hasCap(id) { return !hidePlanGaps || unavailable.indexOf(id) < 0; }
    function applyCaps(caps) {
        unavailable = caps && caps.unavailable ? caps.unavailable : [];
        const intraday = { bars1min: "1min", bars5min: "5min", bars15min: "15min", bars30min: "30min", bars1hour: "1hour", bars4hour: "4hour", daily: "1day" };
        core.setBlockedIntervals(hidePlanGaps ? unavailable.map(function (c) { return intraday[c]; }).filter(Boolean) : []);
        // when the plan turns out not to include what the chart uses, move to something it does include
        const cur = chartSettings;
        const fit = core.usableTimeframe(cur.range, cur.interval);
        if (fit.range !== cur.range || fit.interval !== cur.interval) {
            updateChartSettings(Object.assign({}, cur, fit));
            toast("info", "Your FMP plan has no " + cur.interval + " bars, so the chart switched to " + fit.interval + ".", { title: "Chart changed" });
        }
        if (!hasScreen(view)) setView("chart");
    }

    // ------------------------------------------------------------------ symbol, live data, display
    property string symbol: testSymbol || "AAPL"
    function setSymbol(s) { if (s) symbol = String(s).trim().toUpperCase(); }
    property bool live: false           // an FMP key is configured (otherwise sample data)
    function refreshLive() { api.invoke("fmp:key:status", [], function (k) { live = k.source !== "none"; }); }
    property var display: ({ timeZone: "America/New_York", hour12: true })
    function updateDisplay(d) {
        display = d;
        Util.timeZone = d.timeZone || "";
        Util.hour12 = d.hour12 !== false;
        api.invoke("settings:set", ["display", d]);
    }

    // ------------------------------------------------------------------ chart settings and drawings
    readonly property alias chartData: chartData
    property var chartSettings: core.defaultChartSettings()
    function updateChartSettings(s) {
        chartSettings = s;
        api.invoke("settings:set", ["chart", s]);
    }
    // Sets the chart up for a strategy (timeframe, chart type and studies); the toast offers to put the old chart back.
    function applyChartSetup(setup, name, sym) {
        const prev = { symbol: symbol, settings: chartSettings };
        const r = core.chartSettingsFromSetup(chartSettings, setup);
        updateChartSettings(r.settings);
        if (sym) setSymbol(sym);
        setView("chart");
        toast("info", "Chart set up for “" + name + "”" + (r.dropped.length ? " (skipped: " + r.dropped.join(", ") + ")" : ""),
              { action: { label: "Undo", onClick: function () { updateChartSettings(prev.settings); setSymbol(prev.symbol); } } });
        return r.dropped;
    }

    // ------------------------------------------------------------------ paper trading: snapshot, ticket, quote
    property var snap: null
    function refreshTrading() { api.invoke("trade:snapshot", [], function (s) { if (s && s.account) snap = s; }); }
    property var ticket: null           // the order-ticket draft (chart/orderDraft.ts), or null when closed
    property int lastAccountId: -1
    onSnapChanged: {
        // a half-built order must never be sent to a different account than the one it was built for
        const id = snap ? snap.account.id : -1;
        if (lastAccountId >= 0 && id >= 0 && id !== lastAccountId && ticket) {
            ticket = null;
            toast("info", "Order ticket closed because you switched accounts.");
        }
        if (id >= 0) lastAccountId = id;
    }
    function positionOf(sym) {
        const ps = snap ? snap.positions : [];
        for (let i = 0; i < ps.length; ++i) if (ps[i].symbol === sym) return ps[i];
        return null;
    }
    function openTicket(want) {
        const pos = positionOf(symbol);
        const side = want === "buy" ? (pos && pos.qty < 0 ? "buy_to_cover" : "buy") : (pos && pos.qty > 0 ? "sell" : "sell_short");
        if (ticket) ticket = Object.assign({}, ticket, { side: side });
        else ticket = Object.assign(core.emptyDraft(side), { qty: pos ? String(Math.abs(pos.qty)) : "10" });
        if (!isSymbolView(view) || view !== "chart") setView("chart");
    }
    property var quote: ({ last: null, change: null, changePct: null })
    function loadQuote() {
        if (!live) return;
        const want = symbol;
        api.invoke("fmp:quotes", [[want]], function (r) {
            if (want !== symbol || !r.ok || !r.data[0]) return;
            const q = r.data[0];
            quote = { last: Number(q.price) || null, change: Number(q.change) || 0, changePct: Number(q.changePercentage ?? q.changesPercentage) || 0 };
        });
    }
    onSymbolChanged: { quote = { last: null, change: null, changePct: null }; loadQuote(); }
    onLiveChanged: loadQuote()
    Timer {
        interval: app.ticket || app.positionOf(app.symbol) ? 10000 : 60000
        running: app.live
        repeat: true
        onTriggered: app.loadQuote()
    }
    Timer { interval: 20000; running: true; repeat: true; onTriggered: app.refreshTrading() }

    // the order lines on the chart: working orders and positions, plus the draft while a ticket is open
    readonly property var orderLines: {
        const lines = core.tradeLines(symbol, snap ? snap.orders : [], snap ? snap.positions : []);
        return ticket ? lines.concat(core.draftLines(ticket, symbol, quote.last)) : lines;
    }
    function moveOrderLine(id, price) {
        const p = String(Math.round(price * 100) / 100);
        if (id.indexOf("draft:") === 0 && ticket) {
            if (id === "draft:target") ticket = Object.assign({}, ticket, { target: p });
            else if (id === "draft:stop") ticket = Object.assign({}, ticket, { protectStop: p });
            else ticket = Object.assign({}, ticket, ticket.type === "stop" ? { stop: p } : { limit: p });
        } else if (id.indexOf("order:") === 0) {
            const oid = Number(id.slice(6));
            const o = (snap ? snap.orders : []).find(function (x) { return x.id === oid; });
            if (!o) return;
            api.invoke("trade:modify", [o.id, o.type === "limit" ? { limit: price } : { stop: price }], function (r) {
                if (!r.ok) toast("error", r.errors.join(" "), { title: "Could not move order" });
                else toast("success", o.side.replace(/_/g, " ") + " " + o.type.replace("_", " ") + " order moved to " + price.toFixed(2));
                refreshTrading();
            });
        }
    }

    // ------------------------------------------------------------------ position calculator (inputs and risk rules, both saved)
    property var calc: core.defaultCalc()
    property var rules: core.callPure("defaultRules", [])
    function updateCalc(p) { calc = Object.assign({}, calc, p); api.invoke("settings:set", ["calc", calc]); }
    function updateRules(r) { rules = r; api.invoke("settings:set", ["riskRules", r]); }
    function ticketFromCalc(p) {
        const r2 = function (n) { return String(Math.round(n * 100) / 100); };
        setSymbol(p.symbol);
        setView("chart");
        ticket = Object.assign(core.emptyDraft(p.side === "long" ? "buy" : "sell_short"), {
            qty: String(p.shares), type: "limit", limit: r2(p.entry), tif: "day", strategy: p.target !== null && p.target !== undefined ? "bracket" : "single",
            target: p.target !== null && p.target !== undefined ? r2(p.target) : "", protectStop: r2(p.stop), note: p.note, journal: true
        });
        toast("info", p.target !== null && p.target !== undefined ? "Ticket filled from the calculator. Review it before sending."
                                                                  : "Ticket filled from the calculator. It has no target or stop attached, so add them if you want a bracket.");
    }

    // ------------------------------------------------------------------ journal
    property int journalFocus: -1
    function openJournal(id) { if (id !== undefined && id !== null) journalFocus = id; setView("journal"); }
    // Turns a journal idea into a pre-filled order ticket on the chart.
    function ticketFromEntry(e) {
        if (!e.symbol) return;
        const isShort = e.direction === "short";
        const bracket = e.plan_stop !== null && e.plan_target !== null;
        setSymbol(e.symbol);
        setView("chart");
        const d = Object.assign(core.emptyDraft(isShort ? "sell_short" : "buy"), {
            qty: String(e.plan_qty ?? 10), type: e.plan_entry !== null ? "limit" : "market", limit: e.plan_entry !== null ? String(e.plan_entry) : "",
            strategy: bracket ? "bracket" : "single", target: e.plan_target !== null ? String(e.plan_target) : "",
            protectStop: e.plan_stop !== null ? String(e.plan_stop) : "", tif: "gtc", source: e.source, journalId: e.id, journal: true
        });
        if (e.body) d.note = e.body.slice(0, 400);
        ticket = d;
        toast("info", "Ticket filled from “" + e.title + "”. Review it before sending.");
    }

    // ------------------------------------------------------------------ Claude
    // Settings saved new Claude defaults (upstream's chat-defaults-changed window event)
    signal chatDefaultsChanged(var prefs)
    property var chatPrompt: null       // {text, n}: a question another screen hands to the panel
    function askClaude(text) { chatPrompt = { text: text, n: Date.now() }; }
    readonly property var chatContext: ({
        screen: view, symbol: symbol, timeZone: display.timeZone || "local",
        chart: { range: chartSettings.range, interval: chartSettings.interval, type: chartSettings.type, scale: chartSettings.scale,
                 studies: (chartSettings.studies || []).filter(function (s) { return s.visible; }).map(function (s) { return { study: s.studyId, params: s.params }; }) },
        account: snap ? { name: snap.account.name, brokerage: snap.account.broker, kind: "paper (simulated money)", type: snap.account.type, equity: snap.account.equity,
                          cash: snap.account.cash, buyingPowerAvailable: snap.account.buyingPower, openPL: snap.account.unrealizedPl, realizedPL: snap.account.realizedPl } : null,
        positions: snap ? snap.positions.map(function (p) { return { symbol: p.symbol, qty: p.qty, avg: p.avg, last: p.mark, unrealizedPL: p.unrealized }; }) : [],
        workingOrders: snap ? snap.orders.filter(function (o) { return o.status === "working" || o.status === "pending"; }).length : 0,
        watchlists: lists.map(function (l) { return { name: l.name, symbols: l.symbols }; }),
        riskRules: rules
    })

    // ------------------------------------------------------------------ watchlists
    property var lists: []
    property int activeListId: -1
    readonly property var activeList: {
        for (let i = 0; i < lists.length; ++i) if (lists[i].id === activeListId) return lists[i];
        return lists.length ? lists[0] : null;
    }
    function loadLists() { api.invoke("wl:lists", [], function (l) { lists = l; }); }
    function chooseList(id) { activeListId = id; api.invoke("settings:set", ["wlActive", id]); }
    function openWatchlists() { setView("watch"); }
    function toggleInList(listId, add) {
        const list = lists.find(function (l) { return l.id === listId; });
        if (!list) return;
        const sym = symbol;
        if (add) api.invoke("wl:add", [listId, sym], function (r) {
            if (r.ok) toast("success", sym + " added to " + list.name, { action: { label: "View", onClick: function () { chooseList(listId); openWatchlists(); } } });
            else toast("error", r.error);
        });
        else api.invoke("wl:remove", [listId, sym], function () { toast("info", sym + " removed from " + list.name); });
    }
    function createList(name, addSymbol) {
        api.invoke("wl:create", [name], function (r) {
            if (!r.ok) { toast("error", r.error); return; }
            chooseList(r.id);
            if (addSymbol) api.invoke("wl:add", [r.id, addSymbol]);
            toast("success", addSymbol ? "Created “" + name + "” with " + addSymbol : "Created watchlist “" + name + "”");
        });
    }
    function newListDialog() {
        promptText({ title: "New watchlist", label: "Name", placeholder: "e.g. Tech, Earnings this week", okLabel: "Create",
                     validate: function (v) { return v ? null : "Give the list a name."; } }, function (name) { if (name) createList(name); });
    }
    function renameList(l) {
        promptText({ title: "Rename watchlist", label: "Name", initial: l.name, okLabel: "Rename",
                     validate: function (v) { return v ? null : "Give the list a name."; } }, function (name) {
            if (!name || name === l.name) return;
            api.invoke("wl:rename", [l.id, name], function (r) { if (r.ok) toast("success", "Renamed to “" + name + "”"); else toast("error", r.error); });
        });
    }
    function deleteList(l) {
        confirm("Delete the watchlist “" + l.name + "” and its " + l.symbols.length + " symbol" + (l.symbols.length === 1 ? "" : "s") + "?", function () {
            api.invoke("wl:delete", [l.id], function (r) { if (r.ok) toast("info", "Deleted “" + l.name + "”"); else toast("error", r.error); });
        });
    }
    function addSymbolTo(listId, sym) {
        api.invoke("wl:add", [listId, sym], function (r) {
            if (!r.ok) toast("error", r.error);
            else if (!r.added) toast("info", r.symbol + " is already on this list");
            else toast("success", r.symbol + " added");
        });
    }
    function removeSymbolFrom(listId, sym) { api.invoke("wl:remove", [listId, sym], function () { toast("info", sym + " removed"); }); }

    // ------------------------------------------------------------------ messages: toasts, prompts, confirmations
    // toast(kind, text, {title?, action?: {label, onClick}, duration?}); kind: success | error | info | warning
    function toast(kind, text, opts) { toasts.add(kind, text, opts || {}); }
    // promptText({title, label, initial?, placeholder?, okLabel?, validate?}, callback(textOrNull))
    function promptText(opts, cb) { promptDialog.ask(opts, cb); }
    function confirm(text, cb, title) { msgBox.show(title || "Trading Lab", text, "question", ["OK", "Cancel"], function (i) { if (i === 0) cb(); }); }
    function alert(text, icon) { msgBox.show("Trading Lab", text, icon || "info", ["OK"], null); }

    // ------------------------------------------------------------------ backend events
    Connections {
        target: api
        function onEvent(channel, payload) {
            if (channel === "trade:update") {
                app.refreshTrading();
                (payload || []).forEach(function (e) {
                    if (/rejected/i.test(e.text)) app.toast("error", e.text);
                    else if (/expired/i.test(e.text)) app.toast("warning", e.text);
                    else if (/filled/i.test(e.text)) app.toast("success", e.text, { title: "Order filled" });
                    else app.toast("info", e.text);
                });
            } else if (channel === "watchlists:changed") {
                app.loadLists();
            } else if (channel === "fmp:caps:changed") {
                app.applyCaps(payload);
            } else if (channel === "tool:call") {
                toolRunner.run(payload);
            }
        }
    }

    // ------------------------------------------------------------------ start-up: saved state
    Component.onCompleted: {
        refreshLive();
        refreshTrading();
        loadLists();
        api.invoke("settings:get", ["display"], function (d) { updateDisplay(Object.assign({ timeZone: "America/New_York", hour12: true }, d || {})); });
        api.invoke("settings:get", ["chart"], function (s) { if (s) chartSettings = core.normalizeChartSettings(s); });
        api.invoke("settings:get", ["calc"], function (c) { if (c) calc = core.normalizeCalc(c); });
        api.invoke("settings:get", ["riskRules"], function (r) { if (r) rules = Object.assign({}, rules, r); });
        api.invoke("settings:get", ["wlActive"], function (id) { if (typeof id === "number") activeListId = id; });
        api.invoke("settings:get", ["hidePlanGaps"], function (v) { if (v === false) hidePlanGaps = false; api.invoke("fmp:caps:get", [], applyCaps); });
        if (testView) setView(testView);
        if (testGrab) grabTimer.start();
    }

    // ------------------------------------------------------------------ menus
    function fileMenu() {
        return [
            { text: "&Settings", action: function () { setView("settings"); } },
            { separator: true },
            { text: "&Back Up Data...", action: backUp },
            { text: "&Restore From Backup...", action: restore },
            { separator: true },
            { text: "E&xit", shortcut: "Alt+F4", action: function () { Qt.quit(); } },
        ];
    }
    function viewMenu() {
        const items = ribbon.filter(function (r) { return hasScreen(r.id); }).map(function (r) {
            return { text: r.text, checked: r.id === "chart" ? isSymbolView(view) : view === r.id, action: function () { setView(r.id); } };
        });
        items.push({ separator: true });
        items.push({ text: chatVisible ? "Hide &Claude Panel" : "Show &Claude Panel", action: function () { chatVisible = !chatVisible; } });
        return items;
    }
    function chartMenu() {
        return [
            { text: "Chart &Settings...", action: function () { setView("chartSettings"); } },
            { separator: true },
            { text: "&Buy " + symbol + "...", action: function () { openTicket("buy"); } },
            { text: "&Sell " + symbol + "...", action: function () { openTicket("sell"); } },
        ];
    }
    function helpMenu() {
        return [
            { text: "&Help Contents", shortcut: "F1", action: function () { openHelp(); } },
            { separator: true },
            { text: "&About Trading Lab...", action: function () { aboutLoader.active = true; } },
        ];
    }
    Shortcut { sequence: "F1"; onActivated: app.openHelp() }

    // File > Back Up / Restore: the dialogs are here; the database work is in the backend
    function backUp() {
        fileDialog.mode = "save";
        fileDialog.title = "Back up Trading Lab data";
        fileDialog.filters = [{ name: "Trading Lab backup", patterns: ["*.db"] }];
        fileDialog.defaultSuffix = "db";
        fileDialog.purpose = "backup";
        fileDialog.openAt(Win95Fs.homePath() + "/Documents", "trading-lab-backup-" + new Date().toISOString().slice(0, 10) + ".db");
    }
    function restore() {
        fileDialog.mode = "open";
        fileDialog.title = "Restore Trading Lab data from a backup";
        fileDialog.filters = [{ name: "Trading Lab backup", patterns: ["*.db"] }, { name: "All files", patterns: ["*"] }];
        fileDialog.purpose = "restore";
        fileDialog.openAt(Win95Fs.homePath() + "/Documents", "");
    }
    function picked(path) {
        if (fileDialog.purpose === "backup") {
            api.invoke("backup:create", [path], function (r) {
                if (r.ok) toast("success", "Saved " + r.contents + " to " + r.path + ".", { title: "Backup saved" });
                else toast("error", r.error, { title: "Backup failed" });
            });
            return;
        }
        api.invoke("backup:inspect", [path], function (info) {
            if (!info.ok) { alert(info.error, "error"); return; }
            const made = info.summary.createdAt ? " made " + new Date(info.summary.createdAt).toLocaleString() : "";
            msgBox.show("Restore backup", "Replace all current data with this backup?\n\nBackup" + made + ":\n" + info.contents
                        + ".\n\nEverything now in Trading Lab (accounts, orders, journal, watchlists, settings, chats) is replaced. A safety copy of the current data is saved first, and your API keys on this computer are kept. The app restarts when it is done.",
                        "warning", ["Restore and restart", "Cancel"], function (i) {
                if (i !== 0) return;
                api.invoke("backup:restore", [path], function (r) { if (!r.ok) alert(r.error, "error"); });
            });
        });
    }

    // ------------------------------------------------------------------ layout
    MenuBar95 {
        id: menuBar
        x: 2
        y: 2
        width: parent.width - 4
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: app.fileMenu },
            { title: "View", items: app.viewMenu },
            { title: "Chart", items: app.chartMenu },
            { title: "Help", items: app.helpMenu },
        ]
    }

    // the ribbon: one button per screen, like Outlook's bar
    Bevel {
        id: ribbonWell
        style: "sunken"
        face: Theme.shadow
        x: 4
        y: menuBar.y + menuBar.height + 4
        width: 92
        height: statusBar.y - y - 4
        Column {
            x: 2
            y: 4
            width: parent.width - 4
            spacing: 2
            Repeater {
                model: app.ribbon.filter(function (r) { return app.hasScreen(r.id); }).concat([{ id: "settings", text: "Settings", icon: "settings" }])
                delegate: Item {
                    id: place
                    required property var modelData
                    readonly property bool current: modelData.id === "chart" ? app.isSymbolView(app.view) : app.view === modelData.id
                    width: parent.width
                    height: 52
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        color: place.current ? Theme.face : "transparent"
                        border.color: placeMouse.containsMouse && !place.current ? Theme.light : "transparent"
                    }
                    Icon95 { anchors.horizontalCenter: parent.horizontalCenter; y: 5; size: 24; width: 24; height: 24; name: place.modelData.icon }
                    Text {
                        y: 31
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: place.modelData.text
                        elide: Text.ElideRight
                        font.family: Theme.fontFamily
                        font.pixelSize: 10
                        color: place.current ? Theme.text : "white"
                    }
                    MouseArea {
                        id: placeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: app.setView(place.modelData.id)
                    }
                }
            }
        }
    }

    // the screen
    Item {
        id: workbench
        x: ribbonWell.x + ribbonWell.width + 4
        y: ribbonWell.y
        width: (app.chatVisible ? splitter.x : parent.width - 4) - x
        height: ribbonWell.height
        Loader {
            id: viewLoader
            anchors.fill: parent
            source: app.viewFiles[app.view] || ""
            onStatusChanged: if (status === Loader.Error) console.warn("Trading Lab: could not load " + source)
            onLoaded: if (item.hasOwnProperty("app")) item.app = app
        }
        Text {
            anchors.centerIn: parent
            visible: viewLoader.status === Loader.Error
            text: "This screen is not available."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
    }

    // the Claude panel, with a splitter to its left
    property bool chatVisible: true
    property real chatWidth: 340
    Rectangle {
        id: splitter
        visible: app.chatVisible
        x: parent.width - app.chatWidth - 4 - width
        y: ribbonWell.y
        width: 5
        height: ribbonWell.height
        color: Theme.face
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitHCursor
            property real startX: 0
            onPressed: (m) => startX = m.x
            onPositionChanged: (m) => {
                if (!pressed) return;
                app.chatWidth = Math.max(240, Math.min(app.width * 0.6, app.chatWidth - (m.x - startX)));
            }
        }
    }
    Loader {
        id: chatLoader
        visible: app.chatVisible
        x: splitter.x + splitter.width
        y: ribbonWell.y
        width: parent.width - x - 4
        height: ribbonWell.height
        source: "ChatPanel.qml"
        onLoaded: if (item.hasOwnProperty("app")) item.app = app
    }

    // the status bar: symbol, account, equity, buying power, market session
    Item {
        id: statusBar
        x: 2
        y: parent.height - height - 2
        width: parent.width - 4
        height: 22
        readonly property bool open: core.isMarketOpen(clockTick.now)
        Timer { id: clockTick; property real now: Date.now() / 1000; interval: 30000; running: true; repeat: true; onTriggered: now = Date.now() / 1000 }
        Row {
            anchors.fill: parent
            spacing: 2
            StatusCell { width: 90; text: app.symbol }
            StatusCell {
                width: 260
                text: app.snap ? "Paper: " + app.snap.account.name + (app.snap.account.broker ? " (" + app.snap.account.broker + ")" : "") : "Paper account"
                onClicked: app.setView("account")
            }
            StatusCell { width: 170; text: "Equity " + Util.usd(app.snap ? app.snap.account.equity : null) }
            StatusCell { width: 190; text: "Buying power " + Util.usd(app.snap ? app.snap.account.buyingPower : null) }
            StatusCell { width: 150; text: app.live ? (statusBar.open ? "Market open" : "Market closed") : "Sample data (no FMP key)" }
            StatusCell { width: Math.max(80, statusBar.width - x); text: "Simulated trading: not investment advice" }
        }
    }
    component StatusCell: Bevel {
        id: cell
        property string text: ""
        signal clicked()
        style: "thinSunken"
        height: 22
        Text {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 8
            elide: Text.ElideRight
            text: cell.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            color: Theme.text
        }
        MouseArea { anchors.fill: parent; onClicked: cell.clicked() }
    }

    // ------------------------------------------------------------------ overlays
    Toasts { id: toasts; anchors.right: parent.right; anchors.bottom: statusBar.top; anchors.margins: 8 }
    ToolRunner { id: toolRunner; app: app }
    ChartData { id: chartData; app: app }
    PromptDialog { id: promptDialog }
    Loader { id: aboutLoader; active: false; source: "AboutDialog.qml"; onLoaded: { item.app = app; item.show(); } }
    FileDialog95 {
        id: fileDialog
        property string purpose: ""
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => app.picked(Util.localPath(path))
    }
    Menu95 { id: popup }
    MessageBox95 { id: msgBox }

    // W95_TRADINGLAB_GRAB: a picture of the window, for the tests
    Timer {
        id: grabTimer
        interval: 6000
        onTriggered: {
            windowGrabber.grab(app, testGrab);
            if (!testScript) Qt.quit();
        }
    }
}
