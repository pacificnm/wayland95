import QtQuick
import Win95Ui

// Market News: market-wide headlines, or news for the symbols on a watchlist or in the paper account's positions and
// working orders, with a filter box. Port of upstream renderer/src/views/MarketNewsView.tsx (fmp:news). Headlines
// open in the browser; thumbnails are not shown (Wayland 95 does not load remote images, see SvNewsList).
Item {
    id: view
    property var app

    readonly property bool live: app ? app.live : false
    readonly property int pageSize: 25
    property string tab: "market"
    property int listId: -1
    property var items: []
    property bool loading: false
    property string error: ""
    property bool more: true
    property string filter: ""
    property int pageNo: 0
    property int seq: 0

    readonly property var lists: app ? app.lists : []
    readonly property var list: lists.find(function (l) { return l.id === listId; }) || (app ? app.activeList : null) || lists[0] || null
    readonly property var heldSymbols: {
        const snap = app ? app.snap : null;
        const out = [];
        (snap ? snap.positions : []).forEach(function (p) { if (out.indexOf(p.symbol) < 0) out.push(p.symbol); });
        (snap ? snap.orders : []).filter(function (o) { return o.status === "working" || o.status === "pending"; })
            .forEach(function (o) { if (out.indexOf(o.symbol) < 0) out.push(o.symbol); });
        return out;
    }
    readonly property var symbols: tab === "watchlist" ? (list ? list.symbols : []) : tab === "positions" ? heldSymbols : []
    readonly property string key: tab + "|" + symbols.join(",")

    function mergeNews(prev, next, reset) {
        const seen = {};
        return (reset ? next : prev.concat(next)).filter(function (a) {
            const k = a.url || a.title;
            if (seen[k]) return false;
            seen[k] = true;
            return true;
        }).sort(function (a, b) { return b.time - a.time; });
    }

    function load(reset) {
        if (!live) return;
        if (tab !== "market" && symbols.length === 0) { items = []; more = false; loading = false; return; }
        const id = ++seq;
        if (reset) pageNo = 0;
        loading = true;
        error = "";
        const q = tab === "market" ? { general: true, limit: pageSize, page: pageNo } : { symbols: symbols, limit: pageSize, page: pageNo };
        api.invoke("fmp:news", [q], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            more = r.data.length >= pageSize;
            items = mergeNews(items, r.data, reset);
        });
    }
    // W95_TRADINGLAB_TEST=marketnews-watchlist: open the Watchlist tab (for screenshots)
    Component.onCompleted: if (testScript === "marketnews-watchlist") tab = "watchlist"
    function restart() { items = []; more = true; load(true); }
    onKeyChanged: restart()
    onLiveChanged: restart()
    onAppChanged: if (app && app.activeList) listId = app.activeList.id
    Timer { interval: 5 * 60000; running: view.live; repeat: true; onTriggered: view.load(true) }

    readonly property var shown: {
        const q = filter.trim().toLowerCase();
        return q ? items.filter(function (a) { return (a.title + " " + a.text + " " + a.publisher + " " + a.symbol).toLowerCase().indexOf(q) >= 0; }) : items;
    }
    readonly property string emptyMsg: tab === "watchlist" ? "This watchlist is empty. Add symbols with the star next to the chart title."
                                      : tab === "positions" ? "You have no open positions or working orders, so there is nothing to follow here yet." : "No news returned."
    function selectSymbol(s) { app.setSymbol(s); app.setView("chart"); }

    SvTitleBar {
        id: header
        width: view.width
        title: "Market News"
        tabs: [{ id: "market", text: "Market" }, { id: "watchlist", text: "Watchlist (" + (view.list ? view.list.symbols.length : 0) + ")" },
               { id: "positions", text: "Positions & orders (" + view.heldSymbols.length + ")" }]
        current: view.tab
        onTabClicked: (id) => view.tab = id
        ComboBox95 {
            visible: view.tab === "watchlist" && view.lists.length > 1
            width: 150
            popupMenu: popup
            model: view.lists.map(function (l) { return l.name; })
            currentIndex: view.list ? view.lists.findIndex(function (l) { return l.id === view.list.id; }) : -1
            onActivated: (i) => view.listId = view.lists[i].id
        }
    }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        error: view.error
        onRetry: view.load(true)

        Label95 {
            visible: !view.live
            textFormat: Text.StyledText
            text: "Market news comes from Financial Modeling Prep. <a href=\"settings\">Add your FMP key</a> to see it."
            onLinkActivated: view.app.setView("settings")
        }
        Item {
            visible: view.live
            width: page.contentWidth
            height: 23
            TextField95 {
                id: filterField
                width: 200
                height: 21
                anchors.verticalCenter: parent.verticalCenter
                onTextChanged: view.filter = text
                Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; dim: true; visible: filterField.text === ""; text: "Filter headlines…" }
            }
            Label95 {
                anchors.left: filterField.right
                anchors.leftMargin: 10
                anchors.right: refresh.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                dim: true
                text: (view.tab === "market" ? "Latest market-wide headlines" : "News for " + view.symbols.slice(0, 6).join(", ") + (view.symbols.length > 6 ? " +" + (view.symbols.length - 6) + " more" : "")) + " · refreshes every 5 min"
            }
            Button95 {
                id: refresh
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: 21
                text: view.loading ? "Refreshing…" : "Refresh"
                enabled: !view.loading
                onClicked: view.load(true)
            }
        }
        Label95 { visible: view.live && view.loading && view.items.length === 0; dim: true; text: "Loading…" }
        SvNote {
            visible: view.live && !view.loading && !view.error && view.shown.length === 0
            text: view.filter ? "No headlines match your filter." : view.emptyMsg
        }
        Bevel {
            visible: view.live && view.shown.length > 0
            width: page.contentWidth
            height: newsList.height + 4
            style: "sunken"
            face: Theme.window
            SvNewsList {
                id: newsList
                x: 2
                y: 2
                width: parent.width - 4
                items: view.shown
                showSymbol: view.tab !== "market" || view.shown.some(function (a) { return !!a.symbol; })
                onSymbolClicked: (s) => view.selectSymbol(s)
            }
        }
        Button95 {
            visible: view.live && view.items.length > 0 && view.more && !view.filter
            anchors.horizontalCenter: parent.horizontalCenter
            text: view.loading ? "Loading…" : "Load more"
            enabled: !view.loading
            onClicked: { view.pageNo += 1; view.load(false); }
        }
    }
}
