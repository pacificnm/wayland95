import QtQuick
import Win95Ui

// The Chart screen: symbol search, the watchlist drop-down, the index ticker, range/interval/chart type, studies,
// drawing tools, the watchlist star, Buy/Sell (the order ticket opens over the chart), "Updated hh:mm:ss", the
// sample-data notice, and the chart itself (ChartView, the C++ item). Port of renderer/src/views/ChartView.tsx with
// components/SymbolSearch.tsx, WatchlistDropdown.tsx, StarMenu.tsx and IndexTicker.tsx. The data is app.chartData.
Item {
    id: view
    property var app

    readonly property var cd: app ? app.chartData : null
    readonly property var settings: app ? app.chartSettings : core.defaultChartSettings()
    readonly property bool live: app ? app.live : false
    function setSettings(patch) { app.updateChartSettings(Object.assign({}, settings, patch)); }

    readonly property var intervalInfo: {
        const m = {};
        core.intervals().forEach(function (i) { m[i.id] = i; });
        return m;
    }
    readonly property var typeNames: ({ candles: "Candles", hollow: "Hollow candles", bars: "Bars", line: "Line", area: "Area", heikin: "Heikin Ashi" })
    readonly property var tools: [
        { id: "cursor", label: "Cursor (select / edit drawings)" },
        { id: "trend", label: "Trendline" },
        { id: "ray", label: "Ray" },
        { id: "hline", label: "Horizontal line" },
        { id: "fib", label: "Fibonacci retracement" },
        { id: "rect", label: "Rectangle" }
    ]
    readonly property var categories: ["Overlay", "Trend", "Momentum", "Volatility", "Volume"]

    function popupAt(item, items) {
        const p = item.mapToItem(popup, 0, item.height);
        popup.popup(p.x, p.y, items);
    }

    // ------------------------------------------------------------------ studies menu (add, hide/show, edit, remove)
    function studiesMenu() {
        const all = core.studies();
        const items = view.categories.map(function (cat) {
            return {
                text: "Add " + cat,
                submenu: all.filter(function (s) { return s.category === cat; }).map(function (s) {
                    return { text: s.name, action: function () { view.setSettings({ studies: view.settings.studies.concat([core.newStudyInstance(s.id, {})]) }); } };
                })
            };
        });
        const active = view.settings.studies || [];
        if (active.length) items.push({ separator: true });
        active.forEach(function (inst) {
            items.push({
                text: core.studyLabel(inst.studyId, inst.params || {}) + (inst.visible ? "" : " (hidden)"),
                submenu: [
                    { text: inst.visible ? "Hide" : "Show", action: function () { view.patchStudy(inst.uid, function (s) { s.visible = !s.visible; return s; }); } },
                    { text: "Parameters and Colors...", action: function () { view.editStudy(inst.uid); } },
                    { text: "Remove", action: function () { view.setSettings({ studies: view.settings.studies.filter(function (s) { return s.uid !== inst.uid; }) }); } }
                ]
            });
        });
        items.push({ separator: true });
        items.push({ text: "Chart Settings...", action: function () { view.app.setView("chartSettings"); } });
        return items;
    }
    function patchStudy(uid, fn) {
        setSettings({ studies: settings.studies.map(function (s) { return s.uid === uid ? fn(Object.assign({}, s)) : s; }) });
    }
    property string editingUid: ""
    function editStudy(uid) { editingUid = uid; studyDialog.show(); }

    // ------------------------------------------------------------------ drawing tools menu
    function drawingsMenu() {
        const list = cd ? cd.drawings : [];
        return view.tools.map(function (t) {
            return { text: t.label, checked: chart.tool === t.id, action: function () { chart.tool = t.id; } };
        }).concat([
            { separator: true },
            { text: "Delete selected drawing", shortcut: "Del", enabled: chart.selectedId !== "", action: function () { chart.deleteSelected(); } },
            { text: "Remove Claude's drawings", enabled: list.some(function (d) { return d.by === "claude"; }),
              action: function () { view.cd.updateDrawings(function (d) { return d.filter(function (x) { return x.by !== "claude"; }); }); } },
            { text: "Remove all drawings", enabled: list.length > 0, action: function () { view.cd.updateDrawings(function () { return []; }); } }
        ]);
    }

    // ------------------------------------------------------------------ watchlists (WatchlistDropdown, StarMenu)
    readonly property var activeList: app ? app.activeList : null
    readonly property var wlSymbols: activeList ? activeList.symbols : []
    readonly property bool onAnyList: app ? app.lists.some(function (l) { return l.symbols.indexOf(app.symbol) >= 0; }) : false

    function openWatchlistMenu(anchor) {
        const show = function (quotes) {
            const items = [];
            const lists = view.app.lists;
            if (lists.length > 1) {
                lists.forEach(function (l) {
                    items.push({ text: l.name, checked: view.activeList && l.id === view.activeList.id, action: function () { view.app.chooseList(l.id); } });
                });
                items.push({ separator: true });
            }
            if (view.wlSymbols.length === 0)
                items.push({ text: "This list is empty. Use the star next to the chart title to add the current symbol.", enabled: false });
            view.wlSymbols.forEach(function (s) {
                const q = quotes[s];
                const price = q && q.price !== null ? q.price.toFixed(2) : "";
                const pct = q && q.pct !== null ? (q.pct >= 0 ? "+" : "") + q.pct.toFixed(2) + "%" : "";
                items.push({ text: s, shortcut: (price + "  " + pct).trim(), checked: s === view.app.symbol, action: function () { view.app.setSymbol(s); } });
            });
            items.push({ separator: true });
            items.push({ text: "Manage watchlist…", action: function () { view.app.openWatchlists(); } });
            view.popupAt(anchor, items);
        };
        // prices are fetched when the menu opens (the client caches them briefly)
        if (!live || wlSymbols.length === 0) { show({}); return; }
        api.invoke("fmp:quotes", [wlSymbols], function (r) {
            const quotes = {};
            if (r && r.ok) r.data.forEach(function (q) {
                quotes[core.pickStr(q, ["symbol"])] = { price: core.pick(q, ["price"]), pct: core.pick(q, ["changePercentage", "changesPercentage"]) };
            });
            show(quotes);
        });
    }

    function openStarMenu(anchor) {
        const sym = app.symbol;
        const items = [{ text: "Add " + sym + " to…", enabled: false }];
        app.lists.forEach(function (l) {
            const on = l.symbols.indexOf(sym) >= 0;
            items.push({ text: l.name, checked: on, shortcut: String(l.symbols.length), action: function () { view.app.toggleInList(l.id, !on); } });
        });
        items.push({ separator: true });
        items.push({ text: "New watchlist…", action: function () {
            view.app.promptText({ title: "New watchlist", label: "Name", placeholder: "New watchlist…", okLabel: "Create",
                                  validate: function (v) { return v ? null : "Give the list a name."; } },
                                function (name) { if (name) view.app.createList(name, sym); });
        } });
        popupAt(anchor, items);
    }

    // ------------------------------------------------------------------ header: tabs, symbol, price; range, interval, star, Buy/Sell
    SymbolHeader {
        id: header
        app: view.app
        width: parent.width
        title: "Chart"

        ComboBox95 {
            id: rangeBox
            width: 72
            anchors.verticalCenter: parent.verticalCenter
            popupMenu: popup
            readonly property var ranges: view.app ? (view.app.unavailable, core.availableRanges()) : []
            model: ranges.map(function (r) { return r.label; })
            currentIndex: Math.max(0, ranges.findIndex(function (r) { return r.id === view.settings.range; }))
            onActivated: (i) => {
                const range = ranges[i].id;
                view.setSettings(core.usableTimeframe(range, view.settings.interval));
            }
            ToolTipArea { text: "Length" }
        }
        ComboBox95 {
            id: intervalBox
            width: 76
            anchors.verticalCenter: parent.verticalCenter
            popupMenu: popup
            readonly property var intervals: view.app ? (view.app.unavailable, core.allowedIntervals(view.settings.range)) : []
            model: intervals.map(function (r) { return r.label; })
            currentIndex: Math.max(0, intervals.findIndex(function (r) { return r.id === view.settings.interval; }))
            onActivated: (i) => view.setSettings({ interval: intervals[i].id })
            ToolTipArea { text: "Interval" }
        }
        ToolButton95 {
            id: starButton
            width: 24
            height: 22
            anchors.verticalCenter: parent.verticalCenter
            checked: false
            onClicked: view.openStarMenu(starButton)
            Text {
                anchors.centerIn: parent
                text: view.onAnyList ? "★" : "☆"
                color: view.onAnyList ? "#c08000" : Theme.text
                font.pixelSize: 15
            }
            ToolTipArea { text: "Add " + (view.app ? view.app.symbol : "") + " to a watchlist" }
        }
        Button95 { width: 50; height: 22; anchors.verticalCenter: parent.verticalCenter; text: "Buy"; onClicked: view.app.openTicket("buy") }
        Button95 { width: 50; height: 22; anchors.verticalCenter: parent.verticalCenter; text: "Sell"; onClicked: view.app.openTicket("sell") }
    }

    // ------------------------------------------------------------------ toolbar: search, watchlist, chart type, studies, drawing tools
    Bevel {
        id: toolbar
        y: header.height + 2
        width: parent.width
        height: 30
        style: "raised"
        clip: true

        Row {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            // SymbolSearch
            TextField95 {
                id: searchField
                width: 130
                property int idx: 0
                property bool open: false
                property var remote: []
                readonly property string q: text.trim().toUpperCase()
                readonly property var results: {
                    if (!q) return [];
                    if (view.live && remote.length) return remote;
                    const hits = view.knownSymbols.filter(function (k) { return k[0].indexOf(q) === 0 || k[1].toUpperCase().indexOf(q) >= 0; }).slice(0, 8);
                    // any typed ticker is allowed; the data feed will validate it later
                    return hits.some(function (k) { return k[0] === q; }) ? hits : hits.concat([[q, "Go to symbol"]]);
                }
                function pick(s) { view.app.setSymbol(s); text = ""; open = false; idx = 0; focus = false; }
                onTextChanged: { open = activeFocus; idx = 0; searchTimer.restart(); }
                onActiveFocusChanged: if (!activeFocus) closeTimer.restart(); else open = q !== ""
                onAccepted: if (results[idx]) pick(results[idx][0])
                onEscaped: { open = false; focus = false; }
                Keys.onDownPressed: idx = Math.min(idx + 1, results.length - 1)
                Keys.onUpPressed: idx = Math.max(idx - 1, 0)
                Timer {
                    id: searchTimer
                    interval: 250
                    onTriggered: {
                        const q = searchField.q;
                        if (!view.live || !q) { searchField.remote = []; return; }
                        api.invoke("fmp:search", [q], function (r) {
                            if (q !== searchField.q || !r || !r.ok) return;
                            searchField.remote = r.data.map(function (h) { return [h.symbol, h.name + (h.exchange ? " · " + h.exchange : "")]; });
                        });
                    }
                }
                Timer { id: closeTimer; interval: 150; onTriggered: searchField.open = false }
                Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; visible: searchField.text === ""; text: "Search symbol…"; dim: true }
            }

            // WatchlistDropdown
            ToolButton95 {
                id: wlButton
                width: 130
                height: 22
                onClicked: view.openWatchlistMenu(wlButton)
                Row {
                    x: 5
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text { text: view.onAnyList ? "★" : "☆"; color: view.onAnyList ? "#c08000" : Theme.text; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                    Label95 { width: 70; elide: Text.ElideRight; text: view.activeList ? view.activeList.name : "Watchlist"; anchors.verticalCenter: parent.verticalCenter }
                    Label95 { text: String(view.wlSymbols.length); dim: true; anchors.verticalCenter: parent.verticalCenter }
                }
                Text { anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter; text: "▾"; font.pixelSize: 10; color: Theme.text }
                ToolTipArea { text: "Watchlist" }
            }

            Sep {}
            ComboBox95 {
                width: 110
                anchors.verticalCenter: parent.verticalCenter
                popupMenu: popup
                readonly property var types: core.chartTypes()
                model: types.map(function (t) { return view.typeNames[t] || t; })
                currentIndex: Math.max(0, types.indexOf(view.settings.type))
                onActivated: (i) => view.setSettings({ type: types[i] })
                ToolTipArea { text: "Chart type" }
            }
            ToolButton95 {
                id: studiesButton
                height: 22
                text: "Studies ▾"
                onClicked: view.popupAt(studiesButton, view.studiesMenu())
                ToolTipArea { text: "Add, hide, edit or remove studies" }
            }
            Sep {}
            Repeater {
                model: view.tools
                delegate: ToolButton95 {
                    required property var modelData
                    width: 24
                    height: 22
                    checked: chart.tool === modelData.id
                    onClicked: chart.tool = modelData.id
                    ToolGlyph { anchors.centerIn: parent; kind: parent.modelData.id }
                    ToolTipArea { text: parent.modelData.label }
                }
            }
            ToolButton95 {
                id: drawMenuButton
                width: 16
                height: 22
                text: "▾"
                onClicked: view.popupAt(drawMenuButton, view.drawingsMenu())
                ToolTipArea { text: "Drawing tools" }
            }
        }

        // "Updated 10:32:05 AM" / "Update failed, retrying"
        Label95 {
            id: updated
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: !!(view.live && view.cd && view.cd.checkedAt !== null)
            readonly property bool failed: !!(view.cd && view.cd.refreshError !== null)
            text: !visible ? "" : failed ? "Update failed, retrying"
                                         : "Updated " + new Date(view.cd.checkedAt).toLocaleTimeString(Qt.locale("en_US"), Util.hour12 ? "h:mm:ss AP" : "HH:mm:ss")
            color: failed ? "#b00000" : Theme.shadow
            ToolTipArea {
                text: updated.failed ? "Could not update: " + view.cd.refreshError + ". It will try again."
                                     : "The chart refreshes itself while the market is open. How often depends on the interval."
            }
        }
    }

    // ------------------------------------------------------------------ IndexTicker
    readonly property var indexQuotes: [
        { symbol: "^GSPC", name: "S&P 500", price: 5850.2, change: 21.4, pct: 0.37 },
        { symbol: "^DJI", name: "Dow Jones", price: 42310.5, change: -85.3, pct: -0.2 },
        { symbol: "^IXIC", name: "Nasdaq", price: 18420.7, change: 96.8, pct: 0.53 },
        { symbol: "^RUT", name: "Russell 2000", price: 2210.9, change: -6.1, pct: -0.28 },
        { symbol: "^VIX", name: "VIX", price: 15.62, change: -0.34, pct: -2.13 },
        { symbol: "^FTSE", name: "FTSE 100", price: 8290.4, change: 12.7, pct: 0.15 },
        { symbol: "^GDAXI", name: "DAX", price: 19040.1, change: 74.2, pct: 0.39 },
        { symbol: "^N225", name: "Nikkei 225", price: 38450.6, change: -190.5, pct: -0.49 }
    ]
    readonly property var knownSymbols: [
        ["AAPL", "Apple Inc."], ["MSFT", "Microsoft Corp."], ["NVDA", "NVIDIA Corp."], ["TSLA", "Tesla Inc."],
        ["AMZN", "Amazon.com Inc."], ["GOOGL", "Alphabet Inc."], ["META", "Meta Platforms"], ["AMD", "Advanced Micro Devices"],
        ["NFLX", "Netflix Inc."], ["JPM", "JPMorgan Chase"], ["V", "Visa Inc."], ["WMT", "Walmart Inc."],
        ["DIS", "Walt Disney Co."], ["BA", "Boeing Co."], ["INTC", "Intel Corp."], ["COST", "Costco Wholesale"],
        ["SPY", "SPDR S&P 500 ETF"], ["QQQ", "Invesco QQQ Trust"], ["DIA", "SPDR Dow Jones ETF"], ["IWM", "iShares Russell 2000 ETF"]
    ]
    property var tickerQuotes: indexQuotes
    function loadTicker() {
        if (!live) { tickerQuotes = indexQuotes; return; }
        api.invoke("fmp:quotes", [indexQuotes.map(function (q) { return q.symbol; })], function (r) {
            if (!r || !r.ok || r.data.length === 0) return;
            const names = {};
            view.indexQuotes.forEach(function (q) { names[q.symbol] = q.name; });
            view.tickerQuotes = r.data.map(function (q) {
                const symbol = core.pickStr(q, ["symbol"]);
                return { symbol: symbol, name: names[symbol] || core.pickStr(q, ["name"]) || symbol, price: core.pick(q, ["price"]) || 0,
                         change: core.pick(q, ["change"]) || 0, pct: core.pick(q, ["changePercentage", "changesPercentage"]) || 0 };
            });
        });
    }
    onLiveChanged: loadTicker()
    Timer { interval: 60000; running: view.live; repeat: true; onTriggered: view.loadTicker() }

    Bevel {
        id: ticker
        y: toolbar.y + toolbar.height + 2
        width: parent.width
        height: 20
        style: "thinSunken"
        clip: true
        Row {
            id: track
            y: 2
            height: parent.height - 4
            // the list is drawn twice and moved by half its width for a seamless loop
            Repeater {
                model: view.tickerQuotes.concat(view.tickerQuotes)
                delegate: Item {
                    required property var modelData
                    width: tickRow.implicitWidth + 24
                    height: track.height
                    Row {
                        id: tickRow
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Label95 { text: parent.parent.modelData.name; bold: true }
                        Label95 { text: core.grouped(parent.parent.modelData.price, 2) }
                        Label95 {
                            readonly property var q: parent.parent.modelData
                            text: (q.change >= 0 ? "▲ " : "▼ ") + core.grouped(Math.abs(q.change), 2) + " (" + (q.pct >= 0 ? "+" : "") + Number(q.pct).toFixed(2) + "%)"
                            color: q.change >= 0 ? "#007000" : "#b00000"
                        }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: view.app.setSymbol(parent.modelData.symbol) }
                    ToolTipArea { text: parent.modelData.symbol }
                }
            }
            NumberAnimation on x {
                from: 0
                to: -track.width / 2
                duration: Math.max(1000, track.width / 2 * 30)
                loops: Animation.Infinite
                running: track.width > 0
                paused: tickerHover.hovered
            }
        }
        HoverHandler { id: tickerHover }
    }

    // ------------------------------------------------------------------ the chart well
    Bevel {
        id: well
        y: ticker.y + ticker.height + 3
        width: parent.width
        height: parent.height - y
        style: "sunken"
        face: "#1e1e1e"

        PriceChart {
            id: chart
            x: 2
            y: 2
            width: parent.width - 4
            height: parent.height - 4
            visible: !!(view.cd && view.cd.candles)
            candles: view.cd && view.cd.candles ? view.cd.candles : []
            settings: view.settings
            dataKey: view.cd && view.cd.loadedKey ? view.cd.loadedKey : ""
            symbol: dataKey ? dataKey.split("|")[0] : ""
            intraday: !!(view.intervalInfo[view.settings.interval] && view.intervalInfo[view.settings.interval].intraday)
            timeZone: view.app ? view.app.display.timeZone || "" : ""
            hour12: view.app ? view.app.display.hour12 !== false : true
            drawings: view.cd ? view.cd.drawings : []
            orderLines: view.app ? view.app.orderLines : []
            onDrawingsEdited: (list) => view.cd.updateDrawings(function () { return list; })
            onOrderLineMoved: (id, price) => view.app.moveOrderLine(id, price)
        }
        Binding { target: view.cd; property: "busy"; value: chart.busy; when: view.cd !== null }

        // notes over the chart: sample data, loading, a load error
        Rectangle {
            id: note
            // bottom centre of the price pane, clear of the legend
            x: Math.max(8, (parent.width - 70 - width) / 2)
            y: Math.max(8, (parent.height - 26) * 0.66 - height - 8)
            z: 5
            readonly property string kind: !view.live ? "sample" : view.cd && view.cd.error ? "error" : view.cd && view.cd.loading ? "loading" : ""
            visible: kind !== ""
            width: noteRow.implicitWidth + 16
            height: 26
            color: kind === "error" ? "#ffe8e8" : "#ffffe1"
            border.color: Theme.dark
            Row {
                id: noteRow
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Icon95 { anchors.verticalCenter: parent.verticalCenter; name: note.kind === "error" ? "error" : "info" }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#000000"
                    text: note.kind === "sample" ? "Sample data — add your FMP key in Settings"
                        : note.kind === "loading" ? "Loading…"
                        : note.kind === "error" ? String(view.cd.error) : ""
                    font.underline: note.kind === "sample" && sampleMouse.containsMouse
                }
                Button95 {
                    visible: note.kind === "error"
                    width: 60
                    height: 20
                    text: "Retry"
                    onClicked: view.cd.reload()
                }
            }
            MouseArea {
                id: sampleMouse
                anchors.fill: parent
                visible: note.kind === "sample"
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: view.app.setView("settings")
            }
        }

        // the order ticket, over the chart
        Loader {
            id: ticketLoader
            x: 8
            y: 8
            z: 10
            active: !!(view.app && view.app.ticket)
            source: "TradeTicket.qml"
            onLoaded: {
                item.app = view.app;
                item.maxHeight = Qt.binding(function () { return well.height - 16; });
            }
        }
    }

    // the search results, over everything below the toolbar
    Rectangle {
        id: results
        visible: searchField.open && searchField.results.length > 0
        z: 50
        x: toolbar.x + 4
        y: toolbar.y + toolbar.height - 3
        width: 300
        height: resultsColumn.implicitHeight + 4
        color: Theme.window
        border.color: Theme.dark
        Column {
            id: resultsColumn
            x: 2
            y: 2
            width: parent.width - 4
            Repeater {
                model: searchField.results
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: resultsColumn.width
                    height: 20
                    color: index === searchField.idx ? Theme.selection : "transparent"
                    Label95 { x: 4; anchors.verticalCenter: parent.verticalCenter; width: 70; text: parent.modelData[0]; bold: true; color: parent.index === searchField.idx ? Theme.selectionText : Theme.text }
                    Label95 {
                        x: 78
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 82
                        elide: Text.ElideRight
                        text: parent.modelData[1]
                        color: parent.index === searchField.idx ? Theme.selectionText : Theme.shadow
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: searchField.idx = parent.index
                        onPressed: searchField.pick(parent.modelData[0])
                    }
                }
            }
        }
    }

    // Edit a study's parameters and colours (the same block as in Chart Settings)
    Dialog95 {
        id: studyDialog
        title: "Study"
        dialogWidth: 440
        dialogHeight: studyCard.implicitHeight + 80
        readonly property var inst: view.settings.studies.find(function (s) { return s.uid === view.editingUid; }) || null
        onInstChanged: if (visible && !inst) close()
        ChartStudyCard {
            id: studyCard
            x: 8
            y: 8
            width: 440 - 24
            visible: studyDialog.inst !== null
            inst: studyDialog.inst
            studies: view.settings.studies
            onEdited: (next) => view.patchStudy(next.uid, function () { return next; })
            onRemoved: { const uid = inst.uid; studyDialog.close(); view.setSettings({ studies: view.settings.studies.filter(function (s) { return s.uid !== uid; }) }); }
        }
        Button95 {
            x: 440 - 24 - width + 8
            y: studyCard.y + studyCard.height + 10
            text: "Close"
            isDefault: true
            onClicked: studyDialog.close()
        }
    }

    // ------------------------------------------------------------------ small parts
    component Sep: Item {
        width: 6
        height: 22
        Rectangle { x: 2; y: 1; width: 1; height: 20; color: Theme.shadow }
        Rectangle { x: 3; y: 1; width: 1; height: 20; color: Theme.highlight }
    }
    // the drawing tools' pictures
    component ToolGlyph: Canvas {
        property string kind: "cursor"
        width: 16
        height: 16
        onKindChanged: requestPaint()
        onPaint: {
            const c = getContext("2d");
            c.reset();
            c.strokeStyle = "#000000";
            c.fillStyle = "#000000";
            c.lineWidth = 1.5;
            const dot = function (x, y) { c.beginPath(); c.arc(x, y, 1.8, 0, Math.PI * 2); c.fill(); };
            c.beginPath();
            if (kind === "cursor") {
                c.moveTo(4, 2); c.lineTo(4, 13); c.lineTo(7, 10); c.lineTo(9.5, 14.5); c.lineTo(11, 13.7); c.lineTo(8.6, 9.3); c.lineTo(12.5, 9.3); c.closePath();
                c.lineWidth = 1;
                c.stroke();
            } else if (kind === "trend") {
                c.moveTo(3, 13); c.lineTo(13, 3); c.stroke(); dot(3, 13); dot(13, 3);
            } else if (kind === "ray") {
                c.moveTo(3, 13); c.lineTo(14, 2); c.stroke(); dot(3, 13);
                c.beginPath(); c.moveTo(14, 2); c.lineTo(9, 3); c.lineTo(13, 7); c.closePath(); c.fill();
            } else if (kind === "hline") {
                c.moveTo(1, 8); c.lineTo(15, 8); c.stroke();
            } else if (kind === "fib") {
                c.lineWidth = 1;
                [3, 6, 9, 13].forEach(function (y) { c.moveTo(2, y + 0.5); c.lineTo(14, y + 0.5); });
                c.stroke();
            } else if (kind === "rect") {
                c.lineWidth = 1;
                c.rect(2.5, 4.5, 11, 8);
                c.stroke();
            }
        }
    }

    // app (and so app.chartData) is set just after this screen is created
    onCdChanged: if (cd) cd.chartItem = chart
    Component.onCompleted: {
        if (cd) cd.chartItem = chart;
        loadTicker();
        if (testScript === "chart-demo" || testScript === "chart-ticket") testTimer.start();
    }
    Component.onDestruction: if (cd && cd.chartItem === chart) cd.chartItem = null

    // W95_TRADINGLAB_TEST=chart-demo (studies and drawings) or chart-ticket (and a bracket ticket), for the screenshots
    Timer {
        id: testTimer
        interval: 2500
        onTriggered: {
            const c = view.cd.candles;
            if (!c || c.length < 40) { restart(); return; }
            const s = view.settings.studies.filter(function (x) { return x.studyId === "volume"; });
            view.setSettings({ type: "candles", studies: s.concat([core.newStudyInstance("sma", { length: 20 }), core.newStudyInstance("bb", {}), core.newStudyInstance("rsi", {})]) });
            const n = c.length;
            let lo = n - 60, hi = n - 60;
            for (let i = n - 60; i < n; ++i) { if (c[i].low < c[lo].low) lo = i; if (c[i].high > c[hi].high) hi = i; }
            const a = Math.min(lo, hi), b = Math.max(lo, hi);
            view.cd.updateDrawings(function () {
                return [
                    { id: "t1", type: "trend", p1: { time: c[a].time, price: a === lo ? c[a].low : c[a].high }, p2: { time: c[b].time, price: b === lo ? c[b].low : c[b].high }, color: "#2196f3", label: "swing" },
                    { id: "f1", type: "fib", p1: { time: c[n - 120].time, price: c[hi].high }, p2: { time: c[n - 80].time, price: c[lo].low }, color: "#ab47bc" },
                    { id: "m1", type: "marker", p1: { time: c[n - 10].time, price: c[n - 10].high }, p2: { time: c[n - 10].time, price: c[n - 10].high }, color: "#ffd54f", label: "Claude", by: "claude" }
                ];
            });
            if (testScript === "chart-ticket") {
                const last = c[n - 1].close;
                const r2 = function (x) { return String(Math.round(x * 100) / 100); };
                view.app.ticket = Object.assign(core.emptyDraft("buy"), { qty: "20", type: "limit", limit: r2(last * 0.99), strategy: "bracket",
                                                                       target: r2(last * 1.06), protectStop: r2(last * 0.96), journal: true });
            }
        }
    }
}
