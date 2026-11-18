// ---- the self test (tools/selftest.py puts this on the end of HomeForm's
// ---- (General) in a copy of the project; it isn't part of the app) ----
property var testSteps: []
property int testAt: 0
property var tx: ({})
Timer { id: testTimer; interval: 1500; running: true; onTriggered: testNext() }
function say(s) { console.log("T " + s); }
function el(f, name) { return f.element(name); }
function shot(f, name) {
    // (a picture of a window's items leaves out the window's own color: put it behind them)
    Qt.createQmlObject('import QtQuick; Rectangle { anchors.fill: parent; z: -1; color: "' + f.color + '" }', f.contentItem);
    f.contentItem.grabToImage(function (r) { r.saveToFile("shots/" + name + ".png"); });
}
function gridText(g, n) {
    return g.rows.slice(0, n || 99).map(function (r) {
        return g.columns.map(function (c) { return r[c]; }).join(" | ");
    }).join("\n    ");
}
function testNext() {
    if (testAt >= testSteps.length) {
        say("done");
        App.quit();
        return;
    }
    var wait = 450;
    try {
        var w = testSteps[testAt]();
        if (w)
            wait = w;
    } catch (e) {
        say("STEP " + testAt + " THREW " + e + " line " + e.lineNumber);
    }
    testAt += 1;
    testTimer.interval = wait;
    testTimer.restart();
}
Component.onCompleted: testSteps = [
    function () {
        say("categories " + Db.count("Categories") + " tax " + Db.count("Categories", { TaxRelated: true }));
        tx.checking = Db.insert("Accounts", { Name: "Checking", Type: "Checking", Institution: "First Bank", OpeningBalance: 1000 }).id;
        tx.savings = Db.insert("Accounts", { Name: "Savings", Type: "Savings", OpeningBalance: 5000 }).id;
        tx.visa = Db.insert("Accounts", { Name: "Visa", Type: "Credit Card", CreditLimit: 5000, InterestRate: 19.99 }).id;
        tx.loan = Db.insert("Accounts", { Name: "Car Loan", Type: "Loan", OpeningBalance: -12000, InterestRate: 6.5 }).id;
        tx.broker = Db.insert("Accounts", { Name: "Brokerage", Type: "Investment" }).id;
        say("bad rate " + JSON.stringify(Db.insert("Accounts", { Name: "X", InterestRate: 120 }).messages));
        var groceries = Db.select("Categories", { where: { Name: "Groceries" } })[0].id;
        tx.groceries = groceries;
        tx.dining = Db.select("Categories", { where: { Name: "Dining Out" } })[0].id;
        tx.rent = Db.select("Categories", { where: { Name: "Housing : Rent" } })[0].id;
        tx.salary = Db.select("Categories", { where: { Name: "Salary" } })[0].id;
        tx.grocer = Db.insert("Payees", { Name: "Corner Grocer", Category: groceries, Phone: "555-010-2000" }).id;
        tx.landlord = Db.insert("Payees", { Name: "Landlord", Category: tx.rent }).id;
        tx.employer = Db.insert("Payees", { Name: "Employer", Category: tx.salary }).id;
        say("accounts " + JSON.stringify([tx.checking, tx.savings, tx.visa, tx.loan, tx.broker]));
    },
    function () {
        var r = App.openForm("RegisterForm", { account: tx.checking });
        tx.reg = r;
        say("register title " + r.title + " rows " + el(r, "grid").rows.length + " toCombo " + el(r, "toCombo").visible);
        // a withdrawal, the payee's usual category filled in
        r.setField("Payee", tx.grocer);
        r.setField("Number", "101");
        el(r, "amountBox").text = "45.10";
        say("enter1 " + r.enter() + " amount " + r.record.Amount + " category " + r.record.Category + " id " + r.recordId);
        // a deposit
        r.startNew();
        el(r, "optDeposit").choose();
        r.setField("Payee", tx.employer);
        el(r, "amountBox").text = "$2,500.00";
        say("enter2 " + r.enter() + " amount " + r.record.Amount);
        // nothing typed
        r.startNew();
        say("enter3 " + r.enter());
    },
    function () {
        var r = tx.reg;
        el(r, "amountBox").text = "300";
        el(r, "optTransfer").choose();
        say("transfer shows to " + el(r, "toCombo").visible + " category " + el(r, "categoryCombo").visible);
        say("enter4 no account " + r.enter());
    },
    function () {
        var r = tx.reg;
        r.setField("TransferAccount", tx.checking);
        el(r, "amountBox").text = "300";
        say("enter5 same account " + r.enter() + " " + JSON.stringify(r.errors));
        r.setField("TransferAccount", tx.savings);
        say("enter6 " + r.enter() + " twin " + r.record.TransferId);
        tx.transfer = r.recordId;
        var twin = Db.get("Transactions", r.record.TransferId);
        say("twin " + twin.Account + " " + twin.Amount + " back " + twin.TransferId + " to " + twin.TransferAccount);
        // change it
        el(r, "amountBox").text = "350";
        say("enter7 " + r.enter() + " twin now " + Db.get("Transactions", r.record.TransferId).Amount + " count " + Db.count("Transactions"));
        say("register\n    " + gridText(el(r, "grid")));
        say("balance " + el(r, "balanceLabel").text);
    },
    function () {
        var r = tx.reg;
        r.startNew();
        r.setField("Payee", tx.grocer);
        el(r, "amountBox").text = "120";
        say("enter8 " + r.enter());
        tx.splitTx = r.recordId;
        tx.split = App.openForm("SplitForm", { transaction: r.recordId });
    },
    function () {
        var s = tx.split;
        say("split " + el(s, "totalLabel").text + " / " + el(s, "leftLabel").text + " box " + el(s, "amountBox").text);
        s.setField("Category", tx.groceries);
        el(s, "amountBox").text = "80";
        say("part1 " + s.saveLine());
        s.startNew();
        say("left box " + el(s, "amountBox").text);
        s.setField("Category", tx.dining);
        s.setField("Memo", "lunch");
        say("part2 " + s.saveLine() + " " + el(s, "leftLabel").text);
        say("split grid\n    " + gridText(el(s, "grid")));
        var t = Db.get("Transactions", tx.splitTx);
        say("is split " + t.IsSplit + " category " + t.Category);
        return 600;
    },
    function () {
        shot(tx.split, "split");
    },
    function () {
        tx.split.close();
        var r = tx.reg;
        el(r, "findBox").text = "grocer";
        say("find rows " + el(r, "grid").rows.length);
        el(r, "findBox").text = "";
        // a transaction to delete
        var id = Db.insert("Transactions", { Account: tx.checking, Amount: -9.99, Memo: "oops" }).id;
        say("zero refused " + JSON.stringify(Db.insert("Transactions", { Account: tx.checking, Amount: 0 }).messages));
        say("removed " + removeTransaction(id) + " count " + Db.count("Transactions"));
        shot(r, "register");
    },
    function () {
        // delete the transfer from the savings side: both go
        var twin = Db.get("Transactions", tx.transfer).TransferId;
        say("remove twin " + removeTransaction(twin) + " left " + Db.count("Transactions") + " original gone " + (Db.get("Transactions", tx.transfer).id === undefined));
        // bills
        tx.rentBill = Db.insert("Bills", { Name: "Rent", Account: tx.checking, Payee: tx.landlord, Category: tx.rent, Amount: 1200,
                                           Frequency: "Monthly", NextDue: addDays(today(), -1), AutoEnter: true }).id;
        tx.power = Db.insert("Bills", { Name: "Electric", Account: tx.checking, Amount: 80, Frequency: "Monthly", NextDue: addDays(today(), 3) }).id;
        Db.insert("Bills", { Name: "Far away", Account: tx.checking, Amount: 10, Frequency: "Yearly", NextDue: addDays(today(), 200) });
        tx.save = Db.insert("Bills", { Name: "To savings", Kind: "Transfer", Account: tx.checking, ToAccount: tx.savings, Amount: 100,
                                       Frequency: "Every two weeks", NextDue: today() }).id;
        say("bad bill " + JSON.stringify(Db.insert("Bills", { Name: "B", Account: tx.checking, Amount: -5, NextDue: today(), EndDate: addDays(today(), -9) }).messages));
        say("next " + [nextDue("2026-01-31", "Monthly"), nextDue("2026-12-15", "Monthly"), nextDue("2024-02-29", "Yearly"), nextDue("2026-03-01", "Weekly"), nextDue("2026-03-01", "Once")].join(" "));
        everyMinute();
        var b = Db.get("Bills", tx.rentBill);
        say("rent next " + b.NextDue + " status " + statusLabel.text);
        say("enter transfer bill " + enterBill(tx.save) + " next " + Db.get("Bills", tx.save).NextDue);
    },
    function () {
        say("bills\n    " + billList.items.join("\n    ") + "\n    frame " + billsFrame.text);
        var f = App.openForm("BillsForm");
        f.loadRecord(tx.power);
        say("bills form " + el(f, "dueLabel").text + " to visible " + el(f, "toAccountCombo").visible);
        el(f, "skipButton").clicked();
        say("skipped " + f.record.NextDue + " " + el(f, "dueLabel").text);
        tx.billsForm = f;
        Db.insert("Budgets", { Category: tx.groceries, Amount: 400 });
        Db.insert("Budgets", { Category: tx.dining, Amount: 30 });
        Db.insert("Budgets", { Category: tx.rent, Amount: 1200 });
        say("dup budget " + JSON.stringify(Db.insert("Budgets", { Category: tx.rent, Amount: 1 }).messages));
        tx.budget = App.openForm("BudgetForm");
    },
    function () {
        shot(tx.billsForm, "bills");
        var f = tx.budget;
        say("budget\n    " + gridText(el(f, "grid")) + "\n    " + el(f, "totalLabel").text + " title " + el(f, "graph1").title);
        shot(f, "budget");
        tx.rec = App.openForm("ReconcileForm", { account: tx.checking });
    },
    function () {
        tx.billsForm.close();
        tx.budget.close();
        var f = tx.rec;
        say("reconcile " + f.title + " rows " + el(f, "grid").rows.length + " cleared " + el(f, "clearedLabel").text + " diff " + el(f, "differenceLabel").text);
        el(f, "endingBox").text = "-200.00";
        f.fill();
        say("ending typed: diff " + el(f, "differenceLabel").text + " finish " + el(f, "finishButton").enabled);
        el(f, "grid").select(0, true);
        say("after first " + el(f, "clearedLabel").text + " diff " + el(f, "differenceLabel").text + " finish " + el(f, "finishButton").enabled);
        shot(f, "reconcile");
    },
    function () {
        var f = tx.rec;
        // (the message box would wait for OK: do what Finish does up to it)
        Db.select("Transactions", { where: { Account: tx.checking, Cleared: "Cleared" } }).forEach(function (t) { Db.update("Transactions", t.id, { Cleared: "Reconciled" }); });
        f.fill();
        say("reconciled rows left " + el(f, "grid").rows.length + " R count " + Db.count("Transactions", { Cleared: "Reconciled" }));
        f.close();
        // a trade by hand
        var t = App.openForm("TradeForm", { account: tx.broker });
        tx.trade = t;
        Db.insert("Transactions", { Account: tx.broker, Amount: 1000, Memo: "Money in" });
        tx.acme = findSecurity("acme", "Acme Corp", "Stock");
        say("bad symbol " + JSON.stringify(Db.insert("Securities", { Symbol: "ab c!" }).messages));
        t.setField("Security", tx.acme);
        el(t, "quantityBox").text = "10";
        el(t, "priceBox").text = "20";
        el(t, "commissionBox").text = "4.95";
        t.showTotal();
        say("trade total " + el(t, "totalLabel").text + " amountBox " + el(t, "amountBox").visible);
        return 700;
    },
    function () {
        shot(tx.trade, "trade");
    },
    function () {
        var t = tx.trade;
        // (Enter ends in a message box: the same steps, without it)
        t.setField("Amount", Math.round(t.cashFor() * 100) / 100);
        t.setField("Cleared", "Cleared");
        say("trade save " + t.save() + " " + JSON.stringify(t.errors));
        rebuildHoldings(tx.broker);
        Db.insert("Transactions", { Account: tx.broker, Date: today(), Amount: -60.5, Action: "Buy", Security: tx.acme, Quantity: 2, Price: 30.25 });
        Db.insert("Transactions", { Account: tx.broker, Date: today(), Amount: 12, Action: "Dividend", Security: tx.acme });
        Db.insert("Transactions", { Account: tx.broker, Date: today(), Amount: 75, Action: "Sell", Security: tx.acme, Quantity: 3, Price: 25 });
        say("security without action " + JSON.stringify(Db.insert("Transactions", { Account: tx.broker, Amount: 5, Security: tx.acme }).messages));
        rebuildHoldings(tx.broker);
        say("holdings " + JSON.stringify(Db.select("Holdings").map(function (h) { return [h.Account, h.Security, h.Quantity, h.CostBasis, h.MarketValue]; })));
        t.close();
        tx.sec = App.openForm("SecuritiesForm", { security: tx.acme });
    },
    function () {
        var s = tx.sec;
        say("security " + s.record.Symbol + " / " + el(s, "statusLabel").text);
        s.setField("LastPrice", "26.5");
        say("price save " + s.save() + " prices " + Db.count("Prices") + " holding value " + Db.select("Holdings")[0].MarketValue);
        say("after price " + el(s, "statusLabel").text + " chart " + el(s, "chart").labels.join(","));
        tx.port = App.openForm("PortfolioForm");
    },
    function () {
        var p = tx.port;
        say("portfolio\n    " + gridText(el(p, "grid")) + "\n    " + el(p, "totalLabel").text);
        say("home\n    " + gridText(accountGrid) + "\n    assets " + assetsLabel.text + " owed " + owedLabel.text + " worth " + worthLabel.text);
        // ---- Schwab (the mock) ----
        setSetting("SchwabBase", "MOCKURL");
        var f = App.openForm("SchwabForm");
        tx.schwab = f;
        say("schwab state " + el(f, "stateLabel").text + " sync enabled " + el(f, "syncButton").enabled);
        f.syncAll();
    },
    function () {
        var f = tx.schwab;
        say("schwab log\n    " + el(f, "logList").items.map(function (l) { return l.substring(10); }).join("\n    "));
        el(f, "logList").items = [];
        el(f, "keyBox").text = "KEY";
        el(f, "secretBox").text = "WRONG";
        el(f, "pasteBox").text = "https://127.0.0.1/?code=C0.abc%40&session=xyz";
        f.finishSignIn();
        return 1200;
    },
    function () {
        var f = tx.schwab;
        say("schwab log\n    " + el(f, "logList").items.map(function (l) { return l.substring(10); }).join("\n    "));
        el(f, "logList").items = [];
        el(f, "secretBox").text = "SECRET";
        el(f, "pasteBox").text = "https://127.0.0.1/?code=C0.abc%40&session=xyz";
        f.finishSignIn();
        return 6000;
    },
    function () {
        var f = tx.schwab;
        say("schwab log\n    " + el(f, "logList").items.map(function (l) { return l.substring(10); }).join("\n    "));
        say("schwab state " + el(f, "stateLabel").text.substring(0, 15) + " busy " + f.busy);
        var accounts = Db.select("Accounts", { where: { Institution: "Charles Schwab" } });
        say("schwab accounts " + JSON.stringify(accounts.map(function (a) { return [a.Name, a.AccountNumber, a.SchwabHash, a.OpeningBalance, a.LastSync !== null]; })));
        tx.s1 = accounts[0].id;
        say("schwab tx\n    " + Db.select("Transactions", { where: { Source: "Schwab" }, orderBy: "Date" }).map(function (t) {
            return [t.Account, t.Date, t.Amount, t.Action, t.Security, t.Quantity, t.Price, t.Commission, t.Category, t.SchwabId, t.Memo].join(" | "); }).join("\n    "));
        say("securities " + JSON.stringify(Db.select("Securities").map(function (s) { return [s.Symbol, s.Company, s.Type, s.LastPrice, s.Change, s.ChangePercent]; })));
        say("holdings " + JSON.stringify(Db.select("Holdings").map(function (h) { return [h.Account, h.Security, h.Quantity, h.CostBasis, h.MarketValue]; })));
        var b = balances();
        say("schwab values " + JSON.stringify(accounts.map(function (a) { return [a.Name, fmt(b[a.id].cash), fmt(b[a.id].holdings), fmt(b[a.id].value)]; })));
        // again: nothing twice; and with the access token run out: the refresh token
        el(f, "logList").items = [];
        setSetting("SchwabAccessUntil", 0);
        f.syncAll();
        return 6000;
    },
    function () {
        var f = tx.schwab;
        say("schwab log 2\n    " + el(f, "logList").items.map(function (l) { return l.substring(10); }).join("\n    "));
        say("schwab tx count " + Db.count("Transactions", { Source: "Schwab" }) + " access " + setting("SchwabAccess") + " last " + (setting("SchwabLastSync") !== ""));
        shot(f, "schwab");
        say("sync label " + syncLabel.text.substring(0, 18));
        // rename one, and its register
        Db.update("Accounts", tx.s1, { Name: "Schwab Brokerage" });
        tx.reg.showAccount(tx.s1);
        var aapl = Db.select("Securities", { where: { Symbol: "AAPL" } })[0].id;
        tx.aapl = aapl;
        tx.sec.loadRecord(aapl);
        tx.sec.download();
        return 5000;
    },
    function () {
        var s = tx.sec, r = tx.reg;
        say("schwab register " + r.title + "\n    " + gridText(el(r, "grid")) + "\n    " + el(r, "balanceLabel").text);
        shot(r, "register-schwab");
        say("prices " + Db.count("Prices", { Security: tx.aapl }) + " status " + el(s, "statusLabel").text + " chart bars " + el(s, "chart").labels.length
          + " last " + s.record.LastPrice + " studies " + el(s, "chart").studies.join(","));
        el(s, "averageCheck").toggle();
        el(s, "bandsCheck").toggle();
        el(s, "rsiCheck").toggle();
        el(s, "rangeCombo").choose(1);
        say("3 months bars " + el(s, "chart").labels.length + " overlays " + el(s, "chart").overlays.join(",") + " studies " + el(s, "chart").studies.join(","));
        s.download();
        return 3000;
    },
    function () {
        var s = tx.sec;
        say("prices again " + Db.count("Prices", { Security: tx.aapl }) + " " + el(s, "statusLabel").text);
        shot(s, "securities");
        shot(tx.port, "portfolio");
        say("portfolio\n    " + gridText(el(tx.port, "grid")) + "\n    " + el(tx.port, "totalLabel").text);
        el(tx.port, "pricesButton").clicked();
        return 2500;
    },
    function () {
        say("prices button " + el(tx.port, "statusLabel").text.substring(0, 17));
        tx.port.close();
        tx.sec.close();
        tx.schwab.close();
        tx.reg.close();
        // some history for the reports
        var d = today();
        for (var m = 1; m <= 3; ++m) {
            var day = addMonths(d, -m);
            Db.insert("Transactions", { Account: tx.checking, Date: day, Payee: tx.employer, Category: tx.salary, Amount: 2500 });
            Db.insert("Transactions", { Account: tx.checking, Date: day, Payee: tx.landlord, Category: tx.rent, Amount: -1200 });
            Db.insert("Transactions", { Account: tx.visa, Date: day, Payee: tx.grocer, Category: tx.groceries, Amount: -100 * m });
        }
        Db.insert("WorthHistory", { Day: addDays(d, -30), Assets: 5000, Liabilities: 12000, Worth: -7000 });
        Db.insert("WorthHistory", { Day: addDays(d, -15), Assets: 7000, Liabilities: 12000, Worth: -5000 });
        tx.rep = App.openForm("ReportsForm");
    },
    function () {
        var f = tx.rep, list = el(f, "reportList");
        say("period " + el(f, "fromPicker").value + " to " + el(f, "toPicker").value);
        el(f, "periodCombo").choose(4);
        for (var i = 0; i < list.items.length; ++i) {
            list.choose(i);
            say("report " + list.text + " [" + el(f, "graph1").graphType + "] " + el(f, "totalLabel").text + "\n    " + gridText(el(f, "grid"), 6));
        }
        list.choose(2);
        shot(f, "reports-months");
    },
    function () {
        var f = tx.rep;
        el(f, "reportList").choose(0);
        el(f, "periodCombo").choose(5);
        say("all dates " + JSON.stringify([el(f, "fromPicker").value, el(f, "toPicker").value]) + " " + el(f, "totalLabel").text);
        tx.find = App.openForm("FindForm", { text: "Groceries", from: addMonths(today(), -12), to: today() });
    },
    function () {
        shot(tx.rep, "reports");
        var f = tx.find;
        say("find " + el(f, "countLabel").text + "\n    " + gridText(el(f, "grid"), 4));
        el(f, "findBox").text = "";
        el(f, "leastBox").text = "1,000";
        el(f, "accountCombo").choose(1);
        f.find();
        say("find big in " + el(f, "accountCombo").text + ": " + el(f, "countLabel").text);
        shot(f, "find");
        tx.plan = App.openForm("PlannerForm");
    },
    function () {
        tx.rep.close();
        tx.find.close();
        var f = tx.plan;
        say("planner debts " + el(f, "accountCombo").items.join(",") + " / " + el(f, "resultLabel").text);
        el(f, "accountCombo").choose(0);
        el(f, "yearsBox").text = "5";
        f.work();
        say("planner " + el(f, "amountBox").text + " at " + el(f, "rateBox").text + ": " + el(f, "resultLabel").text + " rows " + el(f, "grid").rows.length);
        el(f, "extraBox").text = "100";
        f.work();
        say("planner extra: " + el(f, "resultLabel").text + " rows " + el(f, "grid").rows.length + "\n    " + gridText(el(f, "grid"), 2));
        return 700;
    },
    function () {
        var f = tx.plan;
        shot(f, "planner");
        tx.acc = App.openForm("AccountsForm");
        tx.acc.loadRecord(tx.checking);
        tx.adv = App.openForm("AdvisorForm");
        tx.about = App.openForm("AboutForm");
        tx.pay = App.openForm("PayeesForm");
        tx.cat = App.openForm("CategoriesForm");
    },
    function () {
        tx.plan.close();
        say("accounts form " + el(tx.acc, "balanceLabel").text + " grid " + el(tx.acc, "grid").rows.length);
        say("advisor key " + el(tx.adv, "chat1").hasKey + " about " + el(tx.about, "versionLabel").text);
        shot(tx.acc, "accounts");
        shot(tx.adv, "advisor");
        shot(tx.about, "about");
        shot(tx.pay, "payees");
        shot(tx.cat, "categories");
    },
    function () {
        [tx.acc, tx.adv, tx.about, tx.pay, tx.cat].forEach(function (f) { f.close(); });
        refreshHome();
        say("home\n    " + gridText(accountGrid) + "\n    assets " + assetsLabel.text + " owed " + owedLabel.text + " worth " + worthLabel.text);
        say("spending " + JSON.stringify(spendingGraph.labels) + " " + JSON.stringify(spendingGraph.series[0].values) + " worth history " + Db.count("WorthHistory") + " graph " + worthGraph.labels.length);
        say("open forms closed: " + ["RegisterForm", "SchwabForm", "ReportsForm"].map(function (n) { return App.form(n) === null; }).join(","));
        return 900;
    },
    function () { shot(form, "home"); return 900; }
]
