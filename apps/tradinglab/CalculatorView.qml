import QtQuick
import Win95Ui

// The Position calculator: how many shares to buy for a trade so that being stopped out costs no more than the risk
// budget, capped by the user's limits (max per position, max total invested, buying power). The stop can be a price, a
// percentage or a multiple of the 14-day ATR from daily bars. The result can fill an order ticket (which the user still
// has to send), be saved to the journal as an idea, or be handed to Claude to check. Inputs and limits are saved as they
// change (app.calc / app.rules). Port of renderer/src/views/CalculatorView.tsx (and chart/calcState.ts's fetchLast/fetchAtr).
Item {
    id: view
    property var app

    readonly property var calc: app ? app.calc : core.defaultCalc()
    readonly property var rules: app ? app.rules : core.callPure("defaultRules", [])
    readonly property bool live: app ? app.live : false
    readonly property var snap: app ? app.snap : null
    readonly property string symbol: String(calc.symbol || (app ? app.symbol : "")).toUpperCase()

    property var last: null
    property var atr: null
    readonly property var bindText: ({ risk: "your risk budget", position: "your max position size", total: "your max total invested", buyingPower: "your buying power" })

    function d(n, digits) { return n === null || n === undefined || !isFinite(n) ? "—" : Number(n).toFixed(digits === undefined ? 2 : digits); }
    function num(s, fallback) { const v = parseFloat(s); return isFinite(v) ? v : fallback; }
    function set(p) { app.updateCalc(p); }
    function setRule(p) { app.updateRules(Object.assign({}, rules, p)); }
    function usedPct(v, cap) { return Math.max(0, Math.min(100, cap > 0 ? (v / cap) * 100 : 0)); }
    function grouped(n) { return core.grouped(n, 0); }

    // ---------------------------------------------------------------- prices (calcState.ts fetchLast / fetchAtr)
    function sampleBars(range) { return core.callPure("sampleCandles", [symbol, range, "1day"]) || []; }
    function loadLast() {
        const want = symbol;
        if (!live) {
            const bars = sampleBars("1M");
            last = bars.length ? bars[bars.length - 1].close : null;
            return;
        }
        api.invoke("fmp:quotes", [[want]], function (r) {
            if (want !== view.symbol) return;
            view.last = r.ok && r.data[0] ? Number(r.data[0].price) || null : null;
        });
    }
    // 14-day average true range from daily bars, so stops can be sized in ATRs (optional: null when it cannot be had)
    function loadAtr() {
        const want = symbol;
        atr = null;
        if (!live) { atr = core.latestAtr(sampleBars("3M"), 14); return; }
        api.invoke("fmp:bars", [{ symbol: want, interval: "1day", from: core.callPure("loadFromDay", ["3M", "1day"]) }], function (r) {
            if (want !== view.symbol || !r.ok || !r.data || r.data.length === 0) return;
            view.atr = core.latestAtr(core.callPure("lastBars", [r.data, "3M", "1day"]), 14);
        });
    }
    function reload() { last = null; loadLast(); loadAtr(); }
    readonly property string priceKey: symbol + "|" + live
    onPriceKeyChanged: reload()
    Timer { interval: 15000; repeat: true; running: true; onTriggered: view.loadLast() }

    // ---------------------------------------------------------------- the calculation
    readonly property real equity: snap ? snap.account.equity : 0
    readonly property real invested: (snap ? snap.positions : []).reduce(function (s, p) { return s + Math.abs(p.qty * (p.mark ?? p.avg)); }, 0)
    readonly property var input: core.calcToInput(calc, { last: last, atr: atr, equity: equity, buyingPower: snap ? snap.account.buyingPower : null, investedNow: invested })
    readonly property var r: core.callPure("calculate", [input, rules])
    readonly property bool ready: r.ok && r.shares > 0

    function summary() {
        return symbol + " " + calc.side + ": entry " + d(r.entry) + ", stop " + d(r.stop) + ", target " + d(r.target) + ", " + r.shares + " shares ($" + d(r.positionValue, 0)
            + " = " + d(r.pctOfAccount, 1) + "% of the account), risk $" + d(r.risk, 0) + " (" + d(r.riskPct) + "%), reward $" + d(r.reward, 0) + ", reward:risk " + d(r.rr) + ".";
    }
    function saveIdea() {
        api.invoke("journal:create", [{
            kind: "trade", status: "idea", source: "user", symbol: symbol, direction: calc.side, title: symbol + " " + calc.side + " plan",
            plan_entry: r.entry, plan_stop: r.stop, plan_target: r.target, plan_qty: r.shares,
            body: "Sized with the calculator. " + summary() + "\n\nRisk budget $" + d(r.riskBudget, 0) + (r.binding && r.binding !== "risk" ? "; limited by " + bindText[r.binding] : "") + "."
        }], function (e) {
            app.toast("success", "Saved to your trading journal as an idea", { action: { label: "Open journal", onClick: function () { view.app.openJournal(e.id); } } });
        });
    }
    function askClaude() {
        app.askClaude("Please sanity-check my position sizing. " + summary() + " Rules: max " + rules.maxRiskPct + "% risk per trade, max " + rules.maxPositionPct
                      + "% of the account per position, max total invested " + (rules.maxTotalMode === "percent" ? rules.maxTotalPct + "% of the account" : "$" + rules.maxTotalDollars)
                      + ". Use get_risk_rules, get_account and the chart tools to check my stop against the real chart (support, ATR) and tell me whether the size, stop and target make sense, and what you would change.");
    }
    function openTicket() {
        app.ticketFromCalc({ symbol: symbol, side: calc.side, shares: r.shares, entry: r.entry, stop: r.stop, target: r.target, note: "From the position calculator. " + summary() });
    }

    PxTitle {
        id: title
        width: parent.width
        title: "Position calculator"
        PxButton { icon: "help"; text: "Ask Claude to check"; onClicked: view.askClaude() }
    }

    PxPage {
        id: page
        y: title.height
        width: parent.width
        height: parent.height - y

        Notice {
            width: page.contentWidth
            kind: "warning"
            text: view.live ? "" : "Sample prices — add your FMP key in File → Settings for live valuation."
        }

        Grid {
            id: grid
            width: page.contentWidth
            columns: width >= 700 ? 2 : 1
            spacing: 8
            readonly property real cardWidth: (width - (columns - 1) * spacing) / columns

            // ------------------------------------------------ Setup
            CxCard {
                title: "Setup"
                note: view.last !== null ? "live price " + core.money(view.last) : "loading price…"
                CxRow {
                    label: "Symbol"
                    TextField95 {
                        id: symField
                        width: 100
                        height: 21
                        text: view.symbol
                        onTextChanged: if (text !== text.toUpperCase()) text = text.toUpperCase()
                        onAccepted: view.set({ symbol: text.trim() })
                        Connections { target: symField.input; function onActiveFocusChanged() { if (!symField.input.activeFocus && symField.text.trim() !== view.symbol) view.set({ symbol: symField.text.trim() }); } }
                    }
                    PxButton { width: 24; height: 21; flat: true; icon: "refresh"; onClicked: view.loadLast(); ToolTipArea { text: "Refresh price" } }
                }
                CxRow {
                    label: "Direction"
                    PxSeg { options: [{ id: "long", text: "Long" }, { id: "short", text: "Short" }]; current: view.calc.side; onPicked: (id) => view.set({ side: id }) }
                }
                CxRow {
                    label: "Entry price"
                    hint: view.calc.followLive ? "(live)" : ""
                    TextField95 {
                        width: 80
                        height: 21
                        enabled: !view.calc.followLive
                        input.color: enabled ? Theme.text : Theme.shadow
                        text: view.calc.followLive ? (view.last !== null ? String(Math.round(view.last * 100) / 100) : "") : view.calc.entry
                        onTextChanged: if (!view.calc.followLive && text !== view.calc.entry) view.set({ entry: text })
                    }
                    CheckBox95 {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "follow live price"
                        checked: view.calc.followLive
                        onToggled: (c) => view.set({ followLive: c, entry: c ? view.calc.entry : view.last !== null ? String(Math.round(view.last * 100) / 100) : view.calc.entry })
                    }
                }
                CxRow {
                    label: "Account"
                    ComboBox95 {
                        width: 180
                        popupMenu: menu
                        model: ["Paper account (" + core.money(view.equity) + ")", "Custom amount"]
                        currentIndex: view.calc.accountBasis === "custom" ? 1 : 0
                        onActivated: (i) => view.set({ accountBasis: i === 1 ? "custom" : "paper" })
                    }
                    CxField { visible: view.calc.accountBasis === "custom"; key: "customAccount"; width: 60 }
                }
                CxRow {
                    label: "Risk per trade"
                    ComboBox95 {
                        width: 100
                        popupMenu: menu
                        model: ["% of account", "Dollars"]
                        currentIndex: view.calc.riskMode === "dollars" ? 1 : 0
                        onActivated: (i) => view.set({ riskMode: i === 1 ? "dollars" : "percent" })
                    }
                    CxField { visible: view.calc.riskMode === "percent"; key: "riskPct" }
                    CxField { visible: view.calc.riskMode === "dollars"; key: "riskDollars" }
                    CxDim { text: "= " + core.money(view.r.riskBudgetRequested) }
                }
                CxRow { label: "Win rate"; hint: "for expected value"; CxField { key: "winRate" } CxDim { text: "%" } }
                CxRow { label: "Commission"; hint: "round trip"; CxField { key: "commission" } CxDim { text: "$" } }
            }

            // ------------------------------------------------ Stop & target
            CxCard {
                title: "Stop & target"
                note: view.atr !== null ? "ATR(14) = " + core.money(view.atr) : "ATR unavailable"
                CxRow {
                    label: "Stop loss"
                    ComboBox95 {
                        width: 110
                        popupMenu: menu
                        readonly property var ids: ["atr", "percent", "price"]
                        model: ["ATR multiple", "% from entry", "Price"]
                        currentIndex: Math.max(0, ids.indexOf(view.calc.stopMode))
                        onActivated: (i) => view.set({ stopMode: ids[i] })
                    }
                    CxField { visible: view.calc.stopMode === "atr"; key: "stopAtr" }
                    CxDim { visible: view.calc.stopMode === "atr"; text: "× ATR" }
                    CxField { visible: view.calc.stopMode === "percent"; key: "stopPct" }
                    CxDim { visible: view.calc.stopMode === "percent"; text: "%" }
                    CxField { visible: view.calc.stopMode === "price"; key: "stopPrice" }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                    text: "Stop at <b><font color='#b00000'>" + view.d(view.r.stop) + "</font></b>"
                          + (view.r.stopPct !== null ? " · " + view.d(view.r.stopPct) + "% away" : "")
                          + (view.r.stopAtr !== null ? " · " + view.d(view.r.stopAtr, 1) + " ATR" : "")
                          + (view.r.riskPerShare !== null ? " · " + core.money(view.r.riskPerShare) + " risk per share" : "")
                }
                CxRow {
                    label: "Profit target"
                    ComboBox95 {
                        width: 140
                        popupMenu: menu
                        readonly property var ids: ["r", "percent", "price", "none"]
                        model: ["Multiple of risk (R)", "% from entry", "Price", "No target"]
                        currentIndex: Math.max(0, ids.indexOf(view.calc.targetMode))
                        onActivated: (i) => view.set({ targetMode: ids[i] })
                    }
                    CxField { visible: view.calc.targetMode === "r"; key: "targetR" }
                    CxDim { visible: view.calc.targetMode === "r"; text: "R" }
                    CxField { visible: view.calc.targetMode === "percent"; key: "targetPct" }
                    CxDim { visible: view.calc.targetMode === "percent"; text: "%" }
                    CxField { visible: view.calc.targetMode === "price"; key: "targetPrice" }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    color: Theme.text
                    text: view.r.target !== null
                          ? "Target at <b><font color='#007000'>" + view.d(view.r.target) + "</font></b>"
                            + (view.r.entry > 0 ? " · " + view.d(Math.abs(view.r.target - view.r.entry) / view.r.entry * 100) + "% away" : "")
                          : "No target set"
                }
                Label95 {
                    width: parent.width
                    wrapMode: Text.Wrap
                    dim: true
                    text: view.calc.followLive ? "Entry follows the live price, so percent, ATR and R-based levels and the share count update as the price moves."
                                               : "Entry is fixed. Switch on “follow live price” to keep the numbers current."
                }
            }

            // ------------------------------------------------ Result
            CxCard {
                title: "Result"
                note: view.r.binding && view.ready ? "limited by " + view.bindText[view.r.binding] : ""
                Row {
                    spacing: 8
                    Text {
                        text: view.r.shares.toLocaleString(Qt.locale("en_US"), "f", 0)
                        font.family: Theme.fontFamily
                        font.pixelSize: 26
                        font.bold: true
                        color: Theme.text
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Label95 { text: "shares"; bold: true }
                        Label95 { dim: true; text: core.money(view.r.positionValue) + " · " + view.d(view.r.pctOfAccount, 1) + "% of account" }
                    }
                }
                Grid {
                    width: parent.width
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6
                    readonly property real cw: (width - columnSpacing) / 2
                    PxCard { width: parent.cw; label: "Risk if stopped"; value: core.money(view.r.risk); valueColor: Util.tone(-1); sub: view.d(view.r.riskPct) + "% of account" }
                    PxCard { width: parent.cw; label: "Reward at target"; value: view.r.reward !== null ? core.money(view.r.reward) : "—"; valueColor: Util.tone(1); sub: view.r.rr !== null ? view.d(view.r.rr) + " : 1" : " " }
                    PxCard {
                        width: parent.cw
                        label: "Break-even win rate"
                        value: view.r.breakEvenWinRate !== null ? view.d(view.r.breakEvenWinRate * 100, 0) + "%" : "—"
                        sub: "needed to not lose"
                    }
                    PxCard {
                        width: parent.cw
                        label: "Expected value"
                        value: view.r.ev !== null ? core.money(view.r.ev) : "—"
                        valueColor: view.r.ev === null ? Theme.text : Util.tone(view.r.ev >= 0 ? 1 : -1)
                        sub: view.r.evR !== null ? (view.r.evR >= 0 ? "+" : "") + view.d(view.r.evR) + "R per trade" : " "
                    }
                }
                Repeater {
                    model: view.r.errors
                    Notice { required property string modelData; width: parent.width; kind: "error"; text: Util.plain(modelData) }
                }
                Repeater {
                    model: view.r.warnings
                    Notice { required property string modelData; width: parent.width; kind: "warning"; text: Util.plain(modelData) }
                }
                Row {
                    spacing: 6
                    Button95 { text: "Open order ticket"; isDefault: true; width: 130; enabled: view.ready && view.r.stop !== null; onClicked: view.openTicket() }
                    Button95 { text: "Save to journal"; width: 120; enabled: view.ready; onClicked: view.saveIdea() }
                }
            }

            // ------------------------------------------------ Your limits
            CxCard {
                title: "Your limits"
                note: "saved automatically"
                CxRow { label: "Max risk per trade"; CxRule { key: "maxRiskPct" } CxDim { text: "% = " + core.money(view.equity * view.rules.maxRiskPct / 100) } }
                CxRow { label: "Max per position"; CxRule { key: "maxPositionPct" } CxDim { text: "% = " + core.money(view.r.maxPositionDollars) } }
                CxRow {
                    label: "Max total invested"
                    ComboBox95 {
                        width: 100
                        popupMenu: menu
                        model: ["% of account", "Dollars"]
                        currentIndex: view.rules.maxTotalMode === "dollars" ? 1 : 0
                        onActivated: (i) => view.setRule({ maxTotalMode: i === 1 ? "dollars" : "percent" })
                    }
                    CxRule { visible: view.rules.maxTotalMode === "percent"; key: "maxTotalPct" }
                    CxRule { visible: view.rules.maxTotalMode === "dollars"; key: "maxTotalDollars"; width: 80 }
                    CxDim { text: "= " + core.money(view.r.maxTotalDollars) }
                }
                Item {
                    width: parent.width
                    height: 16
                    Label95 { text: "Invested now" }
                    Label95 { anchors.right: parent.right; bold: true; text: core.money(view.invested) + " of " + core.money(view.r.maxTotalDollars) }
                }
                Bevel {
                    width: parent.width
                    height: 14
                    style: "thinSunken"
                    face: Theme.window
                    readonly property real used: view.usedPct(view.invested, view.r.maxTotalDollars)
                    readonly property real add: Math.min(100 - used, view.usedPct(view.r.positionValue, view.r.maxTotalDollars))
                    Rectangle { x: 1; y: 1; height: parent.height - 2; width: (parent.width - 2) * parent.used / 100; color: Theme.selection }
                    Rectangle { x: 1 + (parent.width - 2) * parent.used / 100; y: 1; height: parent.height - 2; width: (parent.width - 2) * parent.add / 100; color: "#8fb0e0" }
                }
                Label95 { width: parent.width; wrapMode: Text.Wrap; dim: true; text: core.money(view.r.remainingTotalDollars) + " of room left. The lighter part is this trade." }
            }

            // ------------------------------------------------ What if I risk…
            CxCard {
                title: "What if I risk…"
                note: "same stop, other risk levels"
                PxTable {
                    width: parent.width
                    autoHeight: true
                    rows: view.r.whatIf
                    selected: function (w) { return Math.abs(w.riskPct - (view.calc.riskMode === "percent" ? view.num(view.calc.riskPct, 0) : -1)) < 1e-9; }
                    columns: [
                        { title: "Risk", flex: true, text: function (w) { return w.riskPct + "%"; } },
                        { title: "Shares", w: 80, right: true, text: function (w) { return view.grouped(w.shares); } },
                        { title: "Position", w: 100, right: true, text: function (w) { return core.money(w.value); } },
                        { title: "Max loss", w: 90, right: true, text: function (w) { return core.money(w.risk); }, color: function () { return Util.tone(-1); } }
                    ]
                }
                Label95 { width: parent.width; wrapMode: Text.Wrap; dim: true; text: "Sizes are still capped by your limits, so a bigger risk does not always mean more shares." }
            }

            // ------------------------------------------------ Scenarios
            CxCard {
                title: "Scenarios"
                note: "profit or loss if price moves"
                PxTable {
                    width: parent.width
                    autoHeight: true
                    rows: view.r.scenarios
                    columns: [
                        { title: "Move", flex: true, text: function (s) { return s.label; } },
                        { title: "Price", w: 85, right: true, text: function (s) { return core.money(s.price); } },
                        { title: "P/L", w: 95, right: true, text: function (s) { return (s.pl >= 0 ? "+" : "-") + core.money(Math.abs(s.pl)); }, color: function (s) { return Util.tone(s.pl >= 0 ? 1 : -1); } },
                        { title: "% of account", w: 90, right: true, text: function (s) { return (view.input.account > 0 ? view.d(s.pl / view.input.account * 100) : "—") + "%"; },
                          color: function () { return Theme.shadow; } }
                    ]
                }
                Label95 { width: parent.width; wrapMode: Text.Wrap; dim: true; text: "Gaps can jump past a stop, so a real loss can exceed the planned risk." }
            }
        }
    }

    // ---------------------------------------------------------------- pieces
    // a section (upstream .card-x): a group box with a grey note beside its title
    component CxCard: GroupBox95 {
        id: card
        property string note: ""
        default property alias rows: body.data
        width: grid.cardWidth
        height: body.implicitHeight + 32
        Text {
            parent: card
            anchors.right: parent.right
            anchors.rightMargin: 10
            y: 0
            leftPadding: 3
            rightPadding: 3
            text: card.note
            visible: card.note !== ""
            textFormat: Text.PlainText
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            Rectangle { z: -1; anchors.fill: parent; color: Theme.face }
        }
        Column {
            id: body
            width: parent.width
            spacing: 6
        }
    }
    // a labelled line of inputs (upstream's Row)
    component CxRow: Item {
        id: cxRow
        property string label: ""
        property string hint: ""
        default property alias fields: fieldRow.data
        width: parent ? parent.width : 0
        height: 22
        Text {
            width: 146
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            textFormat: Text.StyledText
            text: cxRow.label + (cxRow.hint ? " <font color='" + Theme.shadow + "'>" + cxRow.hint + "</font>" : "")
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Row {
            id: fieldRow
            x: 150
            height: parent.height
            spacing: 4
        }
    }
    // a calculator input (a string in app.calc, saved as typed)
    component CxField: TextField95 {
        property string key: ""
        width: 60
        height: 21
        text: view.calc[key] !== undefined ? String(view.calc[key]) : ""
        onTextChanged: if (key && text !== String(view.calc[key])) view.set({ [key]: text })
    }
    // a limit (a number in app.rules): what is being typed is kept until the field is left
    component CxRule: TextField95 {
        id: ruleField
        property string key: ""
        width: 60
        height: 21
        readonly property var saved: view.rules[key]
        onSavedChanged: if (!input.activeFocus) text = String(saved)
        Component.onCompleted: text = String(saved)
        onTextChanged: {
            if (!input.activeFocus) return;
            const v = view.num(text, view.rules[key]);
            if (v !== view.rules[key]) view.setRule({ [key]: v });
        }
        Connections { target: ruleField.input; function onActiveFocusChanged() { if (!ruleField.input.activeFocus) ruleField.text = String(ruleField.saved); } }
    }
    component CxDim: Text {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        textFormat: Text.PlainText
        color: Theme.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
    }

    Menu95 { id: menu }
}
