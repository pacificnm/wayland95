//// (General)
readonly property var home: App.form("HomeForm")
property var accountIds: []

function fill() {
    var only = accountCombo.currentIndex > 0 ? accountIds[accountCombo.currentIndex - 1] : null;
    var secs = {}, bySecurity = {}, order = [];
    Db.select("Securities").forEach(function (s) { secs[s.id] = s; });
    Db.select("Holdings", { where: only === null ? {} : { Account: only } }).forEach(function (h) {
        var line = bySecurity[h.Security];
        if (!line) {
            line = bySecurity[h.Security] = { security: h.Security, shares: 0, value: 0, cost: 0 };
            order.push(line);
        }
        line.shares += h.Quantity || 0;
        line.value += h.MarketValue || 0;
        line.cost += h.CostBasis || 0;
    });
    order.sort(function (a, b) { return b.value - a.value; });
    var value = 0, cost = 0;
    grid.rows = order.map(function (line) {
        var s = secs[line.security] || {};
        value += line.value;
        cost += line.cost;
        return { id: line.security, Symbol: s.Symbol || "?", Name: s.Company || "",
                 Shares: String(Math.round(line.shares * 10000) / 10000),
                 Price: s.LastPrice === null || s.LastPrice === undefined ? "" : home.fmt(s.LastPrice),
                 Value: home.fmt(line.value), Cost: home.fmt(line.cost), Gain: home.fmt(line.value - line.cost),
                 Percent: line.cost > 0 ? ((line.value - line.cost) / line.cost * 100).toFixed(1) + "%" : "" };
    });
    graph1.setData(order.map(function (line) { return (secs[line.security] || {}).Symbol || "?"; }),
                   [{ name: "Value", values: order.map(function (line) { return Math.round(line.value * 100) / 100; }) }]);
    totalLabel.text = "Value " + home.fmt(value) + "   Cost " + home.fmt(cost) + "   Gain " + home.fmt(value - cost);
    totalLabel.foreColor = value - cost < 0 ? "#c00000" : "#008000";
}

Timer { id: soon; interval: 300; onTriggered: fill() }
Connections {
    target: Db
    function onChanged(table) {
        if (table === "Holdings" || table === "Securities")
            soon.restart();
    }
}

//// load
var accounts = Db.select("Accounts", { orderBy: "Name" }).filter(function (a) { return home.isInvestment(a.Type); });
accountIds = accounts.map(function (a) { return a.id; });
accountCombo.items = ["(all investment accounts)"].concat(accounts.map(function (a) { return a.Name; }));
accountCombo.currentIndex = 0;
fill();

//// accountCombo.changed
fill();

//// grid.rowActivated
App.openForm("SecuritiesForm", { security: grid.currentId });

//// pricesButton.clicked
statusLabel.text = "Asking Schwab for prices...";
home.updateQuotes(Db.select("Securities"), function (ok, result) {
    statusLabel.text = ok ? result + " prices updated at " + Qt.formatTime(new Date(), "h:mm AP") + "." : result;
});

//// tradeButton.clicked
App.openForm("TradeForm", { account: accountCombo.currentIndex > 0 ? accountIds[accountCombo.currentIndex - 1] : null });

//// securityButton.clicked
App.openForm("SecuritiesForm", { security: grid.currentId });
