//// (General) core
// ---- Wayland Money's library -------------------------------------------
// Every other form reaches these through App.form("HomeForm").

// money as text: 1,234.50 (blank for nothing)
function fmt(n) {
    if (n === null || n === undefined || n === "" || isNaN(n))
        return "";
    var v = Math.round(Number(n) * 100) / 100;
    return (v === 0 ? 0 : v).toLocaleString(Qt.locale("en_US"), "f", 2);
}
// what was typed, as a number: "1,234.50", "$12", "(5.00)"; null if it isn't one
function parseMoney(text) {
    var s = String(text === null || text === undefined ? "" : text).replace(/[$,\s]/g, "");
    var negative = /^\(.*\)$/.test(s);
    if (negative)
        s = s.substring(1, s.length - 1);
    if (s === "" || isNaN(Number(s)))
        return null;
    return Math.round((negative ? -Number(s) : Number(s)) * 100) / 100;
}

// days: kept as 2026-11-18, shown as 11/18/2026
function today() { return AppDates.stored("today", "Date"); }
function rightNow() { return AppDates.stored("now", "Date and Time"); }
function showDate(day) { return day ? AppDates.display(String(day).substring(0, 10), "Date", "MM/dd/yyyy", "") : ""; }
function two(n) { return (n < 10 ? "0" : "") + n; }
function iso(d) { return d.getFullYear() + "-" + two(d.getMonth() + 1) + "-" + two(d.getDate()); }
function toDate(day) {
    var p = String(day).substring(0, 10).split("-");
    return new Date(Number(p[0]), Number(p[1]) - 1, Number(p[2]));
}
function addDays(day, n) {
    var d = toDate(day);
    d.setDate(d.getDate() + n);
    return iso(d);
}
function addMonths(day, n) {
    var d = toDate(day), wanted = d.getDate();
    d.setDate(1);
    d.setMonth(d.getMonth() + n);
    var last = new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate();
    d.setDate(Math.min(wanted, last));
    return iso(d);
}
function monthStart(day) { return String(day).substring(0, 8) + "01"; }
function daysBetween(from, to) { return Math.round((toDate(to) - toDate(from)) / 86400000); }

// a setting, kept in the Settings table
function setting(name, fallback) {
    var rows = Db.select("Settings", { where: { Name: name } });
    if (rows.length > 0 && rows[0].Value !== null && rows[0].Value !== "")
        return rows[0].Value;
    return fallback === undefined ? "" : fallback;
}
function setSetting(name, value) {
    var rows = Db.select("Settings", { where: { Name: name } });
    if (rows.length > 0)
        Db.update("Settings", rows[0].id, { Value: String(value) });
    else
        Db.insert("Settings", { Name: name, Value: String(value) });
}

// a table's records' names, by id: names("Payees")[3]
function names(table) {
    var out = {};
    Db.lookupChoices(table).forEach(function (c) { out[c.id] = c.label; });
    return out;
}

// the kinds of account
function isDebt(type) { return type === "Credit Card" || type === "Loan" || type === "Liability"; }
function isInvestment(type) { return type === "Investment" || type === "Retirement"; }

// every account's money: { id: { cash, holdings, value } }
function balances() {
    var out = {};
    Db.select("Accounts").forEach(function (a) {
        out[a.id] = { cash: a.OpeningBalance || 0, holdings: 0, value: 0 };
    });
    Db.select("Transactions").forEach(function (t) {
        if (out[t.Account])
            out[t.Account].cash += t.Amount || 0;
    });
    Db.select("Holdings").forEach(function (h) {
        if (out[h.Account])
            out[h.Account].holdings += h.MarketValue || 0;
    });
    Object.keys(out).forEach(function (k) { out[k].value = out[k].cash + out[k].holdings; });
    return out;
}
function netWorth() {
    var b = balances(), assets = 0, owed = 0;
    Db.select("Accounts").forEach(function (a) {
        if (a.Closed)
            return;
        if (isDebt(a.Type))
            owed -= b[a.id].value;
        else
            assets += b[a.id].value;
    });
    return { assets: assets, liabilities: owed, net: assets - owed };
}
// today's line of the net worth history (the graph at the bottom reads it)
function snapshot() {
    var w = netWorth(), rows = Db.select("WorthHistory", { where: { Day: today() } });
    var line = { Day: today(), Assets: w.assets, Liabilities: w.liabilities, Worth: w.net };
    if (rows.length === 0)
        Db.insert("WorthHistory", line);
    else if (fmt(rows[0].Worth) !== fmt(w.net) || fmt(rows[0].Assets) !== fmt(w.assets))
        Db.update("WorthHistory", rows[0].id, line);
}

