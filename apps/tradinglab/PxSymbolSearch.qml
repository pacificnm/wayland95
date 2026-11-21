import QtQuick
import Win95Ui

// A symbol box with suggestions under it (upstream components/SymbolSearch.tsx): with a market-data key it asks FMP's
// search as you type (after a quarter of a second); without one it suggests from a short list of well-known tickers.
// Any typed ticker can be picked ("Go to symbol"); the data feed checks it later. Up/Down move, Enter picks, Escape closes.
// Put it where its drop-down can draw over what follows (give its parent a higher z).
Item {
    id: search
    property bool live: false
    signal picked(string symbol)

    implicitWidth: 220
    implicitHeight: 22

    // data/placeholder.ts KNOWN_SYMBOLS
    readonly property var known: [
        ["AAPL", "Apple Inc."], ["MSFT", "Microsoft Corp."], ["NVDA", "NVIDIA Corp."], ["TSLA", "Tesla Inc."],
        ["AMZN", "Amazon.com Inc."], ["GOOGL", "Alphabet Inc."], ["META", "Meta Platforms"], ["AMD", "Advanced Micro Devices"],
        ["NFLX", "Netflix Inc."], ["JPM", "JPMorgan Chase"], ["V", "Visa Inc."], ["WMT", "Walmart Inc."],
        ["DIS", "Walt Disney Co."], ["BA", "Boeing Co."], ["INTC", "Intel Corp."], ["COST", "Costco Wholesale"],
        ["SPY", "SPDR S&P 500 ETF"], ["QQQ", "Invesco QQQ Trust"], ["DIA", "SPDR Dow Jones ETF"], ["IWM", "iShares Russell 2000 ETF"]
    ]
    property var remote: []
    property int idx: 0
    property bool open: false
    readonly property string q: field.text.trim().toUpperCase()
    readonly property var results: {
        if (!q) return [];
        if (live && remote.length) return remote;
        const hits = known.filter(function (k) { return k[0].indexOf(q) === 0 || k[1].toUpperCase().indexOf(q) >= 0; }).slice(0, 8);
        // any typed ticker is allowed; the data feed will validate it later
        return hits.some(function (h) { return h[0] === q; }) ? hits : hits.concat([[q, "Go to symbol"]]);
    }
    onQChanged: {
        remote = [];
        idx = 0;
        if (q && live) debounce.restart();
    }
    Timer {
        id: debounce
        interval: 250
        onTriggered: {
            const want = search.q;
            api.invoke("fmp:search", [want], function (r) {
                if (want !== search.q || !r.ok) return;
                search.remote = r.data.map(function (h) { return [h.symbol, h.name + (h.exchange ? " · " + h.exchange : "")]; });
            });
        }
    }
    // puts the cursor in the box with `text` typed (for a test, or a screen that wants to start a search)
    function typeText(text) { field.input.forceActiveFocus(); field.text = text; }
    function pick(symbol) { picked(symbol); field.text = ""; open = false; idx = 0; }

    TextField95 {
        id: field
        anchors.fill: parent
        onTextChanged: search.open = true
        onAccepted: if (search.results[search.idx]) search.pick(search.results[search.idx][0])
        onEscaped: { search.open = false; field.input.focus = false; }
        Keys.onDownPressed: search.idx = Math.min(search.idx + 1, search.results.length - 1)
        Keys.onUpPressed: search.idx = Math.max(search.idx - 1, 0)
        Connections {
            target: field.input
            function onActiveFocusChanged() { if (field.input.activeFocus) search.open = true; }
        }
        PxHint { text: "Search symbol…" }
    }

    Rectangle {
        id: drop
        visible: search.open && search.results.length > 0 && field.input.activeFocus
        y: search.height
        z: 1000
        width: Math.max(300, search.width)
        height: list.height + 4
        color: Theme.window
        border.color: Theme.dark
        Column {
            id: list
            x: 2
            y: 2
            width: parent.width - 4
            Repeater {
                model: search.results
                delegate: Rectangle {
                    id: hit
                    required property var modelData
                    required property int index
                    readonly property bool current: index === search.idx
                    width: list.width
                    height: 20
                    color: current ? Theme.selection : "transparent"
                    Text {
                        id: sym
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 70
                        elide: Text.ElideRight
                        text: hit.modelData[0]
                        textFormat: Text.PlainText
                        font.bold: true
                        color: hit.current ? Theme.selectionText : Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Text {
                        x: sym.x + sym.width + 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - 4
                        elide: Text.ElideRight
                        text: hit.modelData[1]
                        textFormat: Text.PlainText
                        color: hit.current ? Theme.selectionText : Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: search.idx = hit.index
                        onClicked: search.pick(hit.modelData[0])
                    }
                }
            }
        }
    }
}
