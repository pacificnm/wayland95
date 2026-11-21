import QtQuick
import Win95Ui

// The Watchlists screen: one tab per list (double-click to rename, + for a new one), live quotes for the symbols on
// the chosen list (refreshed every 30 seconds while a market-data key is set), adding and removing symbols. Clicking a
// row opens that symbol's chart. Port of renderer/src/views/WatchlistView.tsx.
Item {
    id: view
    property var app

    readonly property var lists: app ? app.lists : []
    readonly property var active: app ? app.activeList : null
    readonly property var symbols: active ? active.symbols : []
    readonly property string symKey: symbols.join(",")
    property var quotes: ({})

    function pct(n) { return (n >= 0 ? "+" : "") + n.toFixed(2) + "%"; }
    function load() {
        if (!app || !app.live || symbols.length === 0) return;
        api.invoke("fmp:quotes", [symbols], function (r) {
            if (!r.ok) return;
            const q = {};
            r.data.forEach(function (x) {
                q[core.pickStr(x, ["symbol"])] = { price: core.pick(x, ["price"]), change: core.pick(x, ["change"]), pct: core.pick(x, ["changePercentage", "changesPercentage"]) };
            });
            view.quotes = q;
        });
    }
    onSymKeyChanged: load()
    readonly property bool live: app ? app.live : false
    onLiveChanged: load()
    Timer { interval: 30000; repeat: true; running: view.live && view.symbols.length > 0; onTriggered: view.load() }

    function add() {
        const t = addField.text.trim();
        if (active && t) { app.addSymbolTo(active.id, t); addField.text = ""; }
    }

    PxTitle { id: title; width: parent.width; title: "Watchlists" }

    // the lists, as tabs
    Item {
        id: tabRow
        y: title.height + 4
        width: parent.width
        height: 24
        Row {
            spacing: 2
            Repeater {
                model: view.lists
                delegate: PxButton {
                    required property var modelData
                    height: 22
                    text: modelData.name + "  " + modelData.symbols.length
                    checked: view.active !== null && modelData.id === view.active.id
                    onClicked: view.app.chooseList(modelData.id)
                    onDoubleClicked: view.app.renameList(modelData)
                    ToolTipArea { text: "Double-click to rename" }
                }
            }
            PxButton {
                width: 24
                height: 22
                icon: "add"
                onClicked: view.app.newListDialog()
                ToolTipArea { text: "New watchlist" }
            }
        }
        Row {
            visible: view.active !== null
            anchors.right: parent.right
            spacing: 2
            PxButton {
                width: 24
                height: 22
                flat: true
                icon: "edit"
                onClicked: view.app.renameList(view.active)
                ToolTipArea { text: "Rename this list" }
            }
            PxButton {
                width: 24
                height: 22
                flat: true
                icon: "delete"
                enabled: view.lists.length > 1
                onClicked: view.app.deleteList(view.active)
                ToolTipArea { text: "Delete this list" }
            }
        }
    }

    // add a symbol
    Row {
        id: addRow
        y: tabRow.y + tabRow.height + 4
        width: parent.width
        spacing: 4
        TextField95 {
            id: addField
            width: parent.width - addButton.width - 4
            height: 22
            onTextChanged: if (text !== text.toUpperCase()) text = text.toUpperCase()
            onAccepted: view.add()
            PxHint { text: "Add a symbol to " + (view.active ? view.active.name : "this list") + " (e.g. AMZN) and press Enter" }
        }
        PxButton { id: addButton; width: 60; height: 22; text: "Add"; enabled: addField.text.trim() !== ""; onClicked: view.add() }
    }

    PxTable {
        y: addRow.y + addRow.height + 4
        width: parent.width
        height: parent.height - y
        rows: view.symbols
        emptyText: "This list is empty. Add a symbol above, or use the star next to the chart title."
        selected: function (s) { return s === view.app.symbol; }
        onRowClicked: (i, s) => { view.app.setSymbol(s); view.app.setView("chart"); }
        columns: [
            { title: "Symbol", flex: true, text: function (s) { return s; }, bold: function () { return true; } },
            { title: "Last", w: 110, right: true, text: function (s) { const r = view.quotes[s]; return r && r.price !== null && r.price !== undefined ? core.money(r.price) : "—"; } },
            { title: "Change", w: 90, right: true,
              text: function (s) { const r = view.quotes[s]; return r && r.change !== null && r.change !== undefined ? r.change.toFixed(2) : "—"; },
              color: function (s) { const r = view.quotes[s]; return r && r.change !== null && r.change !== undefined ? Util.tone(r.change >= 0 ? 1 : -1) : Theme.text; } },
            { title: "%", w: 80, right: true,
              text: function (s) { const r = view.quotes[s]; return r && r.pct !== null && r.pct !== undefined ? view.pct(r.pct) : "—"; },
              color: function (s) { const r = view.quotes[s]; return r && r.change !== null && r.change !== undefined ? Util.tone(r.change >= 0 ? 1 : -1) : Theme.text; } },
            { title: "", w: 30, button: "×", flat: true, tip: function (s) { return "Remove " + s + " from " + view.active.name; },
              onClick: function (s) { view.app.removeSymbolFrom(view.active.id, s); } }
        ]
    }
}