// income or spending? (a transfer or a trade only moves money)
function isFlow(t) {
    return !t.TransferAccount && ["Buy", "Sell", "Reinvest", "Add Shares", "Remove Shares"].indexOf(t.Action) < 0;
}

// what came in and went out between two days, by category:
// [{ category, name, kind, total }], the biggest spending first.
function totalsByCategory(from, to) {
    var cats = {}, sums = {}, splits = {};
    Db.select("Categories").forEach(function (c) { cats[c.id] = c; });
    Db.select("Splits").forEach(function (s) {
        if (!splits[s.Transaction])
            splits[s.Transaction] = [];
        splits[s.Transaction].push(s);
    });
    function add(category, amount) {
        var k = category || 0;
        sums[k] = (sums[k] || 0) + (amount || 0);
    }
    Db.select("Transactions").forEach(function (t) {
        if (t.Date < from || t.Date > to || !isFlow(t))
            return;
        if (t.IsSplit && splits[t.id])
            splits[t.id].forEach(function (s) { add(s.Category, s.Amount); });
        else
            add(t.Category, t.Amount);
    });
    return Object.keys(sums).map(function (k) {
        var c = cats[k];
        return { category: c ? c.id : null, name: c ? c.Name : "(no category)",
                 kind: c ? c.Kind : (sums[k] < 0 ? "Expense" : "Income"), total: sums[k] };
    }).sort(function (a, b) { return a.total - b.total; });
}

// ---- the Home page itself ----------------------------------------------

property var billIds: []

// (a change anywhere: the page is read again, a moment later, once)
Timer { id: soon; interval: 300; onTriggered: refreshHome() }
Connections {
    target: Db
    function onChanged(table) {
        if (table !== "WorthHistory" && table !== "Settings" && table !== "Prices")
            soon.restart();
    }
}

function refreshHome() {
    var b = balances(), rows = [];
    Db.select("Accounts", { orderBy: "Name" }).forEach(function (a) {
        if (!a.Closed)
            rows.push({ id: a.id, Account: a.Name, Type: a.Type, Balance: fmt(b[a.id].value) });
    });
    accountGrid.rows = rows;
    var w = netWorth();
    assetsLabel.text = fmt(w.assets);
    owedLabel.text = fmt(w.liabilities);
    worthLabel.text = fmt(w.net);
    snapshot();

    var spent = totalsByCategory(monthStart(today()), today()).filter(function (c) { return c.total < 0; });
    spendingGraph.setData(spent.map(function (c) { return c.name; }),
                          [{ name: "Spent", values: spent.map(function (c) { return Math.round(-c.total * 100) / 100; }) }]);
    showBills();
    var last = setting("SchwabLastSync");
    syncLabel.text = last === "" ? "Never downloaded."
                   : "Last: " + AppDates.display(last, "Date and Time", "MM/dd/yyyy", "h:mm AP");
}

function openRegister() {
    if (accountGrid.currentId === null)
        form.msgBox("Click an account in the list first.", "Register");
    else
        App.openForm("RegisterForm", { account: accountGrid.currentId });
}

