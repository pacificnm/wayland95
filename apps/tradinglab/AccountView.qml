import QtQuick
import Win95Ui

// The Account screen: the paper accounts (each mirrors one of the user's real accounts by name, brokerage and link, and
// only ever holds simulated money), the active one's balances and returns, its details, matching its cash to the real
// balance (recorded as a deposit or withdrawal), reset and delete. Clicking another account makes it the active one.
// Port of renderer/src/views/AccountView.tsx.
Item {
    id: view
    property var app

    readonly property var a: app && app.snap ? app.snap.account : null
    property var accounts: []
    property var transfers: []
    property bool creating: false
    readonly property var typeLabels: core.callPure("accountTypeLabels", [])
    readonly property var typeIds: Object.keys(typeLabels)
    readonly property var brokers: core.callPure("brokers", [])

    function parseMoney(v) { return Number(String(v).replace(/[$,\s]/g, "")); }
    function when(t) { return new Date(t * 1000).toLocaleDateString(Qt.locale("en_US"), "MMM d, yyyy"); }
    function plText(n) { return (n >= 0 ? "+" : "-") + core.money(Math.abs(n)); }
    function plColor(n) { return n >= 0 ? Util.tone(1) : Util.tone(-1); }

    function load() { api.invoke("acct:list", [true], function (r) { if (r && r.list) view.accounts = r.list; }); }
    // the details form and balance box follow the active account
    readonly property string accountKey: a ? [a.id, a.name, a.broker, a.url, a.type, a.cash].join("|") : ""
    onAccountKeyChanged: {
        if (!a) return;
        details.fill({ name: a.name, broker: a.broker, url: a.url, type: a.type, balance: "" });
        balanceField.text = String(Math.round(a.cash * 100) / 100);
        const id = a.id;
        api.invoke("acct:transfers", [id], function (t) { if (view.a && view.a.id === id) view.transfers = t || []; });
    }
    readonly property var snapAt: app && app.snap ? app.snap.at : null
    onSnapAtChanged: load()
    Component.onCompleted: load()

    function done(r, msg) {
        if (r.ok) app.toast("success", msg);
        else app.toast("error", r.error || "That did not work.");
        app.refreshTrading();
        load();
        return r.ok;
    }
    function switchTo(id) {
        if (a && id === a.id) return;
        creating = false;
        api.invoke("acct:setActive", [id], function (r) {
            const x = view.accounts.find(function (y) { return y.id === id; });
            if (r.ok) app.toast("info", "Now trading " + (x ? x.name : "the account"));
            app.refreshTrading();
        });
    }
    function startCreate() {
        creating = true;
        newFields.fill({ name: "", broker: "", url: "", type: "margin", balance: "100000" });
    }
    function create() {
        const f = newFields.value();
        const bal = parseMoney(f.balance);
        if (!isFinite(bal) || bal < 0) { app.toast("error", "Enter the starting balance as a number, for example 25000."); return; }
        api.invoke("acct:create", [{ name: f.name, broker: f.broker, url: f.url, type: f.type }, bal], function (r) {
            if (view.done(r, "Created " + f.name.trim()) && r.ok) {
                api.invoke("acct:setActive", [r.data.id], function () { view.creating = false; app.refreshTrading(); });
            }
        });
    }
    function saveDetails() {
        if (!a) return;
        const f = details.value();
        api.invoke("acct:update", [a.id, { name: f.name, broker: f.broker, url: f.url, type: f.type }], function (r) { view.done(r, "Account details saved"); });
    }
    function saveBalance() {
        if (!a) return;
        const v = parseMoney(balanceField.text);
        if (!isFinite(v) || v < 0) { app.toast("error", "Enter the cash balance as a number, for example 25000."); return; }
        api.invoke("acct:setBalance", [a.id, v, "Balance set to match the real account"], function (r) { view.done(r, "Cash balance set to " + core.money(v)); });
    }
    function reset() {
        if (!a) return;
        const acct = a;
        app.promptText({
            title: "Reset " + acct.name,
            label: "This deletes this account's positions, orders, trade history and balance changes. Other accounts are not touched. Starting balance:",
            initial: String(acct.startingCash), okLabel: "Reset",
            validate: function (v) { return view.parseMoney(v) > 0 ? null : "Enter a positive amount."; }
        }, function (input) {
            if (input === null || input === undefined) return;
            const cash = view.parseMoney(input);
            api.invoke("trade:reset", [cash], function () {
                app.toast("success", acct.name + " reset to " + core.money(cash));
                app.refreshTrading();
            });
        });
    }
    function remove() {
        if (!a) return;
        const acct = a;
        app.confirm("Delete “" + acct.name + "”? Its positions, orders and history are deleted too. This cannot be undone.", function () {
            api.invoke("acct:remove", [acct.id], function (r) { view.done(r, "Deleted " + acct.name); });
        });
    }

    PxTitle {
        id: title
        width: parent.width
        title: "Accounts"
        PxButton { icon: "add"; text: "New account"; onClicked: view.startCreate() }
    }

    // ---------------------------------------------------------------- the accounts
    Bevel {
        id: listWell
        y: title.height + 4
        width: 230
        height: parent.height - y
        style: "sunken"
        face: Theme.window
        Flickable {
            id: listFlick
            x: 2
            y: 2
            width: parent.width - 4 - (listBar.visible ? listBar.width : 0)
            height: parent.height - 4
            clip: true
            interactive: false
            contentHeight: listColumn.height
            Column {
                id: listColumn
                width: listFlick.width
                Repeater {
                    model: view.accounts
                    delegate: Item {
                        id: acctRow
                        required property var modelData
                        readonly property bool sel: !view.creating && view.a !== null && modelData.id === view.a.id
                        width: listColumn.width
                        height: 38
                        Rectangle { anchors.fill: parent; color: acctRow.sel ? Theme.selection : "transparent" }
                        Icon95 { x: 4; y: 4; name: "money" }
                        Text {
                            x: 24
                            y: 4
                            width: parent.width - 28
                            elide: Text.ElideRight
                            text: acctRow.modelData.name
                            textFormat: Text.PlainText
                            font.bold: true
                            color: acctRow.sel ? Theme.selectionText : Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Text {
                            x: 24
                            y: 20
                            width: parent.width - 28
                            elide: Text.ElideRight
                            text: (acctRow.modelData.broker || "No brokerage set")
                                  + (acctRow.modelData.equity !== undefined && acctRow.modelData.equity !== null ? " · " + core.money(acctRow.modelData.equity) : "")
                            textFormat: Text.PlainText
                            color: acctRow.sel ? Theme.selectionText : Theme.shadow
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        Rectangle { y: parent.height - 1; width: parent.width; height: 1; color: "#e4e4e4" }
                        MouseArea { anchors.fill: parent; onClicked: view.switchTo(acctRow.modelData.id) }
                    }
                }
            }
        }
        ScrollBar95 {
            id: listBar
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: 2
            height: parent.height - 4
            flickable: listFlick
        }
    }

    // ---------------------------------------------------------------- the active account, or the new-account form
    PxPage {
        id: page
        x: listWell.width + 4
        y: listWell.y
        width: parent.width - x
        height: parent.height - y

        // new account
        Column {
            visible: view.creating
            width: page.contentWidth
            spacing: 8
            Heading95 { text: "New paper account"; level: 1 }
            Label95 {
                width: parent.width
                wrapMode: Text.Wrap
                dim: true
                text: "Set this up to mirror one of your real accounts. It only ever holds simulated money."
            }
            AcctFields { id: newFields; width: Math.min(parent.width, 560); withBalance: true }
            Row {
                spacing: 6
                Button95 { text: "Create account"; isDefault: true; enabled: newFields.name.trim() !== ""; onClicked: view.create() }
                Button95 { text: "Cancel"; onClicked: view.creating = false }
            }
        }

        Label95 { visible: !view.creating && view.a === null; text: "Loading…"; dim: true }

        // the active account
        Column {
            visible: !view.creating && view.a !== null
            width: page.contentWidth
            spacing: 8
            Heading95 { width: parent.width; text: view.a ? view.a.name : ""; level: 1 }
            Row {
                spacing: 8
                height: 22
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: badge.implicitWidth + 12
                    height: 17
                    radius: 8
                    color: "#dce8f5"
                    border.color: "#7f9db9"
                    Label95 { id: badge; anchors.centerIn: parent; text: "paper account"; small: true; color: "#1f3f66" }
                }
                Label95 {
                    anchors.verticalCenter: parent.verticalCenter
                    dim: true
                    text: view.a && view.a.broker ? "Mirrors a " + view.a.broker + " account" : "No brokerage set"
                }
                PxButton {
                    visible: view.a !== null && view.a.url !== ""
                    icon: "link"
                    text: "Open " + (view.a && view.a.broker ? view.a.broker : "account")
                    onClicked: api.invoke("app:openExternal", [view.a.url])
                }
            }
            Grid {
                id: cards
                width: parent.width
                columns: width >= 520 ? 3 : 2
                spacing: 6
                readonly property real cardWidth: (width - (columns - 1) * spacing) / columns
                PxCard { width: cards.cardWidth; label: "Net liquidation (equity)"; value: view.a ? core.money(view.a.equity) : "" }
                PxCard { width: cards.cardWidth; label: "Buying power available"; value: view.a ? core.money(view.a.buyingPower) : "" }
                PxCard { width: cards.cardWidth; label: "Cash"; value: view.a ? core.money(view.a.cash) : "" }
                PxCard { width: cards.cardWidth; label: "Open P/L"; value: view.a ? view.plText(view.a.unrealizedPl) : ""; valueColor: view.a ? view.plColor(view.a.unrealizedPl) : Theme.text }
                PxCard { width: cards.cardWidth; label: "Realized P/L"; value: view.a ? view.plText(view.a.realizedPl) : ""; valueColor: view.a ? view.plColor(view.a.realizedPl) : Theme.text }
                PxCard {
                    width: cards.cardWidth
                    label: "Total return"
                    value: view.a ? (view.a.totalReturnPct >= 0 ? "+" : "") + view.a.totalReturnPct.toFixed(2) + "%" : ""
                    valueColor: view.a ? view.plColor(view.a.totalReturnPct) : Theme.text
                }
            }
            Label95 {
                width: Math.min(parent.width, 640)
                wrapMode: Text.Wrap
                dim: true
                text: !view.a ? "" : "Started with " + core.money(view.a.startingCash) + " (including any deposits or withdrawals below). "
                      + (view.a.type === "cash" ? "This is a cash account: buying power is your cash, less the cost of orders still waiting to fill, and short selling is not allowed."
                                                : "This is a margin account: buying power is 2:1 (equity × 2 minus the market value of your positions), less the cost of orders still waiting to fill.")
                      + (view.a.reserved > 0 ? " " + core.money(view.a.reserved) + " is set aside for working orders right now." : "")
                      + " Fills are simulated from real 1-minute prices with no commissions or slippage. New orders go to this account; working orders in your other accounts keep filling."
            }

            GroupBox95 {
                width: parent.width
                height: detailsColumn.implicitHeight + 30
                title: "Account details"
                Column {
                    id: detailsColumn
                    width: parent.width
                    spacing: 8
                    AcctFields { id: details; width: Math.min(parent.width, 560); withBalance: false }
                    Button95 { text: "Save details"; enabled: details.name.trim() !== ""; onClicked: view.saveDetails() }
                }
            }

            GroupBox95 {
                width: parent.width
                height: balanceColumn.implicitHeight + 30
                title: "Match my real balance"
                Column {
                    id: balanceColumn
                    width: parent.width
                    spacing: 8
                    Row {
                        spacing: 6
                        TextField95 { id: balanceField; width: 200; height: 23; onAccepted: view.saveBalance() }
                        Button95 { text: "Set cash balance"; onClicked: view.saveBalance() }
                    }
                    Label95 {
                        width: Math.min(parent.width, 640)
                        wrapMode: Text.Wrap
                        dim: true
                        text: "Sets this account's cash to the number you enter. The difference is recorded as a deposit or withdrawal, so total return keeps measuring your trading, not money you added or took out. Positions are not changed."
                    }
                    PxTable {
                        visible: view.transfers.length > 0
                        width: Math.min(parent.width, 420)
                        autoHeight: true
                        rows: view.transfers
                        columns: [
                            { title: "Date", w: 120, text: function (t) { return view.when(t.time); }, color: function () { return Theme.shadow; } },
                            { title: "Change", flex: true, text: function (t) { return t.amount >= 0 ? "Deposit" : "Withdrawal"; } },
                            { title: "Amount", w: 130, right: true, text: function (t) { return view.plText(t.amount); }, color: function (t) { return view.plColor(t.amount); } }
                        ]
                    }
                }
            }

            Row {
                spacing: 6
                Button95 { text: "Reset account…"; onClicked: view.reset() }
                Button95 {
                    text: "Delete account"
                    enabled: view.accounts.length >= 2
                    onClicked: view.remove()
                }
                Label95 { visible: view.accounts.length < 2; anchors.verticalCenter: parent.verticalCenter; dim: true; text: "You need at least one account" }
            }
        }
    }

    // name, type, brokerage (typed, or picked from the list), link and, for a new account, the starting balance
    component AcctFields: Grid {
        id: fields
        property bool withBalance: false
        property string type: "margin"
        readonly property string name: nameField.text
        function fill(f) {
            nameField.text = f.name || "";
            brokerField.text = f.broker || "";
            urlField.text = f.url || "";
            balanceInput.text = f.balance || "";
            type = f.type || "margin";
        }
        function value() { return { name: nameField.text, broker: brokerField.text, url: urlField.text, type: type, balance: balanceInput.text }; }
        columns: 2
        columnSpacing: 10
        rowSpacing: 6
        readonly property real fieldWidth: (width - columnSpacing) / 2

        Column {
            spacing: 2
            Label95 { text: "Account name" }
            TextField95 { id: nameField; width: fields.fieldWidth; height: 21; PxHint { text: "e.g. Schwab IRA (paper)" } }
        }
        Column {
            spacing: 2
            Label95 { text: "Account type" }
            ComboBox95 {
                width: fields.fieldWidth
                popupMenu: menu
                model: view.typeIds.map(function (t) { return view.typeLabels[t]; })
                currentIndex: Math.max(0, view.typeIds.indexOf(fields.type))
                onActivated: (i) => fields.type = view.typeIds[i]
            }
        }
        Column {
            spacing: 2
            Label95 { text: "Brokerage" }
            Row {
                spacing: 2
                TextField95 { id: brokerField; width: fields.fieldWidth - 24; height: 21; PxHint { text: "e.g. Charles Schwab" } }
                PxButton {
                    width: 22
                    height: 21
                    text: "▼"
                    onClicked: {
                        const p = mapToItem(menu, 0, height);
                        menu.popup(p.x, p.y, view.brokers.map(function (b) { return { text: b, action: function () { brokerField.text = b; } }; }));
                    }
                }
            }
        }
        Column {
            spacing: 2
            Label95 { text: "Link to the real account" }
            TextField95 { id: urlField; width: fields.fieldWidth; height: 21; PxHint { text: "https://…" } }
        }
        Column {
            visible: fields.withBalance
            spacing: 2
            Label95 { text: "Starting balance" }
            TextField95 { id: balanceInput; width: fields.fieldWidth; height: 21; PxHint { text: "25000" } }
        }
    }
    Menu95 { id: menu }
}
