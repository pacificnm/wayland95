import QtQuick
import Win95Ui

// The scrolling strip of market indexes above the chart (port of upstream components/IndexTicker.tsx): S&P 500, Dow,
// Nasdaq, Russell 2000, VIX, FTSE, DAX, Nikkei with price and change, re-read every minute when an FMP key is set
// (placeholder numbers otherwise). Hovering pauses it; clicking an index opens its chart.
//
//   IndexTicker { live: app.live; onSelected: (symbol) => app.setSymbol(symbol) }
Bevel {
    id: ticker
    property bool live: false
    signal selected(string symbol)

    // upstream data/placeholder.ts INDEX_QUOTES
    readonly property var placeholder: [
        { symbol: "^GSPC", name: "S&P 500", price: 5850.2, change: 21.4, pct: 0.37 },
        { symbol: "^DJI", name: "Dow Jones", price: 42310.5, change: -85.3, pct: -0.2 },
        { symbol: "^IXIC", name: "Nasdaq", price: 18420.7, change: 96.8, pct: 0.53 },
        { symbol: "^RUT", name: "Russell 2000", price: 2210.9, change: -6.1, pct: -0.28 },
        { symbol: "^VIX", name: "VIX", price: 15.62, change: -0.34, pct: -2.13 },
        { symbol: "^FTSE", name: "FTSE 100", price: 8290.4, change: 12.7, pct: 0.15 },
        { symbol: "^GDAXI", name: "DAX", price: 19040.1, change: 74.2, pct: 0.39 },
        { symbol: "^N225", name: "Nikkei 225", price: 38450.6, change: -190.5, pct: -0.49 }
    ]
    property var quotes: placeholder

    function fmt(n) { return core.grouped(n, 2); }
    function num(o, ...names) {
        for (const n of names) { const v = o[n]; if (v !== undefined && v !== null && v !== "" && isFinite(Number(v))) return Number(v); }
        return null;
    }
    function load() {
        if (!live) { quotes = placeholder; return; }
        api.invoke("fmp:quotes", [placeholder.map(function (q) { return q.symbol; })], function (r) {
            if (!r || !r.ok || r.data.length === 0) return;
            const names = {};
            placeholder.forEach(function (q) { names[q.symbol] = q.name; });
            quotes = r.data.map(function (q) {
                const symbol = String(q.symbol || "");
                return { symbol: symbol, name: names[symbol] || q.name || symbol, price: num(q, "price") ?? 0, change: num(q, "change") ?? 0,
                         pct: num(q, "changePercentage", "changesPercentage") ?? 0 };
            });
        });
    }
    onLiveChanged: load()
    Component.onCompleted: load()
    Timer { interval: 60000; running: ticker.live; repeat: true; onTriggered: ticker.load() }

    style: "thinSunken"
    face: Theme.window
    height: 22
    clip: true

    // The list is drawn twice and moved left by one copy's width for a seamless loop.
    Row {
        id: track
        y: 1
        height: parent.height - 2
        Repeater {
            model: 2
            delegate: Row {
                height: track.height
                Repeater {
                    model: ticker.quotes
                    delegate: Item {
                        id: tick
                        required property var modelData
                        width: tickRow.implicitWidth + 24
                        height: parent.height
                        Rectangle { anchors.fill: parent; color: tickMouse.containsMouse ? Theme.selection : "transparent" }
                        Row {
                            id: tickRow
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Label95 { bold: true; text: tick.modelData.name; color: tickMouse.containsMouse ? Theme.selectionText : Theme.text }
                            Label95 { text: ticker.fmt(tick.modelData.price); color: tickMouse.containsMouse ? Theme.selectionText : Theme.text }
                            Label95 {
                                color: tickMouse.containsMouse ? Theme.selectionText : Util.tone(tick.modelData.change)
                                text: (tick.modelData.change >= 0 ? "▲ " : "▼ ") + ticker.fmt(Math.abs(tick.modelData.change)) + " (" + (tick.modelData.pct >= 0 ? "+" : "") + tick.modelData.pct.toFixed(2) + "%)"
                            }
                        }
                        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.shadow }
                        MouseArea {
                            id: tickMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ticker.selected(tick.modelData.symbol)
                        }
                    }
                }
            }
        }
    }
    // about 25 px a second (upstream: one loop in 45 s); hovering pauses it
    Timer {
        interval: 40
        repeat: true
        running: ticker.visible && track.width > 0 && !hover.hovered
        onTriggered: track.x = track.x - 1 <= -track.width / 2 ? 0 : track.x - 1
    }
    HoverHandler { id: hover }
}