// the categories a new file starts with
function seed() {
    if (Db.count("Categories") > 0)
        return;
    var income = ["Salary", "Interest Income", "Investment Income : Dividends", "Investment Income : Interest",
                  "Other Income"];
    var expense = ["Automobile : Fuel", "Automobile : Maintenance", "Bank Charges", "Clothing", "Dining Out",
                   "Education", "Entertainment", "Gifts", "Groceries", "Healthcare", "Housing : Rent",
                   "Housing : Mortgage Interest", "Insurance", "Investment Expense : Fees", "Taxes : Federal",
                   "Taxes : State", "Travel", "Utilities : Electric", "Utilities : Internet", "Utilities : Water"];
    var taxed = ["Salary", "Interest Income", "Investment Income : Dividends", "Investment Income : Interest",
                 "Housing : Mortgage Interest", "Taxes : Federal", "Taxes : State"];
    income.forEach(function (n) { Db.insert("Categories", { Name: n, Kind: "Income", TaxRelated: taxed.indexOf(n) >= 0 }); });
    expense.forEach(function (n) { Db.insert("Categories", { Name: n, Kind: "Expense", TaxRelated: taxed.indexOf(n) >= 0 }); });
}

//// (General) core-nobills
// (until the Bills chapter: nothing to show yet)
function showBills() { }

//// (General) register
// ---- transactions ------------------------------------------------------

// keep a transfer's other half in step with it (or take it away)
function matchTransfer(id) {
    var t = Db.get("Transactions", id);
    if (t.id === undefined)
        return;
    var twin = t.TransferId ? Db.get("Transactions", t.TransferId) : ({});
    if (!t.TransferAccount) {
        if (twin.id !== undefined)
            Db.remove("Transactions", twin.id);
        if (t.TransferId)
            Db.update("Transactions", t.id, { TransferId: null });
        return;
    }
    var other = { Account: t.TransferAccount, Date: t.Date, Number: t.Number, Payee: t.Payee, Memo: t.Memo,
                  Amount: -t.Amount, TransferAccount: t.Account, TransferId: t.id, Source: t.Source };
    if (twin.id !== undefined) {
        Db.update("Transactions", twin.id, other);
    } else {
        var r = Db.insert("Transactions", other);
        if (r.ok)
            Db.update("Transactions", t.id, { TransferId: r.id });
    }
}

// delete a transaction: its splits and a transfer's other half go with it
function removeTransaction(id) {
    var t = Db.get("Transactions", id);
    if (t.id === undefined)
        return true;
    Db.select("Splits", { where: { Transaction: id } }).forEach(function (s) { Db.remove("Splits", s.id); });
    if (t.TransferId && Db.get("Transactions", t.TransferId).id !== undefined)
        Db.remove("Transactions", t.TransferId);
    var r = Db.remove("Transactions", id);
    if (r.ok && t.Security)
        rebuildHoldings(t.Account);
    return r.ok;
}

//// (General) register-noholdings
// (until the Investing chapter: no holdings to work out yet)
function rebuildHoldings(accountId) { }

//// (General) bills
// ---- bills and deposits --------------------------------------------------

function nextDue(day, frequency) {
    switch (frequency) {
    case "Weekly": return addDays(day, 7);
    case "Every two weeks": return addDays(day, 14);
    case "Twice a month": return addDays(day, 15);
    case "Monthly": return addMonths(day, 1);
    case "Every three months": return addMonths(day, 3);
    case "Every six months": return addMonths(day, 6);
    case "Yearly": return addMonths(day, 12);
    }
    return "";      // Once
}

// the bills inside their own reminder days, the soonest first: [{ bill, days }]
function billsDue() {
    var out = [];
    Db.select("Bills", { where: { Active: true }, orderBy: "NextDue" }).forEach(function (b) {
        var days = daysBetween(today(), b.NextDue);
        if (days <= (b.RemindDays === null ? 7 : b.RemindDays))
            out.push({ bill: b, days: days });
    });
    return out;
}

