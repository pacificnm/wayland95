import QtQuick
import Win95Ui

// News: the latest headlines for the chart's symbol, 20 at a time, refreshed every five minutes. Port of upstream
// renderer/src/views/NewsView.tsx (fmp:news). Headlines open in the browser; thumbnails are not shown (Wayland 95
// does not load remote images, see SvNewsList).
Item {
    id: view
    property var app

    readonly property string symbol: app ? app.symbol : ""
    readonly property bool live: app ? app.live : false
    readonly property int pageSize: 20
    property var items: []
    property bool loading: false
    property string error: ""
    property bool more: true
    property int pageNo: 0
    property int seq: 0

    // Merge pages of articles: drop duplicates, newest first (upstream NewsList mergeNews).
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
        if (!live || !symbol) return;
        const id = ++seq;
        if (reset) pageNo = 0;
        loading = true;
        error = "";
        api.invoke("fmp:news", [{ symbol: symbol, limit: pageSize, page: pageNo }], function (r) {
            if (id !== seq) return;
            loading = false;
            if (!r || !r.ok) { error = r ? r.error : "No answer from the data service."; return; }
            more = r.data.length >= pageSize;
            items = mergeNews(items, r.data, reset);
        });
    }
    function restart() { items = []; more = true; load(true); }
    onSymbolChanged: restart()
    onLiveChanged: restart()
    Timer { interval: 5 * 60000; running: view.live; repeat: true; onTriggered: view.load(true) }

    SymbolHeader {
        id: header
        app: view.app
        width: view.width
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
        loading: view.live && view.loading && view.items.length === 0
        error: view.error
        onRetry: view.load(true)

        SvNote {
            visible: !view.live
            text: "News comes from Financial Modeling Prep. Add your FMP key in File → Settings to see headlines for " + view.symbol + "."
        }
        Label95 {
            visible: view.live
            textFormat: Text.StyledText
            text: "Latest headlines for <b>" + view.symbol + "</b> · refreshes every 5 minutes"
            dim: true
        }
        SvNote {
            visible: view.live && !view.loading && !view.error && view.items.length === 0
            text: "No recent news for " + view.symbol + ". Index symbols and some ETFs have no company news."
        }
        Bevel {
            visible: view.items.length > 0
            width: page.contentWidth
            height: newsList.height + 4
            style: "sunken"
            face: Theme.window
            SvNewsList {
                id: newsList
                x: 2
                y: 2
                width: parent.width - 4
                items: view.items
            }
        }
        Button95 {
            visible: view.live && view.items.length > 0 && view.more
            anchors.horizontalCenter: parent.horizontalCenter
            text: view.loading ? "Loading…" : "Load more"
            enabled: !view.loading
            onClicked: { view.pageNo += 1; view.load(false); }
        }
    }
}
