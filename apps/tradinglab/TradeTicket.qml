import QtQuick
import Win95Ui

// The order ticket shown over the chart while app.ticket is set (a Think-or-Swim style ticket): side, quantity, type,
// limit/stop/trail, time in force, single/bracket/OCO with a profit target and a protective leg, a live analysis of
// cost, risk and reward, a review step, and "record in trading journal". Port of renderer/src/components/TradeTicket.tsx.
// Nothing is sent except by the Send order button (invariant 1).
Bevel {
    id: ticket
    property var app

    readonly property var draft: app && app.ticket ? app.ticket : core.emptyDraft("buy")
    readonly property string symbol: app ? app.symbol : ""
    readonly property var snap: app ? app.snap : null
    readonly property var quote: app ? app.quote : ({ last: null, change: null, changePct: null })
    readonly property bool live: app ? app.live : false

    property string step: "edit"        // edit | review
    property bool sending: false
    property var error: null            // the engine's errors from the last send
    property string riskDollars: ""

    property real maxHeight: 600        // the chart area's height, less margins
    style: "raised"
    width: 340
    height: Math.min(maxHeight, caption.height + 8 + (live ? flick.contentHeight : offline.implicitHeight + 16))

    function set(patch) { error = null; app.ticket = Object.assign({}, app.ticket, patch); }
    function close() { app.ticket = null; }
    function r2(n) { return String(Math.round(n * 100) / 100); }
    function m(n) { return n === null || n === undefined ? "—" : core.money(n, 2); }

    readonly property var sides: ["buy", "sell", "sell_short", "buy_to_cover"]
    readonly property var allTypes: ["market", "limit", "stop", "stop_limit", "trailing_stop"]
    readonly property var allStrategies: [["single", "Single order"], ["bracket", "1st triggers OCO (bracket)"], ["oco", "OCO (exit orders)"]]
    // Working orders are filled against intraday bars. A plan without 1-minute bars uses the finest it has; without any, only plain market orders can fill.
    readonly property bool noBars: snap !== null && snap.fillBars === null
    readonly property var types: noBars ? ["market"] : allTypes
    readonly property var strategies: noBars ? [["single", "Single order"]] : allStrategies
    function enforceNoBars() {
        if (app && app.ticket && noBars && (draft.type !== "market" || draft.strategy !== "single"))
            app.ticket = Object.assign({}, app.ticket, { type: "market", strategy: "single" });
    }
    onNoBarsChanged: enforceNoBars()
    onDraftChanged: enforceNoBars()

    readonly property var spec: core.draftToSpec(draft, symbol)
    readonly property var position: app ? app.positionOf(symbol) : null
    // the snapshot's buying power is already net of working orders
    readonly property var a: core.analyze(spec, { last: quote.last, positionQty: position ? position.qty : 0, buyingPower: snap ? snap.account.buyingPower : 0,
                                                 reserved: 0, canShort: !(snap && snap.account.type === "cash") })
    readonly property bool buy: draft.side === "buy" || draft.side === "buy_to_cover"
    readonly property bool long_: draft.side === "buy" || draft.side === "sell"
    readonly property bool opens: draft.side === "buy" || draft.side === "sell_short"
    readonly property var entry: a.entry
    readonly property bool hasStopLeg: draft.strategy !== "single"
    readonly property color sideColor: buy ? "#007000" : "#b00000"
    readonly property real equity: snap ? snap.account.equity : 0

    function setQtyFromRisk(risk) {
        if (entry !== null && a.stopPrice !== null && Math.abs(entry - a.stopPrice) > 0)
            set({ qty: String(Math.max(1, Math.floor(risk / Math.abs(entry - a.stopPrice)))) });
    }

    function send() {
        sending = true;
        error = null;
        const d = draft, sp = spec, an = a, sym = symbol;
        // grab the chart while the draft lines are still on it, for the journal
        let shot = null;
        if (d.journal && !d.journalId) app.chartData.capture(function (png) { shot = png; });
        api.invoke("trade:place", [sp], function (r) {
            ticket.sending = false;
            if (!r.ok) {
                ticket.error = r.errors;
                ticket.step = "edit";
                app.toast("error", r.errors[0], { title: "Order rejected" });
                return;
            }
            // an immediate fill is announced by the engine's own "filled" toast
            if (!r.filledNow) app.toast("success", core.describeOrder(sp)[0].split("  —")[0], { title: "Order placed" });
            const failed = function () { app.toast("warning", "The order was placed, but saving the journal entry failed."); };
            if (d.journalId) {
                // Claude's idea (or an entry the user opened in the ticket): link the orders to it
                const extra = (d.journalNote || "").trim();
                const jid = d.journalId;
                api.invoke("journal:get", [jid], function (cur) {
                    const patch = { order_ids: r.orderIds, status: "taken" };
                    const body = cur && cur.body ? cur.body : "";
                    if (extra) patch.body = body + (body ? "\n\n" : "") + "**My notes:** " + extra;
                    api.invoke("journal:update", [jid, patch], function (u) {
                        if (u && u.ok === false) { failed(); return; }
                        app.toast("success", "Orders linked to your journal entry", { action: { label: "Open journal", onClick: function () { app.openJournal(jid); } } });
                    });
                });
            } else if (d.journal && (sp.side === "buy" || sp.side === "sell_short")) {
                const claude = d.source === "claude";
                api.invoke("journal:create", [{
                    kind: "trade", status: "taken", source: d.source, symbol: sym, direction: sp.side === "buy" ? "long" : "short",
                    title: core.sideLabel(sp.side) + " " + sym + (sp.strategy === "bracket" ? " (bracket)" : ""),
                    body: [claude ? (d.note || "") : "", (d.journalNote || "").trim()].filter(function (x) { return x; }).join("\n\n"),
                    plan_entry: an.entry, plan_stop: an.stopPrice, plan_target: an.targetPrice, plan_qty: sp.qty, order_ids: r.orderIds, image: shot
                }], function (e) {
                    if (!e || e.ok === false) { failed(); return; }
                    app.toast("info", "Recorded in your trading journal", { action: { label: "Open journal", onClick: function () { app.openJournal(e.id); } } });
                });
            }
            ticket.close();
        });
    }

    // ------------------------------------------------------------------ caption
    Rectangle {
        id: caption
        x: 3
        y: 3
        width: parent.width - 6
        height: 22
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.titleActiveStart }
            GradientStop { position: 1; color: Theme.titleActiveEnd }
        }
        Row {
            x: 5
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: ticket.live ? core.sideLabel(ticket.draft.side) + " " + ticket.symbol : "Order ticket"
                color: Theme.titleActiveText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                font.bold: true
                textFormat: Text.PlainText
            }
            Text {
                visible: ticket.live && ticket.quote.last !== null
                text: ticket.quote.last !== null ? core.money(ticket.quote.last, 2) + (ticket.quote.changePct !== null ? "  " + Util.pct(ticket.quote.changePct) : "") : ""
                color: Theme.titleActiveText
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                textFormat: Text.PlainText
            }
        }
        ToolButton95 {
            anchors.right: parent.right
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 14
            text: "✕"
            onClicked: ticket.close()
            ToolTipArea { text: "Close (discard)" }
        }
    }

    // ------------------------------------------------------------------ no FMP key
    Item {
        id: offline
        visible: !ticket.live
        x: 8
        y: caption.y + caption.height + 8
        width: parent.width - 16
        implicitHeight: offlineText.implicitHeight + 8
        Icon95 { name: "error"; y: 2 }
        Label95 {
            id: offlineText
            x: 22
            width: parent.width - 22
            wrapMode: Text.Wrap
            text: "Trading needs live prices to simulate fills. Add your FMP key in File → Settings."
        }
    }

    // ------------------------------------------------------------------ the ticket
    Flickable {
        id: flick
        visible: ticket.live
        x: 3
        y: caption.y + caption.height + 2
        width: parent.width - 6 - (vbar.needed ? vbar.width : 0)
        height: parent.height - y - 3
        clip: true
        contentHeight: body.implicitHeight + 16
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: body
            x: 8
            y: 8
            width: flick.width - 16
            spacing: 6

            // Claude prepared it
            Rectangle {
                visible: ticket.draft.source === "claude"
                width: parent.width
                height: claudeText.implicitHeight + 8
                color: "#e8f0ff"
                border.color: Theme.shadow
                Label95 {
                    id: claudeText
                    x: 4
                    y: 4
                    width: parent.width - 8
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    text: "<b>Claude set this up.</b> Review every field before sending; you decide whether to place it."
                          + (ticket.draft.note ? "<br><font color='#606060'>" + Util.plain(ticket.draft.note).replace(/&/g, "&amp;") + "</font>" : "")
                }
            }

            // ======================= edit
            Column {
                visible: ticket.step === "edit"
                width: parent.width
                spacing: 6

                Grid {
                    columns: 2
                    columnSpacing: 8
                    rowSpacing: 6
                    verticalItemAlignment: Grid.AlignVCenter

                    Lbl { text: "Action" }
                    ComboBox95 {
                        width: 170
                        popupMenu: popup
                        model: ticket.sides.map(function (s) { return core.sideLabel(s); })
                        currentIndex: ticket.sides.indexOf(ticket.draft.side)
                        onActivated: (i) => ticket.set({ side: ticket.sides[i] })
                    }

                    Lbl { text: "Quantity" }
                    Row {
                        spacing: 4
                        NumField { width: 70; value: ticket.draft.qty; onEdited: (v) => ticket.set({ qty: v }) }
                        Repeater {
                            model: ticket.position && (ticket.draft.side === "sell" || ticket.draft.side === "buy_to_cover") ? [25, 50, 100] : []
                            delegate: Chip {
                                required property int modelData
                                text: modelData === 100 ? "All" : modelData + "%"
                                onClicked: ticket.set({ qty: String(Math.max(1, Math.floor(Math.abs(ticket.position.qty) * modelData / 100))) })
                            }
                        }
                    }

                    Lbl { text: "Order type" }
                    ComboBox95 {
                        width: 170
                        popupMenu: popup
                        model: ticket.types.map(function (t) { return core.typeLabel(t); })
                        currentIndex: Math.max(0, ticket.types.indexOf(ticket.draft.type))
                        onActivated: (i) => ticket.set({ type: ticket.types[i] })
                    }

                    Lbl { text: "Limit price"; visible: ticket.draft.type === "limit" || ticket.draft.type === "stop_limit" }
                    Row {
                        visible: ticket.draft.type === "limit" || ticket.draft.type === "stop_limit"
                        spacing: 4
                        NumField { value: ticket.draft.limit; onEdited: (v) => ticket.set({ limit: v }) }
                        Chip { visible: ticket.quote.last !== null; text: "Last"; onClicked: ticket.set({ limit: ticket.r2(ticket.quote.last) }) }
                    }
                    Lbl { text: "Stop price"; visible: ticket.draft.type === "stop" || ticket.draft.type === "stop_limit" }
                    Row {
                        visible: ticket.draft.type === "stop" || ticket.draft.type === "stop_limit"
                        spacing: 4
                        NumField { value: ticket.draft.stop; onEdited: (v) => ticket.set({ stop: v }) }
                        Chip { visible: ticket.quote.last !== null; text: "Last"; onClicked: ticket.set({ stop: ticket.r2(ticket.quote.last) }) }
                    }
                    Lbl { text: "Trail by"; visible: ticket.draft.type === "trailing_stop" }
                    Row {
                        visible: ticket.draft.type === "trailing_stop"
                        spacing: 4
                        NumField { width: 70; value: ticket.draft.trailAmount; onEdited: (v) => ticket.set({ trailAmount: v }) }
                        ComboBox95 {
                            width: 48
                            popupMenu: popup
                            model: ["$", "%"]
                            currentIndex: ticket.draft.trailUnit === "%" ? 1 : 0
                            onActivated: (i) => ticket.set({ trailUnit: i === 1 ? "%" : "$" })
                        }
                    }

                    Lbl { text: "Time in force" }
                    ComboBox95 {
                        width: 210
                        popupMenu: popup
                        model: ["DAY — until today's close", "GTC — good till cancelled"]
                        currentIndex: ticket.draft.tif === "gtc" ? 1 : 0
                        onActivated: (i) => ticket.set({ tif: i === 1 ? "gtc" : "day" })
                    }

                    Lbl { text: "Strategy" }
                    ComboBox95 {
                        width: 210
                        popupMenu: popup
                        model: ticket.strategies.map(function (s) { return s[1]; })
                        currentIndex: Math.max(0, ticket.strategies.findIndex(function (s) { return s[0] === ticket.draft.strategy; }))
                        onActivated: (i) => ticket.set({ strategy: ticket.strategies[i][0] })
                    }
                }

                // the exit legs
                Rectangle {
                    visible: ticket.hasStopLeg
                    width: parent.width
                    height: legs.implicitHeight + 12
                    color: "transparent"
                    border.color: Theme.shadow
                    Grid {
                        id: legs
                        x: 6
                        y: 6
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        Lbl { visible: ticket.draft.strategy === "bracket"; text: "Profit target"; color: "#007000"; height: 21 }
                        Column {
                            visible: ticket.draft.strategy === "bracket"
                            spacing: 4
                            NumField { value: ticket.draft.target; onEdited: (v) => ticket.set({ target: v }) }
                            Flow {
                                width: 200
                                spacing: 3
                                Repeater {
                                    model: [2, 5, 10]
                                    delegate: Chip {
                                        required property int modelData
                                        enabled: ticket.entry !== null
                                        text: (ticket.long_ ? "+" : "−") + modelData + "%"
                                        onClicked: ticket.set({ target: ticket.r2(ticket.entry * (1 + (ticket.long_ ? 1 : -1) * modelData / 100)) })
                                    }
                                }
                                Repeater {
                                    model: ticket.entry !== null && ticket.a.stopPrice !== null ? [1, 2, 3] : []
                                    delegate: Chip {
                                        required property int modelData
                                        text: modelData + "R"
                                        onClicked: ticket.set({ target: ticket.r2(ticket.entry + (ticket.long_ ? 1 : -1) * modelData * Math.abs(ticket.entry - ticket.a.stopPrice)) })
                                    }
                                }
                            }
                        }

                        Lbl { text: "Stop loss"; color: "#b00000"; height: 21 }
                        Column {
                            spacing: 4
                            ComboBox95 {
                                width: 130
                                popupMenu: popup
                                model: ["Stop", "Stop limit", "Trailing stop"]
                                currentIndex: Math.max(0, ["stop", "stop_limit", "trailing_stop"].indexOf(ticket.draft.protectType))
                                onActivated: (i) => ticket.set({ protectType: ["stop", "stop_limit", "trailing_stop"][i] })
                            }
                            Row {
                                visible: ticket.draft.protectType !== "trailing_stop"
                                spacing: 4
                                Lbl { text: "stop"; dim: true; height: 21 }
                                NumField { width: 70; value: ticket.draft.protectStop; onEdited: (v) => ticket.set({ protectStop: v }) }
                                Lbl { visible: ticket.draft.protectType === "stop_limit"; text: "limit"; dim: true; height: 21 }
                                NumField { visible: ticket.draft.protectType === "stop_limit"; width: 70; value: ticket.draft.protectLimit; onEdited: (v) => ticket.set({ protectLimit: v }) }
                            }
                            Row {
                                visible: ticket.draft.protectType !== "trailing_stop"
                                spacing: 3
                                Repeater {
                                    model: [1, 2, 5]
                                    delegate: Chip {
                                        required property int modelData
                                        enabled: ticket.entry !== null
                                        text: (ticket.long_ ? "−" : "+") + modelData + "%"
                                        onClicked: {
                                            const v = ticket.r2(ticket.entry * (1 + (ticket.long_ ? -1 : 1) * modelData / 100));
                                            const p = { protectStop: v };
                                            if (ticket.draft.protectType === "stop_limit" && !ticket.draft.protectLimit) p.protectLimit = v;
                                            ticket.set(p);
                                        }
                                    }
                                }
                            }
                            Row {
                                visible: ticket.draft.protectType === "trailing_stop"
                                spacing: 4
                                NumField { width: 70; value: ticket.draft.protectTrail; onEdited: (v) => ticket.set({ protectTrail: v }) }
                                ComboBox95 {
                                    width: 48
                                    popupMenu: popup
                                    model: ["$", "%"]
                                    currentIndex: ticket.draft.protectTrailUnit === "%" ? 1 : 0
                                    onActivated: (i) => ticket.set({ protectTrailUnit: i === 1 ? "%" : "$" })
                                }
                            }
                        }
                    }
                }

                Flow {
                    visible: ticket.draft.strategy === "bracket" && ticket.a.stopPrice !== null && ticket.entry !== null
                    width: parent.width
                    spacing: 3
                    Lbl { text: "Size to risk"; dim: true; height: 20 }
                    Repeater {
                        model: [0.5, 1, 2]
                        delegate: Chip {
                            required property real modelData
                            text: modelData + "% of equity"
                            onClicked: ticket.setQtyFromRisk(ticket.equity * modelData / 100)
                        }
                    }
                    NumField {
                        width: 60
                        height: 20
                        value: ticket.riskDollars
                        onEdited: (v) => { ticket.riskDollars = v; const n = parseFloat(v); if (n > 0) ticket.setQtyFromRisk(n); }
                    }
                    Lbl { text: "$"; dim: true; height: 20 }
                }

                Fine { visible: ticket.noBars; text: "Your FMP plan has no intraday price bars, so only market orders are offered, and only while the market is open." }
                Fine {
                    visible: !!(ticket.snap && ticket.snap.fillBars && ticket.snap.fillBars.seconds > 60)
                    text: ticket.snap && ticket.snap.fillBars ? "Orders waiting for a price are filled against " + ticket.snap.fillBars.label + " bars, because your FMP plan has no 1-minute bars. Fills are less exact." : ""
                }

                // the summary
                Rectangle {
                    width: parent.width
                    height: summary.implicitHeight + 10
                    color: Theme.window
                    border.color: Theme.shadow
                    Column {
                        id: summary
                        x: 6
                        y: 5
                        width: parent.width - 12
                        spacing: 2
                        SumRow { label: "Account"; value: ticket.snap ? ticket.snap.account.name + (!/paper/i.test(ticket.snap.account.name) ? " (paper)" : "") : "—" }
                        SumRow { label: "Buying power available"; value: ticket.snap ? ticket.m(ticket.snap.account.buyingPower) : "—" }
                        SumRow {
                            visible: !!ticket.position
                            label: "Position"
                            value: ticket.position ? (ticket.position.qty > 0 ? "Long " : "Short ") + Math.abs(ticket.position.qty) + " @ " + Number(ticket.position.avg).toFixed(2) : ""
                        }
                        SumRow { label: "Est. " + (ticket.buy ? "cost" : "proceeds"); value: ticket.m(ticket.a.notional) }
                        SumRow {
                            visible: ticket.a.risk !== null
                            label: "Risk"
                            tone: "#b00000"
                            value: ticket.a.risk !== null ? ticket.m(ticket.a.risk) + (ticket.equity > 0 ? " (" + (ticket.a.risk / ticket.equity * 100).toFixed(2) + "% of equity)" : "") : ""
                        }
                        SumRow { visible: ticket.a.reward !== null; label: "Reward"; tone: "#007000"; value: ticket.m(ticket.a.reward) }
                        SumRow { visible: ticket.a.rr !== null; label: "Reward : risk"; value: ticket.a.rr !== null ? ticket.a.rr.toFixed(2) + " : 1" : "" }
                    }
                }

                // record in the journal
                Column {
                    visible: ticket.opens
                    width: parent.width
                    spacing: 4
                    Lbl {
                        visible: !!ticket.draft.journalId
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: "Linked to journal entry #" + ticket.draft.journalId + ". Sending links the orders to it."
                    }
                    CheckBox95 {
                        visible: !ticket.draft.journalId
                        text: "Record in trading journal"
                        checked: !!ticket.draft.journal
                        onToggled: (on) => ticket.set({ journal: on })
                    }
                    TextArea95 {
                        id: jnote
                        visible: !!(ticket.draft.journal || ticket.draft.journalId)
                        width: parent.width
                        height: 44
                        placeholder: ticket.draft.journalId ? "Add your own notes to the entry (optional)" : "Why this trade? What would prove you wrong?"
                        Component.onCompleted: text = ticket.draft.journalNote || ""
                        onTextChanged: if (text !== (ticket.draft.journalNote || "")) ticket.set({ journalNote: text })
                    }
                }

                Repeater { model: ticket.a.warnings; delegate: Msg { required property string modelData; kind: "warning"; text: modelData } }
                Repeater { model: ticket.a.errors; delegate: Msg { required property string modelData; kind: "error"; text: modelData } }
                Repeater { model: ticket.error || []; delegate: Msg { required property string modelData; kind: "error"; text: modelData } }

                Row {
                    anchors.right: parent.right
                    spacing: 6
                    Button95 { width: 80; text: "Cancel"; onClicked: ticket.close() }
                    Button95 {
                        width: 110
                        isDefault: true
                        text: "Review order"
                        enabled: ticket.a.errors.length === 0
                        onClicked: ticket.step = "review"
                    }
                }
            }

            // ======================= review
            Column {
                visible: ticket.step === "review"
                width: parent.width
                spacing: 6
                Lbl { text: "Confirm order"; dim: true }
                Rectangle {
                    width: parent.width
                    height: review.implicitHeight + 10
                    color: Theme.window
                    border.color: Theme.shadow
                    Column {
                        id: review
                        x: 6
                        y: 5
                        width: parent.width - 12
                        spacing: 3
                        Repeater {
                            model: ticket.step === "review" ? core.describeOrder(ticket.spec) : []
                            delegate: Lbl { required property string modelData; width: review.width; wrapMode: Text.Wrap; text: modelData; bold: true }
                        }
                    }
                }
                Column {
                    width: parent.width
                    spacing: 2
                    SumRow { visible: !!ticket.snap; label: "Sent to account"; value: ticket.snap ? ticket.snap.account.name + (ticket.snap.account.broker ? " · " + ticket.snap.account.broker : "") : "" }
                    SumRow { visible: ticket.a.notional !== null; label: "Est. " + (ticket.buy ? "cost" : "proceeds"); value: ticket.m(ticket.a.notional) }
                    SumRow { visible: ticket.a.risk !== null; label: "Max risk at stop"; tone: "#b00000"; value: ticket.m(ticket.a.risk) }
                    SumRow { visible: ticket.a.reward !== null; label: "Reward at target"; tone: "#007000"; value: ticket.m(ticket.a.reward) }
                }
                Fine { text: "Paper trading: fills are simulated from real 1-minute prices. No commissions or slippage." }
                Repeater { model: ticket.step === "review" ? ticket.a.warnings : []; delegate: Msg { required property string modelData; kind: "warning"; text: modelData } }
                Row {
                    anchors.right: parent.right
                    spacing: 6
                    Button95 { width: 80; text: "Back"; enabled: !ticket.sending; onClicked: ticket.step = "edit" }
                    Button95 {
                        width: 110
                        isDefault: true
                        text: ticket.sending ? "Sending…" : "Send order"
                        enabled: !ticket.sending
                        onClicked: ticket.send()
                    }
                }
            }
        }
    }
    ScrollBar95 {
        id: vbar
        visible: ticket.live && needed
        flickable: flick
        x: parent.width - width - 3
        y: flick.y
        width: 16
        height: flick.height
    }

    // ------------------------------------------------------------------ small parts
    component Lbl: Label95 { verticalAlignment: Text.AlignVCenter }
    component Fine: Label95 { width: parent.width; wrapMode: Text.Wrap; small: true; dim: true }
    component Chip: ToolButton95 { height: 20 }
    component NumField: TextField95 {
        id: nf
        property string value: ""
        signal edited(string v)
        width: 90
        text: value
        Connections { target: nf.input; function onTextEdited() { nf.edited(nf.text); } }
    }
    component SumRow: Item {
        id: sr
        property string label: ""
        property string value: ""
        property color tone: Theme.text
        width: parent.width
        height: 16
        Label95 { text: sr.label; dim: true }
        Label95 { anchors.right: parent.right; text: sr.value; color: sr.tone; bold: true }
    }
    component Msg: Row {
        id: msg
        property string kind: "warning"
        property string text: ""
        width: parent.width
        spacing: 4
        Icon95 { name: msg.kind }
        Label95 { width: msg.width - 20; wrapMode: Text.Wrap; text: msg.text; color: msg.kind === "error" ? "#b00000" : Theme.text }
    }
}