// move a bill on to its next day (after it's paid, or to skip one)
function skipBill(id) {
    var b = Db.get("Bills", id), next = nextDue(b.NextDue, b.Frequency);
    if (next === "" || (b.EndDate && next > b.EndDate))
        Db.update("Bills", id, { Active: false });
    else
        Db.update("Bills", id, { NextDue: next });
}

// put a bill in its account's register, then move it on
function enterBill(id) {
    var b = Db.get("Bills", id);
    if (b.id === undefined)
        return false;
    var transfer = b.Kind === "Transfer";
    var r = Db.insert("Transactions", {
        Account: b.Account, Date: b.NextDue, Payee: b.Payee, Category: transfer ? null : b.Category,
        Memo: b.Memo || b.Name, Amount: (b.Kind === "Deposit" ? 1 : -1) * b.Amount,
        TransferAccount: transfer ? b.ToAccount : null, Source: "Bill" });
    if (!r.ok) {
        form.msgBox(b.Name + " wasn't entered:\n" + r.messages.join("\n"), "Bills", "warning");
        return false;
    }
    matchTransfer(r.id);
    skipBill(id);
    return true;
}

function showBills() {
    var due = billsDue();
    billIds = due.map(function (d) { return d.bill.id; });
    billList.items = due.map(function (d) {
        var when = d.days < 0 ? "OVERDUE " + showDate(d.bill.NextDue) : d.days === 0 ? "Today" : showDate(d.bill.NextDue);
        return when + "   " + d.bill.Name + "   " + fmt(d.bill.Amount);
    });
    billsFrame.text = due.length === 0 ? "Bills coming up: none" : "Bills coming up: " + due.length;
}

// once a minute, and when the program starts: the bills that enter themselves
Timer { id: startup; interval: 600; onTriggered: everyMinute() }
Timer { id: minute; interval: 60000; repeat: true; running: true; onTriggered: everyMinute() }
function everyMinute() {
    var entered = 0;
    Db.select("Bills", { where: { Active: true, AutoEnter: true } }).forEach(function (b) {
        // (a bill that fell behind catches up, a payment at a time)
        for (var i = 0; i < 24 && b.Active && b.NextDue <= today(); ++i) {
            if (!enterBill(b.id))
                break;
            entered += 1;
            b = Db.get("Bills", b.id);
        }
    });
    if (entered > 0)
        statusLabel.text = entered + (entered === 1 ? " bill was" : " bills were") + " entered by itself.";
}

//// (General) investing
// ---- investments ---------------------------------------------------------

// the security with this symbol (made, if it's new): its id
function findSecurity(symbol, company, type) {
    var s = String(symbol).trim().toUpperCase();
    var rows = Db.select("Securities", { where: { Symbol: s } });
    if (rows.length > 0)
        return rows[0].id;
    var r = Db.insert("Securities", { Symbol: s, Company: company || "", Type: type || "Stock" });
    return r.ok ? r.id : null;
}

// every holding's market value, from its security's last price
function revalue() {
    var secs = {};
    Db.select("Securities").forEach(function (s) { secs[s.id] = s; });
    Db.select("Holdings").forEach(function (h) {
        var s = secs[h.Security];
        if (!s || s.LastPrice === null)
            return;
        var v = Math.round(h.Quantity * s.LastPrice * (s.Type === "Option" ? 100 : 1) * 100) / 100;
        if (v !== h.MarketValue)
            Db.update("Holdings", h.id, { MarketValue: v });
    });
}

