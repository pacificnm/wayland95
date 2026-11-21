import QtQuick
import Win95Ui

// Market Performance: sector performance and valuation, every industry (filter and sort), and today's biggest
// gainers, losers and most active stocks with filters that keep penny stocks out. Port of upstream
// renderer/src/views/MarketView.tsx (fmp:market; buildMarket and filterMovers through core.callPure).
Item {
    id: view
    property var app

    readonly property bool live: app ? app.live : false
    readonly property var exchanges: ["NASDAQ", "NYSE", "AMEX"]
    property string exchange: "NASDAQ"
    property string date: ""            // "" = the latest trading day
    property var m: null
    property string error: ""
    property bool loading: false
    property int seq: 0

    function load(force) {
        if (!live) return;
        const id = ++seq;
        loading = true;
        error = "";
        const q = { exchange: exchange, force: !!force };
        if (date) q.date = date;
        api.invoke("fmp:market", [q], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            m = core.callPure("buildMarket", [r.data]);
        });
    }
    onExchangeChanged: load(false)
    onDateChanged: load(false)
    onLiveChanged: load(false)
    Component.onCompleted: load(false)
    Timer { interval: 60000; running: view.live; repeat: true; onTriggered: view.load(false) }

    // ------------------------------------------------------------------ the body (upstream MarketBody)
    property var filter: core.callPure("defaultMoverFilter", [])
    property string q: ""
    property string sortBy: "change"
    property int sortDir: -1

    function pct(n, d) { return n === null || n === undefined ? "—" : (n >= 0 ? "+" : "") + n.toFixed(d === undefined ? 2 : d) + "%"; }
    function tint(n) {
        if (n === null || n === undefined) return "#e0e0e0";
        const a = 0.15 + Math.min(Math.abs(n) / 3, 1) * 0.6;
        return n >= 0 ? Qt.rgba(38 / 255, 166 / 255, 154 / 255, a) : Qt.rgba(239 / 255, 83 / 255, 80 / 255, a);
    }
    readonly property int up: m ? m.sectors.filter(function (s) { return (s.change ?? 0) > 0; }).length : 0
    readonly property real peMax: m ? Math.max.apply(null, [1].concat(m.sectors.map(function (s) { return s.pe ?? 0; }))) : 1
    readonly property var peAvg: {
        if (!m) return null;
        const v = m.sectors.map(function (s) { return s.pe; }).filter(function (x) { return x !== null && x !== undefined; });
        return v.length ? v.reduce(function (a, b) { return a + b; }, 0) / v.length : null;
    }
    readonly property var byPe: m ? m.sectors.filter(function (s) { return s.pe !== null && s.pe !== undefined; }).sort(function (a, b) { return b.pe - a.pe; }) : []
    readonly property var industries: {
        if (!m) return [];
        const t = q.trim().toLowerCase();
        const list = m.industries.filter(function (i) { return !t || i.name.toLowerCase().indexOf(t) >= 0; });
        const by = sortBy, dir = sortDir;
        const val = function (i) { return by === "name" ? i.name.toLowerCase() : by === "pe" ? (i.pe ?? -Infinity) : (i.change ?? -Infinity); };
        return list.slice().sort(function (a, b) { return (val(a) < val(b) ? -1 : val(a) > val(b) ? 1 : 0) * dir; });
    }
    function sortOn(by) {
        if (sortBy === by) sortDir = sortDir === 1 ? -1 : 1;
        else { sortBy = by; sortDir = by === "name" ? 1 : -1; }
    }
    function f(patch) { filter = Object.assign({}, filter, patch); }
    function selectSymbol(s) { app.setSymbol(s); app.setView("chart"); }
    function ask() {
        app.askClaude("Give me a read on the market today using get_market_performance. Which sectors and industries are leading and lagging, what does that say about risk appetite or rotation, how do the P/E ratios compare, and what should a beginner take from it? Also point out anything in the movers lists I should be careful about.");
    }

    SvTitleBar {
        id: header
        width: view.width
        title: "Market Performance"
        Button95 {
            visible: view.live
            height: 22
            text: view.loading ? "Refreshing…" : "Refresh"
            enabled: !view.loading
            onClicked: view.load(true)
        }
    }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        error: view.error
        loading: view.live && view.loading && !view.m
        onRetry: view.load(false)

        Label95 {
            visible: !view.live
            textFormat: Text.StyledText
            text: "Market performance comes from Financial Modeling Prep. <a href=\"settings\">Add your FMP key</a> to see it."
            onLinkActivated: view.app.setView("settings")
        }

        Row {
            visible: view.live
            spacing: 6
            height: 23
            Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Exchange" }
            ComboBox95 {
                width: 90
                anchors.verticalCenter: parent.verticalCenter
                popupMenu: popup
                model: view.exchanges
                currentIndex: view.exchanges.indexOf(view.exchange)
                onActivated: (i) => view.exchange = view.exchanges[i]
            }
            Item { width: 6; height: 1 }
            Label95 { anchors.verticalCenter: parent.verticalCenter; text: "Day" }
            TextField95 {
                id: dayField
                width: 90
                anchors.verticalCenter: parent.verticalCenter
                text: view.date || (view.m ? view.m.date : "")
                onAccepted: {
                    const t = text.trim();
                    if (/^\d{4}-\d{2}-\d{2}$/.test(t) && t <= new Date().toISOString().slice(0, 10)) view.date = t;
                    else text = Qt.binding(function () { return view.date || (view.m ? view.m.date : ""); });
                }
                ToolTipArea { text: "A trading day as YYYY-MM-DD; press Enter" }
            }
            Button95 { visible: view.date !== ""; height: 21; anchors.verticalCenter: parent.verticalCenter; text: "Latest"; onClicked: view.date = "" }
            Label95 {
                anchors.verticalCenter: parent.verticalCenter
                dim: true
                text: view.m && view.m.asOf ? "  Data as of " + Util.dateTime(view.m.asOf) + " · " + Util.ago(view.m.asOf) + " · cached to limit FMP requests" : ""
            }
        }

        Notice {
            visible: !!view.m
            width: page.contentWidth
            kind: "info"
            text: view.m ? (view.m.sectors.length ? view.up + " of " + view.m.sectors.length + " sectors are up on " + view.m.exchange + "-listed stocks. " : "")
                           + "Sector and industry figures are the average change across listed stocks. Movers below are live." : ""
            buttons: [{ text: "Ask Claude for a read", action: function () { view.ask(); } }]
        }

        Grid {
            id: grid
            visible: !!view.m
            width: page.contentWidth
            columns: 2
            spacing: 8
            readonly property real cardWidth: (width - spacing) / 2

            SvCard {
                width: grid.cardWidth
                height: Math.max(implicitHeight, peCard.implicitHeight)
                title: "Sector performance"
                sub: view.m ? view.m.date + " · average change" : ""
                Flow {
                    width: parent.width
                    spacing: 3
                    Repeater {
                        model: view.m ? view.m.sectors : []
                        delegate: Rectangle {
                            required property var modelData
                            width: (parent.width - 6) / 3
                            height: 40
                            color: view.tint(modelData.change)
                            border.color: Theme.shadow
                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 8
                                Label95 { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; small: true; text: modelData.name }
                                Label95 { width: parent.width; horizontalAlignment: Text.AlignHCenter; bold: true; text: view.pct(modelData.change) }
                            }
                            ToolTipArea { text: parent.modelData.name + ": " + view.pct(parent.modelData.change) + (parent.modelData.pe !== null ? " · P/E " + parent.modelData.pe.toFixed(1) : "") }
                        }
                    }
                }
                Label95 { visible: !!view.m && view.m.sectors.length === 0; dim: true; text: "No sector data for this day and exchange." }
            }

            SvCard {
                id: peCard
                width: grid.cardWidth
                title: "Sector valuation"
                sub: "price / earnings" + (view.peAvg !== null ? " · average of sectors " + view.peAvg.toFixed(1) : "")
                Label95 { visible: view.byPe.length === 0; dim: true; text: "No P/E data." }
                Column {
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: view.byPe
                        delegate: Item {
                            required property var modelData
                            width: parent.width
                            height: 15
                            Label95 { id: peName; width: 130; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter; text: parent.modelData.name }
                            Bevel {
                                anchors.left: peName.right
                                anchors.right: peVal.left
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                height: 10
                                style: "thinSunken"
                                face: Theme.window
                                Rectangle { x: 1; y: 1; height: parent.height - 2; width: (parent.width - 2) * parent.parent.modelData.pe / view.peMax; color: Theme.selection }
                                Rectangle {
                                    visible: view.peAvg !== null
                                    x: (parent.width - 2) * (view.peAvg ?? 0) / view.peMax
                                    y: -2
                                    width: 2
                                    height: parent.height + 4
                                    color: "#c07000"
                                }
                            }
                            Label95 { id: peVal; anchors.right: parent.right; width: 36; horizontalAlignment: Text.AlignRight; anchors.verticalCenter: parent.verticalCenter; bold: true; text: parent.modelData.pe.toFixed(1) }
                            ToolTipArea { text: parent.modelData.name + ": P/E " + parent.modelData.pe.toFixed(1) }
                        }
                    }
                }
                SvNote { text: "A high P/E means investors pay more per dollar of earnings, usually for expected growth. It is a comparison, not a verdict: sectors differ structurally. The orange mark is the average of sectors." }
            }
        }

        SvCard {
            visible: !!view.m
            width: page.contentWidth
            title: "Industries"
            sub: view.m ? view.industries.length + " of " + view.m.industries.length : ""
            TextField95 {
                id: indFilter
                width: 200
                onTextChanged: view.q = text
                Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; dim: true; visible: indFilter.text === ""; text: "Filter industries…" }
            }
            Item {
                width: parent.width
                height: Math.min(indTable.height, 320)
                Flickable {
                    id: indFlick
                    width: parent.width - (indBar.visible ? indBar.width : 0)
                    height: parent.height
                    clip: true
                    interactive: false
                    contentHeight: indTable.height
                    SvTable {
                        id: indTable
                        width: parent.width
                        model: view.industries
                        sortKey: view.sortBy
                        sortDir: view.sortDir
                        columns: [
                            { title: "Industry", flex: true, min: 200, sort: "name", text: function (i) { return i.name; } },
                            { title: "Change", w: 70, right: true, sort: "change", text: function (i) { return view.pct(i.change); }, color: function (i) { return Util.tone(i.change); } },
                            { title: "", w: 150, diverging: true, bar: function (i) { return Math.min(1, Math.abs(i.change ?? 0) / 4) * ((i.change ?? 0) >= 0 ? 1 : -1); },
                              barColor: function (i) { return (i.change ?? 0) >= 0 ? "#26a69a" : "#ef5350"; } },
                            { title: "P/E", w: 60, right: true, sort: "pe", text: function (i) { return i.pe !== null && i.pe !== undefined ? i.pe.toFixed(1) : "—"; } }
                        ]
                        onHeaderClicked: (c) => { const s = columns[c].sort; if (s) view.sortOn(s); }
                    }
                    WheelHandler {
                        onWheel: (event) => { indFlick.contentY = Math.max(0, Math.min(indFlick.contentHeight - indFlick.height, indFlick.contentY - event.angleDelta.y / 120 * 54)); }
                    }
                }
                ScrollBar95 { id: indBar; anchors.right: parent.right; height: parent.height; flickable: indFlick; stepSize: 18 }
            }
        }

        // movers and their filters
        Bevel {
            visible: !!view.m
            width: page.contentWidth
            height: moverBar.height + 10
            style: "thinRaised"
            Flow {
                id: moverBar
                x: 6
                y: 5
                width: parent.width - 12
                spacing: 8
                Heading95 { level: 3; text: "Movers"; height: 21; verticalAlignment: Text.AlignVCenter }
                Label95 { text: "Min price"; height: 21; verticalAlignment: Text.AlignVCenter }
                ComboBox95 {
                    width: 70
                    popupMenu: popup
                    readonly property var values: [0, 1, 5, 10, 20]
                    model: ["Any", "$1+", "$5+", "$10+", "$20+"]
                    currentIndex: values.indexOf(view.filter.minPrice)
                    onActivated: (i) => view.f({ minPrice: values[i] })
                }
                Label95 { text: "Exchange"; height: 21; verticalAlignment: Text.AlignVCenter }
                ComboBox95 {
                    width: 90
                    popupMenu: popup
                    readonly property var values: ["all", "NASDAQ", "NYSE", "AMEX"]
                    model: ["All", "NASDAQ", "NYSE", "AMEX"]
                    currentIndex: values.indexOf(view.filter.exchange)
                    onActivated: (i) => view.f({ exchange: values[i] })
                }
                CheckBox95 {
                    height: 21
                    text: "Hide funds & ETFs"
                    checked: !!view.filter.hideFunds
                    onToggled: (c) => view.f({ hideFunds: c })
                }
                SvNote {
                    width: moverBar.width
                    text: "Raw lists are dominated by illiquid penny stocks, which move a lot on tiny volume. They are hard to trade and easy to lose money in."
                }
            }
        }

        Row {
            id: movers
            visible: !!view.m
            width: page.contentWidth
            spacing: 8
            readonly property real cardWidth: (width - 2 * spacing) / 3
            readonly property var lists: view.m ? [
                { title: "Biggest gainers", sub: "today", list: core.callPure("filterMovers", [view.m.gainers, view.filter]) },
                { title: "Biggest losers", sub: "today", list: core.callPure("filterMovers", [view.m.losers, view.filter]) },
                { title: "Most active", sub: "top traded today", list: core.callPure("filterMovers", [view.m.actives, view.filter]) }
            ] : []
            readonly property real tallest: lists.reduce(function (h, l) { return Math.max(h, l.list.length); }, 0)
            Repeater {
                model: movers.lists
                delegate: SvCard {
                    required property var modelData
                    width: movers.cardWidth
                    height: 30 + Math.max(1, movers.tallest) * 30 + 8
                    title: modelData.title
                    sub: modelData.sub
                    SvTable {
                        width: parent.width
                        showHeader: false
                        model: parent.parent.modelData.list
                        emptyText: "Nothing matches your filters."
                        columns: [
                            { title: "Symbol", flex: true, min: 70, bold: true, link: true, text: function (m) { return m.symbol; }, sub: function (m) { return m.name; } },
                            { title: "Price", w: 64, right: true, text: function (m) { return m.price !== null ? Util.usd(m.price) : "—"; } },
                            { title: "Change", w: 58, right: true, text: function (m) { return view.pct(m.pct); }, color: function (m) { return Util.tone(m.pct); } }
                        ]
                        onCellClicked: (row, m) => view.selectSymbol(m.symbol)
                    }
                }
            }
        }
    }
}
