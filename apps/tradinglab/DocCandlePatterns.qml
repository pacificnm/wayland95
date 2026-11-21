import QtQuick
import Win95Ui

// The candlestick pattern gallery inside the "candlestick-patterns" lesson: filter by bias, search, thumbnails per
// group, a detail page per pattern, and "Measure" (counts every pattern in the last 5 years of daily bars of the
// symbol). Teaching text from content/patterns.json; detection is C++ (core.countPatterns).
// Port of components/CandlePatterns.tsx.
Column {
    id: gallery
    property var app
    readonly property string symbol: app ? app.symbol : ""
    signal askClaude(string text)
    signal wantTop()     // a pattern was opened: the page scrolls up to the gallery
    spacing: 8

    property var pdata: ({ patterns: [], rarityNote: {}, groups: [] })
    property string bias: "all"
    property string sel: ""
    property var measured: null          // {symbol, bars, counts}
    property bool busy: false
    property string q: ""

    Component.onCompleted: api.invoke("content:get", ["patterns.json"], function (d) { if (d) gallery.pdata = d; })

    readonly property var biases: ["all", "bullish", "bearish", "neutral"]
    readonly property var rarityTone: ({ common: "green", uncommon: "blue", rare: "amber", "very rare": "grey" })
    function biasTone(b) { return b === "bullish" ? "green" : b === "bearish" ? "red" : "grey"; }
    function seen(p) { return measured ? (measured.counts[p.id] || 0) : null; }
    function oneIn(n) { return n === 0 ? "none in this window" : "about 1 in " + Math.round(measured.bars / n) + " bars"; }
    readonly property string term: q.trim().toLowerCase()
    function matches(p) {
        return (bias === "all" || p.bias === bias) && (!term || (p.name + " " + p.summary + " " + p.context).toLowerCase().indexOf(term) >= 0);
    }
    readonly property var cur: {
        const ps = pdata.patterns || [];
        for (let i = 0; i < ps.length; ++i) if (ps[i].id === sel) return ps[i];
        return null;
    }

    function measure() {
        busy = true;
        const want = symbol;
        const from = new Date(Date.now() - 1830 * 86400000).toISOString().slice(0, 10);
        api.invoke("fmp:bars", [{ symbol: want, interval: "1day", from: from }], function (r) {
            busy = false;
            if (!r || !r.ok) { app.toast("error", "Could not load " + want + " prices: " + (r ? r.error : "no answer")); return; }
            if (!r.data.length) { app.toast("error", "Could not load " + want + " prices: No price data returned for " + want + "."); return; }
            const c = r.data.slice(-core.barCount("5Y", "1day"));
            measured = { symbol: want, bars: c.length, counts: core.countPatterns(c) };
        });
    }

    // ---------------------------------------------------------------- the gallery
    Row {
        visible: !gallery.cur
        width: parent.width
        spacing: 4
        Repeater {
            model: gallery.biases
            DocChip {
                required property string modelData
                anchors.verticalCenter: parent.verticalCenter
                text: modelData === "all" ? "All patterns" : modelData
                checked: gallery.bias === modelData
                onClicked: gallery.bias = modelData
            }
        }
        DocField {
            width: 170
            anchors.verticalCenter: parent.verticalCenter
            placeholder: "Search " + (gallery.pdata.patterns || []).length + " patterns…"
            onTextChanged: gallery.q = text
        }
    }
    Item {
        visible: !gallery.cur
        width: parent.width
        height: 24
        Button95 {
            id: measureButton
            enabled: !gallery.busy
            text: gallery.busy ? "Measuring…" : gallery.measured && gallery.measured.symbol === gallery.symbol ? "Re-measure " + gallery.symbol : "Measure how often on " + gallery.symbol
            onClicked: gallery.measure()
            ToolTipArea { text: "Counts every pattern in the last 5 years of daily bars for this symbol" }
        }
    }
    Label95 {
        visible: !gallery.cur && gallery.term !== "" && !(gallery.pdata.patterns || []).some(gallery.matches)
        text: "No pattern matches “" + gallery.q + "”."
        dim: true
    }
    Repeater {
        model: gallery.cur ? [] : (gallery.pdata.groups || [])
        Column {
            id: group
            required property string modelData
            readonly property var items: (gallery.pdata.patterns || []).filter(function (p) { return p.group === group.modelData && gallery.matches(p); })
            visible: items.length > 0
            width: gallery.width
            spacing: 6
            Heading95 { level: 3; text: group.modelData }
            Flow {
                width: parent.width
                spacing: 6
                Repeater {
                    model: group.items
                    Item {
                        id: card
                        required property var modelData
                        width: 136
                        height: cardCol.implicitHeight + 10
                        Bevel {
                            anchors.fill: parent
                            style: cardMouse.pressed ? "thinSunken" : "raised"
                            face: cardMouse.containsMouse ? Theme.light : Theme.face
                        }
                        Column {
                            id: cardCol
                            x: 6
                            y: 5
                            width: parent.width - 12
                            spacing: 3
                            Rectangle {
                                width: 120
                                height: 80
                                color: Theme.window
                                border.color: Theme.shadow
                                DocPatternChart { anchors.fill: parent; anchors.margins: 1; bars: card.modelData.example; size: card.modelData.size }
                            }
                            Label95 { width: parent.width; text: card.modelData.name; bold: true; elide: Text.ElideRight }
                            Row {
                                spacing: 3
                                DocBadge { text: card.modelData.bias; tone: gallery.biasTone(card.modelData.bias) }
                                DocBadge { text: card.modelData.rarity; tone: gallery.rarityTone[card.modelData.rarity] || "grey" }
                            }
                            Label95 {
                                visible: !!gallery.measured
                                text: gallery.measured ? gallery.seen(card.modelData) + "× on " + gallery.measured.symbol : ""
                                dim: true
                                small: true
                            }
                        }
                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { gallery.sel = card.modelData.id; gallery.wantTop(); }
                        }
                        ToolTipArea { text: card.modelData.summary }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- one pattern
    Button95 {
        visible: !!gallery.cur
        text: "← All patterns"
        onClicked: gallery.sel = ""
    }
    Flow {
        visible: !!gallery.cur
        width: parent.width
        spacing: 12
        Bevel {
            id: bigWell
            width: 364
            height: 224
            style: "sunken"
            face: Theme.window
            DocPatternChart { x: 2; y: 2; width: 360; height: 220; big: true; bars: gallery.cur ? gallery.cur.example : []; size: gallery.cur ? gallery.cur.size : 1 }
        }
        Column {
            width: parent.width - bigWell.width - 12 >= 260 ? parent.width - bigWell.width - 12 : parent.width
            spacing: 6
            Heading95 { level: 2; width: parent.width; text: gallery.cur ? gallery.cur.name : "" }
            Flow {
                width: parent.width
                spacing: 4
                DocBadge { text: gallery.cur ? gallery.cur.bias : ""; tone: gallery.cur ? gallery.biasTone(gallery.cur.bias) : "grey" }
                DocBadge {
                    text: gallery.cur ? gallery.cur.rarity : ""
                    tone: gallery.cur ? gallery.rarityTone[gallery.cur.rarity] || "grey" : "grey"
                    ToolTipArea { text: gallery.cur ? gallery.pdata.rarityNote[gallery.cur.rarity] || "" : "" }
                }
                Label95 { text: gallery.cur ? gallery.cur.group + " · " + gallery.cur.context : ""; dim: true }
            }
            Label95 { width: parent.width; wrapMode: Text.Wrap; text: gallery.cur ? gallery.cur.summary : "" }
            Flow {
                width: parent.width
                spacing: 4
                Button95 {
                    isDefault: true
                    text: "Claude, show me on " + gallery.symbol
                    onClicked: gallery.askClaude("Show me real examples of the \"" + gallery.cur.name + "\" candlestick pattern (id " + gallery.cur.id + ") on the " + gallery.symbol + " chart. Use find_candle_pattern to find them (it marks them on the chart), then use get_candles around the best one to explain what each candle did and what price did afterwards. If the pattern is not present in the loaded bars, say so and suggest a longer range or another symbol instead of forcing an example.")
                }
                Button95 {
                    text: "Teach me this"
                    onClicked: gallery.askClaude("Teach me the \"" + gallery.cur.name + "\" candlestick pattern (id " + gallery.cur.id + "). Start from what it means about buyers and sellers, show one real example from the " + gallery.symbol + " chart with find_candle_pattern, then ask me one question to check I understood. Be honest about how often it fails.")
                }
            }
        }
    }
    Column {
        visible: !!gallery.cur
        width: parent.width
        spacing: 6
        Heading95 { level: 3; text: "How often you will see it" }
        Text {
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: (gallery.cur ? Util.plain(gallery.pdata.rarityNote[gallery.cur.rarity] || "") : "")
                  + " <font color=\"#808080\">(a rough guide, mostly worked out from simulated prices, and it varies by stock and time frame. Use <b>Measure</b> on the gallery for real counts.)</font>"
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize + 1
        }
        Text {
            visible: !!gallery.measured && !!gallery.cur
            width: parent.width
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: {
                if (!gallery.measured || !gallery.cur) return "";
                const n = gallery.seen(gallery.cur);
                return "On " + gallery.measured.symbol + ", 5 years of daily bars: <b>" + n + "</b> time" + (n === 1 ? "" : "s") + " in " + gallery.measured.bars + " bars (" + gallery.oneIn(n) + ").";
            }
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize + 1
        }
        Heading95 { level: 3; text: "How to spot it" }
        Repeater {
            model: gallery.cur ? gallery.cur.rules : []
            Label95 {
                required property string modelData
                width: parent.width
                leftPadding: 12
                wrapMode: Text.Wrap
                text: "•  " + modelData
                font.pixelSize: Theme.fontSize + 1
            }
        }
        Heading95 { level: 3; text: "What it says" }
        Label95 { width: parent.width; wrapMode: Text.Wrap; text: gallery.cur ? gallery.cur.story : ""; font.pixelSize: Theme.fontSize + 1 }
        Heading95 { level: 3; text: "Confirmation" }
        Label95 { width: parent.width; wrapMode: Text.Wrap; text: gallery.cur ? gallery.cur.confirm : ""; font.pixelSize: Theme.fontSize + 1 }
        Heading95 { level: 3; text: "Watch out" }
        Label95 { width: parent.width; wrapMode: Text.Wrap; text: gallery.cur ? gallery.cur.caution : ""; font.pixelSize: Theme.fontSize + 1 }
    }
}