// an account's holdings, worked out from its trades (average cost).
// A Schwab account's holdings are Schwab's own positions instead.
function rebuildHoldings(accountId) {
    var account = Db.get("Accounts", accountId);
    if (account.id === undefined || account.SchwabHash)
        return;
    var have = {};
    Db.select("Transactions", { where: { Account: accountId }, orderBy: "Date" }).forEach(function (t) {
        if (!t.Security || !t.Action)
            return;
        var h = have[t.Security] || (have[t.Security] = { shares: 0, cost: 0 });
        var q = t.Quantity || 0;
        if (t.Action === "Buy" || t.Action === "Reinvest" || t.Action === "Add Shares") {
            h.shares += q;
            h.cost += q * (t.Price || 0) + (t.Commission || 0);
        } else if (t.Action === "Sell" || t.Action === "Remove Shares") {
            h.cost -= h.shares > 0 ? h.cost * Math.min(1, q / h.shares) : 0;
            h.shares -= q;
        }
    });
    Db.select("Holdings", { where: { Account: accountId } }).forEach(function (h) { Db.remove("Holdings", h.id); });
    Object.keys(have).forEach(function (k) {
        if (Math.abs(have[k].shares) > 0.00005)
            Db.insert("Holdings", { Account: accountId, Security: Number(k), Quantity: have[k].shares,
                                    CostBasis: Math.round(have[k].cost * 100) / 100, AsOf: rightNow() });
    });
    revalue();
}

// a security's price for a day, into its price history
function keepPrice(securityId, day, bar) {
    var rows = Db.select("Prices", { where: { Security: securityId, Day: day } });
    var line = { Security: securityId, Day: day, Open: bar.open, High: bar.high, Low: bar.low,
                 Close: bar.close, Volume: bar.volume === undefined ? null : Math.round(bar.volume) };
    if (rows.length > 0)
        Db.update("Prices", rows[0].id, line);
    else
        Db.insert("Prices", line);
}

//// (General) schwab
// ---- Charles Schwab ------------------------------------------------------
// Requests don't wait: each takes a function to call with the answer.

function schwabBase() { return setting("SchwabBase", "https://api.schwabapi.com"); }

// one request: done(ok, data, status)
function httpRequest(method, url, headers, body, done) {
    var xhr = new XMLHttpRequest();
    xhr.onreadystatechange = function () {
        if (xhr.readyState !== XMLHttpRequest.DONE)
            return;
        var data = xhr.responseText;
        try { data = JSON.parse(xhr.responseText); } catch (e) { }
        done(xhr.status >= 200 && xhr.status < 300, data, xhr.status);
    };
    xhr.open(method, url);
    Object.keys(headers).forEach(function (h) { xhr.setRequestHeader(h, headers[h]); });
    if (body)
        xhr.send(body);
    else
        xhr.send();
}

// what went wrong, in words
function schwabError(data, status) {
    if (status === 0)
        return "No answer from Schwab. Is the internet there?";
    if (data && typeof data === "object") {
        if (data.error_description || data.message || data.error)
            return String(data.error_description || data.message || data.error);
        if (data.errors && data.errors.length > 0)
            return String(data.errors[0].detail || data.errors[0].title || data.errors[0].message || "Schwab said " + status);
    }
    return "Schwab said " + status + ".";
}

// ask Schwab's token address for tokens, and keep them: done(ok, why)
function schwabTokens(formBody, done) {
    var basic = Qt.btoa(setting("SchwabKey") + ":" + setting("SchwabSecret"));
    httpRequest("POST", schwabBase() + "/v1/oauth/token",
                { "Authorization": "Basic " + basic, "Content-Type": "application/x-www-form-urlencoded" },
                formBody, function (ok, data, status) {
        if (!ok || !data || !data.access_token) {
            done(false, schwabError(data, status));
            return;
        }
        setSetting("SchwabAccess", data.access_token);
        // (a minute early, so one never runs out on the way)
        setSetting("SchwabAccessUntil", Date.now() + ((data.expires_in || 1800) - 60) * 1000);
        if (data.refresh_token)
            setSetting("SchwabRefresh", data.refresh_token);
        done(true, "");
    });
}

