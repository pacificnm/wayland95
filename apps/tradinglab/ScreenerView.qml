import QtQuick
import Win95Ui

// Stock Screener: narrow thousands of stocks with FMP's screener (sector, size, price, volume, beta...) plus the
// filters FMP cannot do (dividend yield, relative volume) worked out in the app; start from a preset, sort the
// results, add a symbol to the active watchlist, save the top 50 as a new watchlist, copy the results as CSV or ask
// Claude to read the screen. Port of upstream renderer/src/views/ScreenerView.tsx (fmp:screener, fmp:screenerOptions;
// presets and defaults from content screens.json; filtering, sorting and CSV through core.callPure).
Item {
    id: view
    property var app

    readonly property bool live: app ? app.live : false
    property var defaults: core.callPure("defaultScreenerFilters", [])
    property var presets: []
    property var f: defaults
    property var applied: defaults
    property string preset: ""
    property var opts: null
    property var result: null           // {rows, asOf}
    property string error: ""
    property bool loading: false
    property int seq: 0
    property bool forceNext: false

    readonly property var sizes: [["", "", "Any size"], ["0", "300", "Micro (< $300M)"], ["300", "2000", "Small ($300M – $2B)"], ["2000", "10000", "Mid ($2B – $10B)"],
                                  ["10000", "200000", "Large ($10B – $200B)"], ["200000", "", "Mega (> $200B)"]]
    readonly property var limits: [100, 250, 500, 1000, 3000]

    Component.onCompleted: {
        api.invoke("content:get", ["screens.json"], function (c) {
            if (!c || !c.screener) return;
            presets = c.screener.presets || [];
            if (c.screener.defaultFilters) { defaults = c.screener.defaultFilters; f = defaults; applied = defaults; }
        });
    }
    onLiveChanged: { loadOptions(); load(); }
    onAppChanged: { loadOptions(); load(); }
    function loadOptions() {
        if (!live || opts) return;
        api.invoke("fmp:screenerOptions", [], function (r) { if (r && r.ok) opts = r.data; });
    }

    readonly property string appliedKey: JSON.stringify(applied)
    onAppliedKeyChanged: load()
    function load() {
        if (!live) return;
        const id = ++seq;
        const force = forceNext;
        forceNext = false;
        loading = true;
        error = "";
        api.invoke("fmp:screener", [{ query: core.callPure("toScreenerQuery", [applied]), force: force }], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            result = r.data;
            shownCount = 100;
        });
    }

    function set(patch) { preset = ""; f = Object.assign({}, f, patch); }
    function run(next) { if (next) f = next; applied = f; }
    readonly property bool dirty: JSON.stringify(f) !== appliedKey
    readonly property var active: presets.find(function (p) { return p.id === preset; }) || null

    // ------------------------------------------------------------------ results (upstream ScreenerResults)
    readonly property var rows: result ? core.callPure("applyClientFilters", [result.rows, applied]) : []
    property string sortKey: "marketCap"
    property int sortDir: -1
    property int shownCount: 100
    readonly property var sorted: core.callPure("sortScreenerRows", [rows, sortKey, sortDir])
    readonly property var sum: core.callPure("summarizeScreener", [rows])
    readonly property bool truncated: !!result && !core.callPure("hasClientFilters", [applied]) && result.rows.length >= applied.limit
    function sortOn(key) {
        if (sortKey === key) sortDir = sortDir === 1 ? -1 : 1;
        else { sortKey = key; sortDir = key === "symbol" || key === "name" ? 1 : -1; }
    }
    function cap(n) { return n === null || n === undefined ? "—" : n >= 1e12 ? "$" + (n / 1e12).toFixed(2) + "T" : n >= 1e9 ? "$" + (n / 1e9).toFixed(1) + "B" : n >= 1e6 ? "$" + Math.round(n / 1e6) + "M" : "$" + Util.num(n, 0); }
    function vol(n) {
        if (n === null || n === undefined) return "—";
        const a = Math.abs(n), r = function (v) { return String(Math.round(v * 10) / 10); };
        return a >= 1e9 ? r(n / 1e9) + "B" : a >= 1e6 ? r(n / 1e6) + "M" : a >= 1e3 ? r(n / 1e3) + "K" : r(n);
    }

    function selectSymbol(s) { app.setSymbol(s); app.setView("chart"); }
    function addToWatchlist(s) {
        const l = app.activeList;
        if (!l) { app.toast("info", "Create a watchlist first (Watchlists screen)."); return; }
        api.invoke("wl:add", [l.id, s], function (r) {
            if (!r.ok) app.toast("error", r.error);
            else if (!r.added) app.toast("info", r.symbol + " is already on " + l.name);
            else app.toast("success", r.symbol + " added to " + l.name);
            app.loadLists();
        });
    }
    function saveList(list) {
        app.promptText({ title: "Save as watchlist", label: "Name for the " + list.length + " symbols", initial: active ? active.label : "Screener results", okLabel: "Save",
                         validate: function (v) { return v ? null : "Give the list a name."; } }, function (name) {
            if (!name) return;
            const symbols = list.map(function (r) { return r.symbol; });
            api.invoke("wl:create", [name], function (c) {
                if (!c.ok) { app.toast("error", c.error); return; }
                const next = function (i) {
                    if (i < symbols.length) { api.invoke("wl:add", [c.id, symbols[i]], function () { next(i + 1); }); return; }
                    app.chooseList(c.id);
                    app.toast("success", "Saved " + symbols.length + " symbols to “" + name + "”", { action: { label: "View", onClick: function () { app.setView("watch"); } } });
                    app.loadLists();
                };
                next(0);
            });
        });
    }
    function copyCsv() {
        clip.text = core.callPure("screenerCsv", [sorted]);
        clip.selectAll();
        clip.copy();
        clip.text = "";
        app.toast("success", "Copied " + sorted.length + " rows as CSV");
    }
    function ask() {
        app.askClaude("I ran a stock screen and want your help reading it. Use run_screener with these arguments: " + JSON.stringify(core.callPure("screenerToolArgs", [applied]))
                      + ". Tell me what the screen is actually selecting for, point out anything odd in the results, pick a few names worth a closer look and explain what you would check next (chart, fundamentals, news), without treating the list as buy recommendations.");
    }
    TextEdit { id: clip; visible: false; textFormat: TextEdit.PlainText }

    // ------------------------------------------------------------------ layout
    SvTitleBar {
        id: header
        width: view.width
        title: "Stock Screener"
    }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        error: view.error
        loading: view.live && view.loading && !view.result
        loadingText: "Screening…"
        onRetry: view.load()

        Label95 {
            visible: !view.live
            textFormat: Text.StyledText
            text: "The screener uses Financial Modeling Prep data. <a href=\"settings\">Add your FMP key</a> to use it."
            onLinkActivated: view.app.setView("settings")
        }

        Flow {
            visible: view.live
            width: page.contentWidth
            spacing: 3
            Label95 { height: 22; verticalAlignment: Text.AlignVCenter; dim: true; text: "Start from  " }
            Repeater {
                model: view.presets
                delegate: ToolButton95 {
                    required property var modelData
                    height: 22
                    text: modelData.label
                    checked: view.preset === modelData.id
                    onClicked: { view.preset = modelData.id; view.run(core.callPure("presetFilters", [modelData])); }
                }
            }
            Button95 { height: 22; text: "Reset"; onClicked: { view.preset = ""; view.run(view.defaults); } }
        }
        SvNote { visible: view.live && !!view.active; text: view.active ? view.active.what : "" }

        SvCard {
            visible: view.live
            width: page.contentWidth
            title: "Filters"
            Grid {
                id: form
                width: parent.width
                columns: 4
                columnSpacing: 10
                rowSpacing: 4
                readonly property real cellWidth: (width - 3 * columnSpacing) / 4

                // sector, industry, exchange, country, type, size
                Repeater {
                    model: [
                        { label: "Sector", key: "sector", values: [""].concat(view.opts ? view.opts.sectors : []), names: ["Any"].concat(view.opts ? view.opts.sectors : []) },
                        { label: "Industry", key: "industry", values: [""].concat(view.opts ? view.opts.industries : []), names: ["Any"].concat(view.opts ? view.opts.industries : []) },
                        { label: "Exchange", key: "exchange", values: ["", "NASDAQ", "NYSE", "AMEX"], names: ["Any", "NASDAQ", "NYSE", "AMEX"] },
                        { label: "Country", key: "country", values: [""].concat(view.opts ? view.opts.countries : (view.f.country ? [view.f.country] : [])), names: ["Any"].concat(view.opts ? view.opts.countries : (view.f.country ? [view.f.country] : [])) },
                        { label: "Type", key: "kind", values: ["stocks", "etfs", "both"], names: ["Stocks", "ETFs", "Stocks and ETFs"] }
                    ]
                    delegate: Column {
                        required property var modelData
                        width: form.cellWidth
                        spacing: 1
                        Label95 { text: modelData.label }
                        ComboBox95 {
                            width: parent.width
                            popupMenu: popup
                            model: modelData.names
                            currentIndex: Math.max(0, modelData.values.indexOf(view.f[modelData.key]))
                            onActivated: (i) => { const p = {}; p[modelData.key] = modelData.values[i]; view.set(p); }
                        }
                    }
                }
                Column {
                    width: form.cellWidth
                    spacing: 1
                    Label95 { text: "Size" }
                    ComboBox95 {
                        width: parent.width
                        popupMenu: popup
                        model: view.sizes.map(function (s) { return s[2]; }).concat(["Custom"])
                        currentIndex: { const i = view.sizes.findIndex(function (s) { return s[0] === view.f.capMin && s[1] === view.f.capMax; }); return i >= 0 ? i : view.sizes.length; }
                        onActivated: (i) => { if (i < view.sizes.length) view.set({ capMin: view.sizes[i][0], capMax: view.sizes[i][1] }); }
                    }
                }
                // numbers
                Repeater {
                    model: [
                        { label: "Market cap min", hint: "$M", key: "capMin" }, { label: "Market cap max", hint: "$M", key: "capMax" },
                        { label: "Price min", hint: "$", key: "priceMin" }, { label: "Price max", hint: "$", key: "priceMax" },
                        { label: "Volume min", hint: "shares", key: "volumeMin" }, { label: "Relative volume min", hint: "× avg", key: "relVolMin" },
                        { label: "Beta min", hint: "", key: "betaMin" }, { label: "Beta max", hint: "", key: "betaMax" },
                        { label: "Dividend yield min", hint: "%", key: "yieldMin" }
                    ]
                    delegate: Column {
                        required property var modelData
                        width: form.cellWidth
                        spacing: 1
                        Label95 { textFormat: Text.StyledText; text: modelData.label + (modelData.hint ? " <font color=\"" + Theme.shadow + "\"><i>" + modelData.hint + "</i></font>" : "") }
                        TextField95 {
                            id: numField
                            width: parent.width
                            text: view.f[modelData.key]
                            input.validator: RegularExpressionValidator { regularExpression: /-?[0-9]*\.?[0-9]*/ }
                            onTextChanged: if (text !== view.f[modelData.key]) { const p = {}; p[modelData.key] = text; view.set(p); }
                            onAccepted: if (view.dirty) view.run()
                        }
                    }
                }
                Column {
                    width: form.cellWidth
                    spacing: 1
                    Label95 { text: "Max results" }
                    ComboBox95 {
                        width: parent.width
                        popupMenu: popup
                        model: view.limits.map(String)
                        currentIndex: view.limits.indexOf(view.f.limit)
                        onActivated: (i) => view.set({ limit: view.limits[i] })
                    }
                }
            }
            Item {
                width: parent.width
                height: 25
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    CheckBox95 {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "actively trading only"
                        checked: !!view.f.active
                        onToggled: (c) => view.set({ active: c })
                    }
                    Label95 {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: core.callPure("hasClientFilters", [view.f])
                        dim: true
                        text: "Yield and relative volume are worked out in the app, so it scans up to 3,000 stocks."
                    }
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Label95 {
                        anchors.verticalCenter: parent.verticalCenter
                        dim: true
                        visible: !!view.result && !!view.result.asOf
                        text: view.result && view.result.asOf ? "As of " + Util.clock(view.result.asOf) + " · " + Util.ago(view.result.asOf) + " · cached to limit FMP requests" : ""
                    }
                    Button95 {
                        height: 23
                        text: "Refresh"
                        enabled: !view.loading
                        onClicked: { view.forceNext = true; if (view.dirty) view.run(); else view.load(); }
                        ToolTipArea { text: "Fetch fresh data" }
                    }
                    Button95 {
                        height: 23
                        isDefault: true
                        text: "Run screen"
                        enabled: !view.loading && view.dirty
                        onClicked: view.run()
                    }
                }
            }
        }

        SvCard {
            visible: view.live && !!view.result
            width: page.contentWidth
            title: "Results"
            sub: Util.num(view.rows.length, 0) + " match" + (view.rows.length === 1 ? "" : "es")
                 + (view.result && view.result.rows.length !== view.rows.length ? " of " + Util.num(view.result.rows.length, 0) + " checked" : "")
                 + (view.truncated ? " (more exist: the list is capped, so raise the result limit or add filters)" : "")
            Row {
                spacing: 4
                Button95 { height: 22; text: "Ask Claude"; enabled: view.rows.length > 0; onClicked: view.ask() }
                Button95 { height: 22; text: "Save top 50 as watchlist"; enabled: view.rows.length > 0; onClicked: view.saveList(view.sorted.slice(0, 50)) }
                Button95 { height: 22; text: "Copy CSV"; enabled: view.rows.length > 0; onClicked: view.copyCsv() }
            }
            Flow {
                visible: view.rows.length > 0
                width: parent.width
                spacing: 3
                Label95 { height: 18; verticalAlignment: Text.AlignVCenter; dim: true; text: "Median market cap " + view.cap(view.sum.medianCap) + "  " }
                Repeater {
                    model: view.sum.sectors
                    delegate: Bevel {
                        required property var modelData
                        style: "thinSunken"
                        face: Theme.window
                        width: secText.implicitWidth + 10
                        height: 18
                        Label95 { id: secText; anchors.centerIn: parent; text: parent.modelData.name + "  " + parent.modelData.count }
                    }
                }
            }
            SvNote { visible: view.rows.length === 0; text: "No stocks match. Loosen a filter, for example lower the market cap or volume minimum." }
            SvTable {
                visible: view.rows.length > 0
                width: parent.width
                model: view.sorted.slice(0, view.shownCount)
                sortKey: view.sortKey
                sortDir: view.sortDir
                columns: [
                    { title: "Symbol", w: 64, sort: "symbol", bold: true, link: true, text: function (r) { return r.symbol; }, sub: function (r) { return r.exchange; } },
                    { title: "Company", flex: true, min: 130, sort: "name", text: function (r) { return r.name; }, sub: function (r) { return r.isEtf ? "ETF" : r.isFund ? "fund" : ""; } },
                    { title: "Sector / industry", flex: true, min: 120, text: function (r) { return r.sector || "—"; }, sub: function (r) { return r.industry; } },
                    { title: "Market cap", w: 78, right: true, sort: "marketCap", text: function (r) { return view.cap(r.marketCap); } },
                    { title: "Price", w: 66, right: true, sort: "price", text: function (r) { return r.price !== null ? Util.usd(r.price) : "—"; } },
                    { title: "Volume", w: 54, right: true, sort: "volume", text: function (r) { return view.vol(r.volume); } },
                    { title: "Rel. vol", w: 52, right: true, sort: "relVol", text: function (r) { const v = core.callPure("relVolume", [r]); return v !== null ? v.toFixed(1) + "×" : "—"; },
                      color: function (r) { const v = core.callPure("relVolume", [r]); return v !== null && v >= 2 ? "#007000" : Theme.text; } },
                    { title: "Beta", w: 42, right: true, sort: "beta", text: function (r) { return r.beta !== null ? r.beta.toFixed(2) : "—"; } },
                    { title: "Yield", w: 50, right: true, sort: "yield", text: function (r) { const y = core.callPure("dividendYield", [r]); return y !== null && y > 0 ? y.toFixed(2) + "%" : "—"; } },
                    { title: "", w: 40, link: true, text: function () { return "+ List"; }, tip: "Add to watchlist" }
                ]
                onHeaderClicked: (c) => { const k = columns[c].sort; if (k) view.sortOn(k); }
                onCellClicked: (row, r, col) => {
                    if (col === 0) view.selectSymbol(r.symbol);
                    else if (col === columns.length - 1) view.addToWatchlist(r.symbol);
                }
            }
            Button95 {
                visible: view.sorted.length > view.shownCount
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Show 100 more (" + (view.sorted.length - view.shownCount) + " left)"
                onClicked: view.shownCount += 100
            }
        }
        SvNote {
            visible: view.live
            width: page.contentWidth
            text: "A screen only narrows thousands of stocks to a list worth looking at. It is not a list of things to buy: check the chart, the fundamentals and the news first. Prices and volume are live; the list is sorted by market cap unless you click a column."
        }
    }
}
