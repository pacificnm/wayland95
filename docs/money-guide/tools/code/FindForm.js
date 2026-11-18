//// (General)
readonly property var home: App.form("HomeForm")
property var accountIds: []

function find() {
    var payees = home.names("Payees"), categories = home.names("Categories"), accounts = home.names("Accounts");
    var text = findBox.text.trim().toLowerCase();
    var only = accountCombo.currentIndex > 0 ? accountIds[accountCombo.currentIndex - 1] : null;
    var from = fromPicker.value, to = toPicker.value;
    var least = home.parseMoney(leastBox.text), most = home.parseMoney(mostBox.text);
    var out = [], total = 0;
    Db.select("Transactions", { where: only === null ? {} : { Account: only }, orderBy: "Date", descending: true }).forEach(function (t) {
        if ((from && t.Date < from) || (to && t.Date > to))
            return;
        var size = Math.abs(t.Amount);
        if ((least !== null && size < least) || (most !== null && size > most))
            return;
        var line = { id: t.id, Date: home.showDate(t.Date), Account: accounts[t.Account] || "", Num: t.Number || "",
                     Payee: payees[t.Payee] || "", Category: t.TransferAccount ? "Transfer" : t.IsSplit ? "(split)" : (categories[t.Category] || ""),
                     Memo: t.Memo || "", Amount: home.fmt(t.Amount) };
        if (text !== "" && (line.Payee + " " + line.Category + " " + line.Memo + " " + line.Num + " " + line.Account)
                .toLowerCase().indexOf(text) < 0)
            return;
        out.push(line);
        total += t.Amount;
    });
    grid.rows = out;
    countLabel.text = out.length + (out.length === 1 ? " transaction" : " transactions") + ", " + home.fmt(total) + " in all";
}

//// load
var accounts = Db.select("Accounts", { orderBy: "Name" });
accountIds = accounts.map(function (a) { return a.id; });
accountCombo.items = ["(all accounts)"].concat(accounts.map(function (a) { return a.Name; }));
accountCombo.currentIndex = 0;
// (a report's slice was clicked: its transactions)
if (form.args.text)
    findBox.text = form.args.text;
if (form.args.from)
    fromPicker.value = form.args.from;
if (form.args.to)
    toPicker.value = form.args.to;
find();

//// findBox.changed
find();

//// findButton.clicked
find();

//// grid.rowActivated
var t = Db.get("Transactions", grid.currentId);
App.openForm("RegisterForm", { account: t.Account, transaction: t.id });
