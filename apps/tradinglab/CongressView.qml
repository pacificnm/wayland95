import QtQuick
import Win95Ui

// Senate & House Trades: the latest congressional trading disclosures, a symbol's trades in Congress, or one member's
// trades, with a summary, the most traded names and a filterable table of filings. Port of upstream
// renderer/src/views/CongressView.tsx (fmp:congress; summaries and filters through core.callPure congress helpers).
Item {
    id: view
    property var app

    readonly property bool live: app ? app.live : false
    readonly property var chNames: ({ senate: "Senate", house: "House" })
    property string mode: "latest"      // latest | symbol | member
    property string chamber: "both"     // both | senate | house
    property int pages: 1
    property string sym: ""
    property string name: ""
    property string pick: ""            // the member chosen when a name matches several
    property var result: null           // {trades, asOf}
    property string error: ""
    property bool loading: false
    property int seq: 0

    readonly property string appSymbol: app ? app.symbol : ""
    onAppSymbolChanged: { sym = appSymbol; symField.text = appSymbol; }

    readonly property bool enabled_: live && (mode === "latest" || (mode === "symbol" && !!sym) || (mode === "member" && name.length >= 2))
    readonly property string key: [enabled_, mode, chamber, pages, sym, name].join("|")
    onKeyChanged: reloadSoon.restart()
    Timer { id: reloadSoon; interval: 0; onTriggered: view.load(false) }

    function load(force) {
        if (!enabled_) { loading = false; return; }
        const id = ++seq;
        loading = true;
        error = "";
        const fail = function (r) { if (id !== seq) return; loading = false; error = r ? r.error : "No answer from the data service."; };
        if (mode === "latest") {
            const got = [];
            let left = pages, failed = false;
            for (let p = 0; p < pages; ++p) {
                api.invoke("fmp:congress", [{ kind: "latest", chamber: chamber, page: p, limit: 100, force: !!force && p === 0 }], (function (page) {
                    return function (r) {
                        if (id !== seq || failed) return;
                        if (!r || !r.ok) { failed = true; fail(r); return; }
                        got[page] = r.data;
                        if (--left > 0) return;
                        loading = false;
                        result = { trades: got.reduce(function (all, d) { return all.concat(d.trades); }, []), asOf: got[0].asOf };
                    };
                })(p));
            }
            return;
        }
        const q = mode === "symbol" ? { kind: "symbol", chamber: chamber, symbol: sym, force: !!force } : { kind: "name", chamber: chamber, name: name, force: !!force };
        api.invoke("fmp:congress", [q], function (r) {
            if (id !== seq) return;
            if (!r || !r.ok) { fail(r); return; }
            loading = false;
            result = r.data;
        });
    }
    onModeChanged: { result = null; error = ""; }
    // W95_TRADINGLAB_TEST=congress-symbol / congress-member: open those tabs (for screenshots)
    Component.onCompleted: {
        if (testScript === "congress-symbol") mode = "symbol";
        else if (testScript === "congress-member") { mode = "member"; name = "Sample"; nameField.text = "Sample"; }
    }
    onNameChanged: pick = ""
    onChamberChanged: { pick = ""; pages = 1; }

    readonly property var members: mode === "member" && result ? core.callPure("groupMembers", [result.trades]) : []
    readonly property string memberKey: pick || (members.length === 1 ? members[0].key : "")
    readonly property var memberTrades: memberKey && result ? result.trades.filter(function (t) { return t.chamber + ":" + (t.memberId || t.member) === memberKey; }) : []
    readonly property var body: mode === "member" && memberKey ? memberTrades : (result ? result.trades : [])
    readonly property var name0: memberTrades.length ? memberTrades[0] : null
    readonly property bool showMember: mode !== "member"
    readonly property bool showResults: !!result && (mode !== "member" || memberKey !== "")

    function ask() {
        app.askClaude(mode === "symbol" ? "Use get_congress_trades to look at how members of Congress have traded " + sym + ". Summarise who traded, buys versus sells, and what a beginner should and should not conclude from it."
                      : mode === "member" && name0 ? "Use get_congress_trades to look at " + name0.member + "'s reported trades and summarise the pattern: what they trade, buys versus sells, filing delays, and how much weight to put on it."
                      : "Use get_congress_trades to summarise the latest Senate and House disclosures: what is being bought and sold, whether several members are trading the same names, and what a beginner should be careful about when reading this data.");
    }
    function selectSymbol(s) { app.setSymbol(s); app.setView("chart"); }
    function money0(n) { return n >= 1e6 ? "$" + (n / 1e6).toFixed(1) + "M" : n >= 1e3 ? "$" + Math.round(n / 1e3) + "K" : "$" + Math.round(n); }
    function isBuy(t) { return /purchase/i.test(t.type); }
    function isSell(t) { return /sale/i.test(t.type); }

    // ------------------------------------------------------------------ results: filters and summary (upstream CongressResults)
    property var f: core.callPure("defaultTradeFilter", [])
    readonly property string resultsKey: mode + (memberKey || sym)
    onResultsKeyChanged: { f = core.callPure("defaultTradeFilter", []); tradeFilter.text = ""; }
    readonly property var shown: core.callPure("filterTrades", [body, f])
    readonly property var s: core.callPure("summarizeCongress", [shown])
    readonly property var owners: {
        const out = [];
        body.forEach(function (t) { if (t.owner && out.indexOf(t.owner) < 0) out.push(t.owner); });
        return out;
    }
    readonly property real net: s.buyValue - s.sellValue
    function setF(patch) { f = Object.assign({}, f, patch); }

    SvTitleBar {
        id: header
        width: view.width
        title: "Senate & House Trades"
        tabs: [{ id: "latest", text: "Latest disclosures" }, { id: "symbol", text: "By symbol" }, { id: "member", text: "By member" }]
        current: view.mode
        onTabClicked: (id) => view.mode = id
        ComboBox95 {
            width: 120
            popupMenu: popup
            readonly property var values: ["both", "senate", "house"]
            model: ["Senate + House", "Senate only", "House only"]
            currentIndex: values.indexOf(view.chamber)
            onActivated: (i) => view.chamber = values[i]
        }
    }

    SvPage {
        id: page
        y: header.height + 2
        width: view.width
        height: view.height - y
        error: view.error
        loading: view.loading && !view.result && view.enabled_
        onRetry: view.load(false)

        Label95 {
            visible: !view.live
            textFormat: Text.StyledText
            text: "Congressional disclosures come from Financial Modeling Prep. <a href=\"settings\">Add your FMP key</a> to see them."
            onLinkActivated: view.app.setView("settings")
        }

        Item {
            visible: view.live
            width: page.contentWidth
            height: 23
            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter
                Label95 { visible: view.mode === "symbol"; anchors.verticalCenter: parent.verticalCenter; text: "Symbol" }
                TextField95 {
                    id: symField
                    visible: view.mode === "symbol"
                    width: 90
                    onTextChanged: if (text !== text.toUpperCase()) text = text.toUpperCase()
                    onAccepted: view.sym = text.trim()
                }
                Button95 { visible: view.mode === "symbol"; height: 21; text: "Look up"; onClicked: view.sym = symField.text.trim() }
                Label95 { visible: view.mode === "member"; anchors.verticalCenter: parent.verticalCenter; text: "Member" }
                TextField95 {
                    id: nameField
                    visible: view.mode === "member"
                    width: 200
                    onAccepted: view.name = text.trim()
                    Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; dim: true; visible: nameField.text === ""; text: "first or last name, e.g. Pelosi" }
                }
                Button95 { visible: view.mode === "member"; height: 21; text: "Search"; onClicked: view.name = nameField.text.trim() }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    dim: true
                    text: view.result && view.result.asOf ? "Data as of " + Util.dateTime(view.result.asOf) + " · " + Util.ago(view.result.asOf) + " · cached to limit FMP requests" : ""
                }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Button95 { height: 21; text: "Ask Claude"; onClicked: view.ask() }
                Button95 {
                    height: 21
                    text: view.loading ? "Refreshing…" : "Refresh"
                    enabled: !view.loading && view.enabled_
                    onClicked: view.load(true)
                }
            }
        }
        Notice {
            visible: view.live
            width: page.contentWidth
            kind: "info"
            text: view.live ? "Members of Congress must report securities trades, but only within 45 days, and only as dollar ranges. It is a fascinating dataset and a poor trading signal: by the time you see a trade, the price has usually moved. Not investment advice." : ""
        }
        SvNote {
            visible: view.live && view.mode === "member" && view.name.length < 2
            text: "Search a member by first or last name to see their reported trades and a profile built from them."
        }
        Flow {
            visible: view.mode === "member" && view.members.length > 1
            width: page.contentWidth
            spacing: 4
            Label95 { height: 22; verticalAlignment: Text.AlignVCenter; dim: true; text: view.members.length + " members match “" + view.name + "”:" }
            Repeater {
                model: view.members
                delegate: ToolButton95 {
                    required property var modelData
                    height: 22
                    checked: modelData.key === view.memberKey
                    text: modelData.member + " (" + view.chNames[modelData.chamber] + ", " + modelData.district + ")  " + modelData.count
                    onClicked: view.pick = modelData.key
                }
            }
        }

        Grid {
            id: grid
            visible: view.showResults
            width: page.contentWidth
            columns: 2
            spacing: 8
            readonly property real cardWidth: (width - spacing) / 2

            SvCard {
                width: grid.cardWidth
                height: Math.max(implicitHeight, topCard.implicitHeight)
                title: Util.plain(view.mode === "member" && view.name0 ? view.name0.member + " · " + view.chNames[view.name0.chamber] + ", " + view.name0.district : view.mode === "symbol" ? view.sym + " in Congress" : "Latest filings")
                sub: view.s.from && view.s.to ? "trades " + view.s.from + " to " + view.s.to : ""
                Row {
                    width: parent.width
                    spacing: 4
                    Repeater {
                        model: [
                            { label: "Trades", value: String(view.s.count), color: Theme.text, note: "" },
                            { label: "Purchases", value: String(view.s.buys), color: "#007000", note: "≈ " + view.money0(view.s.buyValue) },
                            { label: "Sales", value: String(view.s.sells), color: "#b00000", note: "≈ " + view.money0(view.s.sellValue) },
                            { label: "Net (est.)", value: (view.net >= 0 ? "+" : "-") + view.money0(Math.abs(view.net)), color: view.net >= 0 ? "#007000" : "#b00000", note: view.net >= 0 ? "net buying" : "net selling" },
                            { label: "Median filing delay", value: view.s.medianDelay !== null ? view.s.medianDelay + "d" : "—", color: Theme.text, note: view.s.maxDelay !== null ? "up to " + view.s.maxDelay + "d" : "" }
                        ]
                        delegate: Bevel {
                            required property var modelData
                            style: "thinSunken"
                            face: Theme.window
                            width: (parent.width - 16) / 5
                            height: 52
                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 6
                                Label95 { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; small: true; dim: true; text: modelData.label }
                                Label95 { width: parent.width; horizontalAlignment: Text.AlignHCenter; bold: true; font.pixelSize: 14; color: modelData.color; text: modelData.value }
                                Label95 { width: parent.width; horizontalAlignment: Text.AlignHCenter; small: true; dim: true; text: modelData.note }
                            }
                        }
                    }
                }
                SvNote { text: "Amounts are filed as ranges, so dollar figures use each range's midpoint. Trades are public only after filing (up to 45 days later), so this is history, not a live signal." }
            }

            SvCard {
                id: topCard
                width: grid.cardWidth
                title: "Most traded"
                sub: "in the current view"
                Flow {
                    width: parent.width
                    spacing: 3
                    Label95 { height: 20; verticalAlignment: Text.AlignVCenter; dim: true; text: "Symbols  " }
                    Label95 { visible: view.s.topSymbols.length === 0; height: 20; verticalAlignment: Text.AlignVCenter; dim: true; text: "none" }
                    Repeater {
                        model: view.s.topSymbols
                        delegate: ToolButton95 {
                            required property var modelData
                            height: 20
                            text: modelData.symbol + "  " + modelData.count + (modelData.members > 1 ? " · " + modelData.members + " members" : "")
                            onClicked: view.selectSymbol(modelData.symbol)
                            ToolTipArea {
                                text: parent.modelData.count + " trades by " + parent.modelData.members + " member" + (parent.modelData.members === 1 ? "" : "s") + ": " + parent.modelData.buys + " purchases, " + parent.modelData.sells + " sales"
                                      + (parent.modelData.members === 1 ? ". Many filings from one member is usually a single position being built or sold." : "")
                            }
                        }
                    }
                }
                Flow {
                    visible: view.showMember
                    width: parent.width
                    spacing: 3
                    Label95 { height: 18; verticalAlignment: Text.AlignVCenter; dim: true; text: "Members  " }
                    Repeater {
                        model: view.s.topMembers
                        delegate: Bevel {
                            required property var modelData
                            style: "thinSunken"
                            face: Theme.window
                            width: memberText.implicitWidth + 10
                            height: 18
                            Label95 { id: memberText; anchors.centerIn: parent; text: parent.modelData.member + "  " + parent.modelData.count }
                        }
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 3
                    Label95 { height: 18; verticalAlignment: Text.AlignVCenter; dim: true; text: "Assets  " }
                    Repeater {
                        model: view.s.assetTypes.slice(0, 6)
                        delegate: Bevel {
                            required property var modelData
                            style: "thinSunken"
                            face: Theme.window
                            width: assetText.implicitWidth + 10
                            height: 18
                            Label95 { id: assetText; anchors.centerIn: parent; text: parent.modelData.type + "  " + parent.modelData.count }
                        }
                    }
                }
            }
        }

        SvCard {
            visible: view.showResults
            width: page.contentWidth
            title: "Disclosures"
            sub: view.shown.length + " of " + view.body.length
            Row {
                spacing: 6
                ComboBox95 {
                    width: 130
                    popupMenu: popup
                    readonly property var values: ["all", "buys", "sells"]
                    model: ["Buys and sales", "Purchases only", "Sales only"]
                    currentIndex: values.indexOf(view.f.kind)
                    onActivated: (i) => view.setF({ kind: values[i] })
                }
                ComboBox95 {
                    width: 100
                    popupMenu: popup
                    readonly property var values: ["all", "stocks"]
                    model: ["All assets", "Stocks only"]
                    currentIndex: values.indexOf(view.f.assetType)
                    onActivated: (i) => view.setF({ assetType: values[i] })
                }
                ComboBox95 {
                    visible: view.owners.length > 0
                    width: 110
                    popupMenu: popup
                    model: ["Any owner"].concat(view.owners)
                    currentIndex: view.f.owner === "all" ? 0 : view.owners.indexOf(view.f.owner) + 1
                    onActivated: (i) => view.setF({ owner: i === 0 ? "all" : view.owners[i - 1] })
                }
                TextField95 {
                    id: tradeFilter
                    width: 160
                    onTextChanged: view.setF({ q: text })
                    Label95 { x: 5; anchors.verticalCenter: parent.verticalCenter; dim: true; visible: tradeFilter.text === ""; text: "Filter…" }
                }
            }
            SvTable {
                width: parent.width
                model: view.shown
                emptyText: "No trades match."
                columns: {
                    const c = [
                        { title: "Disclosed", w: 72, text: function (t) { return t.disclosed; } },
                        { title: "Traded", w: 72, text: function (t) { return t.traded; },
                          sub: function (t) { const d = core.callPure("delayDays", [t]); return d === null || d === undefined ? "" : d > 45 ? d + "d late" : d + "d"; },
                          tip: "How many days after the trade it was disclosed; past 45 days is late" }
                    ];
                    if (view.showMember)
                        c.push({ title: "Member", flex: true, min: 120, bold: true, text: function (t) { return t.member; }, sub: function (t) { return view.chNames[t.chamber] + " · " + t.district; } });
                    c.push({ title: "Symbol", w: 58, link: function (t) { return !!t.symbol; }, bold: true, text: function (t) { return t.symbol || "—"; } },
                           { title: "Asset", flex: true, min: 120, text: function (t) { return t.asset; }, sub: function (t) { return t.assetType; } },
                           { title: "Type", w: 78, text: function (t) { return t.type; }, color: function (t) { return view.isBuy(t) ? "#007000" : view.isSell(t) ? "#b00000" : Theme.text; } },
                           { title: "Amount", w: 112, right: true, text: function (t) { return t.amount; } },
                           { title: "Owner", w: 50, text: function (t) { return t.owner || "—"; }, color: function () { return Theme.shadow; } },
                           { title: "", w: 42, link: function (t) { return !!t.link; }, text: function (t) { return t.link ? "Filing" : ""; } });
                    return c;
                }
                onCellClicked: (row, t, col) => {
                    const title = columns[col].title;
                    if (title === "Symbol" && t.symbol) view.selectSymbol(t.symbol);
                    else if (title === "" && t.link) api.invoke("app:openExternal", [t.link]);
                }
            }
        }
        Button95 {
            visible: !!view.result && view.mode === "latest"
            anchors.horizontalCenter: parent.horizontalCenter
            text: view.loading ? "Loading…" : "Load older disclosures"
            enabled: !view.loading
            onClicked: view.pages += 1
        }
    }
}