// a token Schwab takes: the one kept, or a new one from the refresh token
function schwabToken(done) {
    if (setting("SchwabAccess") !== "" && Date.now() < Number(setting("SchwabAccessUntil", 0))) {
        done(true, setting("SchwabAccess"));
        return;
    }
    if (setting("SchwabRefresh") === "") {
        done(false, "Not signed in to Schwab yet: Online > Schwab Setup.");
        return;
    }
    schwabTokens("grant_type=refresh_token&refresh_token=" + encodeURIComponent(setting("SchwabRefresh")),
                 function (ok, why) {
        if (ok) {
            done(true, setting("SchwabAccess"));
        } else {
            setSetting("SchwabRefresh", "");
            done(false, "Schwab wants a new sign-in (" + why + "): Online > Schwab Setup.");
        }
    });
}

// GET something from Schwab, signed in: done(ok, data or why)
function schwabGet(path, done) {
    schwabToken(function (ok, token) {
        if (!ok) {
            done(false, token);
            return;
        }
        httpRequest("GET", schwabBase() + path, { "Authorization": "Bearer " + token, "Accept": "application/json" },
                    null, function (ok2, data, status) {
            done(ok2, ok2 ? data : schwabError(data, status));
        });
    });
}

// the latest prices of these securities (rows of Securities): done(ok, how many or why)
function updateQuotes(securities, done) {
    var list = securities.filter(function (s) { return s.Type !== "Option" && s.Type !== "Bond"; });
    if (list.length === 0) {
        done(true, 0);
        return;
    }
    var symbols = list.map(function (s) { return s.Symbol; }).join(",");
    schwabGet("/marketdata/v1/quotes?fields=quote,reference&symbols=" + encodeURIComponent(symbols), function (ok, data) {
        if (!ok) {
            done(false, data);
            return;
        }
        var got = 0;
        list.forEach(function (s) {
            var q = data[s.Symbol] ? data[s.Symbol].quote : null;
            if (!q)
                return;
            var price = q.lastPrice || q.mark || q.nAV || q.closePrice;
            if (!price)
                return;
            var change = { LastPrice: price, Change: q.netChange === undefined ? null : q.netChange,
                           ChangePercent: q.netPercentChange === undefined ? null : q.netPercentChange,
                           PriceTime: rightNow() };
            var ref = data[s.Symbol].reference;
            if (!s.Company && ref && ref.description)
                change.Company = ref.description;
            Db.update("Securities", s.id, change);
            got += 1;
        });
        revalue();
        done(true, got);
    });
}

//// (General) schwab-start
// when the program starts: a download, if Schwab Setup says so
Timer {
    interval: 1500
    running: true
    onTriggered: {
        if (setting("SchwabAuto") === "yes" && setting("SchwabRefresh") !== "")
            App.showForm("SchwabForm").syncAll();
    }
}

//// load
seed();
refreshHome();
statusLabel.text = "Welcome to Wayland Money.";

//// load bills
seed();
refreshHome();
statusLabel.text = "Welcome to Wayland Money.";
startup.start();

//// unload
App.quit();

//// accountGrid.rowActivated
openRegister();

//// billList.doubleClicked
App.showForm("BillsForm");

//// spendingGraph.clicked
App.openForm("ReportsForm", { report: "Spending by Category" });

//// syncButton.clicked
App.showForm("SchwabForm").syncAll();

//// menu1.itemClicked
if (menu1.item === "accountsRegister")
    openRegister();
else if (menu1.item === "accountsBalance" && accountGrid.currentId !== null)
    App.openForm("ReconcileForm", { account: accountGrid.currentId });
else if (menu1.item === "accountsBalance")
    form.msgBox("Click an account in the list first.", "Balance an Account");
else if (menu1.item === "investTrade")
    App.openForm("TradeForm", { account: accountGrid.currentId });
else if (menu1.item === "onlineSync")
    App.showForm("SchwabForm").syncAll();

//// toolBar1.itemClicked
if (toolBar1.item === "registerTool")
    openRegister();
else if (toolBar1.item === "syncTool")
    App.showForm("SchwabForm").syncAll();
