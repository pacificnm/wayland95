//// (General)
readonly property var home: App.form("HomeForm")

function setPeriod() {
    var t = home.today(), year = t.substring(0, 4), from = "", to = t;
    switch (periodCombo.text) {
    case "This month": from = home.monthStart(t); break;
    case "Last month": from = home.addMonths(home.monthStart(t), -1); to = home.addDays(home.monthStart(t), -1); break;
    case "This year": from = year + "-01-01"; break;
    case "Last year": from = (Number(year) - 1) + "-01-01"; to = (Number(year) - 1) + "-12-31"; break;
    case "Last 12 months": from = home.addMonths(t, -12); break;
    default: to = "";       // All dates
    }
    fromPicker.value = from;
    toPicker.value = to;
}

function run() {
    var name = reportList.text, from = fromPicker.value || "0000-01-01", to = toPicker.value || "9999-12-31";
    var rows = [], labels = [], values = [], series = null, type = "Column", columns = ["Item", "Amount"], total = 0;
    function line(item, amount) {
        rows.push({ id: rows.length + 1, Item: item, Amount: home.fmt(amount) });
        labels.push(item);
        values.push(Math.round(amount * 100) / 100);
        total += amount;
    }
    if (name === "Spending by Category" || name === "Income by Category" || name === "Tax-related Totals") {
        var taxed = {};
        Db.select("Categories", { where: { TaxRelated: true } }).forEach(function (c) { taxed[c.id] = true; });
        var list = home.totalsByCategory(from, to);
        if (name === "Income by Category")
            list.reverse();
        list.forEach(function (c) {
            if (name === "Spending by Category" && c.total < 0)
                line(c.name, -c.total);
            else if (name === "Income by Category" && c.total > 0)
                line(c.name, c.total);
            else if (name === "Tax-related Totals" && taxed[c.category])
                line(c.name, c.total);
        });
        type = name === "Tax-related Totals" ? "Bar" : "Pie";
    } else if (name === "Income and Expenses by Month") {
        var months = {}, order = [];
        Db.select("Transactions", { orderBy: "Date" }).forEach(function (t) {
            if (t.Date < from || t.Date > to || !home.isFlow(t))
                return;
            var m = t.Date.substring(0, 7);
            if (!months[m]) {
                months[m] = { income: 0, spent: 0 };
                order.push(m);
            }
            if (t.Amount > 0)
                months[m].income += t.Amount;
            else
                months[m].spent -= t.Amount;
        });
        columns = ["Item", "Income", "Expenses", "Amount"];
        series = [{ name: "Income", values: [] }, { name: "Expenses", values: [] }];
        order.forEach(function (m) {
            var label = AppDates.display(m + "-01", "Date", "MMM yyyy", "");
            rows.push({ id: rows.length + 1, Item: label, Income: home.fmt(months[m].income), Expenses: home.fmt(months[m].spent),
                        Amount: home.fmt(months[m].income - months[m].spent) });
            labels.push(label);
            series[0].values.push(Math.round(months[m].income * 100) / 100);
            series[1].values.push(Math.round(months[m].spent * 100) / 100);
            total += months[m].income - months[m].spent;
        });
    } else if (name === "Spending by Payee") {
        var payees = home.names("Payees"), sums = {};
        Db.select("Transactions").forEach(function (t) {
            if (t.Date < from || t.Date > to || !home.isFlow(t) || t.Amount >= 0)
                return;
            var who = payees[t.Payee] || "(no payee)";
            sums[who] = (sums[who] || 0) - t.Amount;
        });
        Object.keys(sums).sort(function (a, b) { return sums[b] - sums[a]; }).slice(0, 15).forEach(function (who) { line(who, sums[who]); });
        type = "Bar";
    } else if (name === "Net Worth over Time") {
        Db.select("WorthHistory", { orderBy: "Day" }).forEach(function (w) {
            if (w.Day >= from && w.Day <= to)
                line(home.showDate(w.Day), w.Worth);
        });
        total = values.length > 0 ? values[values.length - 1] : 0;
        type = "Line";
    } else if (name === "Account Balances") {
        var b = home.balances();
        Db.select("Accounts", { orderBy: "Name" }).forEach(function (a) {
            if (!a.Closed)
                line(a.Name, b[a.id].value);
        });
        type = "Bar";
    } else if (name === "Portfolio by Kind") {
        var kindOf = {}, worth = {};
        Db.select("Securities").forEach(function (s) { kindOf[s.id] = s.Type; });
        Db.select("Holdings").forEach(function (h) {
            var k = kindOf[h.Security] || "Other";
            worth[k] = (worth[k] || 0) + (h.MarketValue || 0);
        });
        Object.keys(worth).sort(function (a, c) { return worth[c] - worth[a]; }).forEach(function (k) { line(k, worth[k]); });
        type = "Doughnut";
    }
    grid.columns = columns;
    grid.rows = rows;
    graph1.graphType = type;
    graph1.title = name;
    graph1.setData(labels, series || [{ name: name, values: values }]);
    totalLabel.text = (name === "Net Worth over Time" ? "Latest: " : "Total: ") + home.fmt(total)
        + "   (" + rows.length + (rows.length === 1 ? " line)" : " lines)");
}

//// load
periodCombo.currentIndex = 2;
setPeriod();
var wanted = reportList.items.indexOf(form.args.report || "");
reportList.currentIndex = wanted >= 0 ? wanted : 0;
run();

//// reportList.changed
run();

//// periodCombo.changed
setPeriod();
run();

//// fromPicker.changed
run();

//// toPicker.changed
run();

//// graph1.clicked
// a slice or a bar: the transactions behind it
if (graph1.clickedIndex >= 0 && (reportList.text === "Spending by Category" || reportList.text === "Income by Category"
                                 || reportList.text === "Spending by Payee"))
    App.openForm("FindForm", { text: graph1.clickedLabel, from: fromPicker.value, to: toPicker.value });
