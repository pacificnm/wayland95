//// (General)
readonly property var home: App.form("HomeForm")

//// load
chat1.addTool("spending_by_category",
    "What was spent and earned in each category between two days (transfers and trades left out). "
    + "Use it for any question about spending, income, or where the money went.",
    { from: { type: "string", description: "The first day, as YYYY-MM-DD." },
      to: { type: "string", description: "The last day, as YYYY-MM-DD." } },
    function (input) {
        return home.totalsByCategory(input.from, input.to).map(function (c) {
            return { category: c.name, kind: c.kind, total: Math.round(c.total * 100) / 100 };
        });
    });
chat1.addTool("account_balances",
    "Every open account with its type and what it's worth today, and the net worth. "
    + "Use it for questions about balances, debts or net worth.",
    {},
    function () {
        var b = home.balances(), w = home.netWorth();
        return { accounts: Db.select("Accounts", { orderBy: "Name" }).filter(function (a) { return !a.Closed; }).map(function (a) {
                     return { name: a.Name, type: a.Type, cash: Math.round(b[a.id].cash * 100) / 100,
                              investments: Math.round(b[a.id].holdings * 100) / 100 };
                 }),
                 assets: Math.round(w.assets * 100) / 100, liabilities: Math.round(w.liabilities * 100) / 100,
                 netWorth: Math.round(w.net * 100) / 100 };
    });
chat1.addTool("bills_due",
    "The bills and deposits coming up (or overdue), the soonest first. Use it when asked what's due.",
    {},
    function () {
        return home.billsDue().map(function (d) {
            return { name: d.bill.Name, kind: d.bill.Kind, amount: d.bill.Amount, due: d.bill.NextDue, daysFromToday: d.days };
        });
    });

//// newButton.clicked
chat1.clear();
