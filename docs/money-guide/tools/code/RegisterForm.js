//// (General)
readonly property var home: App.form("HomeForm")
property var accountId: null
property var accountIds: []
property string lastDate: ""

// the account's transactions into the grid, oldest first, with the balance after each
function fill() {
    var account = Db.get("Accounts", accountId);
    if (account.id === undefined)
        return;
    var payees = home.names("Payees"), categories = home.names("Categories"), accounts = home.names("Accounts");
    var find = findBox.text.trim().toLowerCase();
    var balance = account.OpeningBalance || 0, cleared = balance, out = [];
    Db.select("Transactions", { where: { Account: accountId }, orderBy: "Date" }).forEach(function (t) {
        balance += t.Amount;
        if (t.Cleared !== "Unreconciled")
            cleared += t.Amount;
        var category = t.TransferAccount ? "Transfer: " + (accounts[t.TransferAccount] || "?")
                     : t.IsSplit ? "(split)"
                     : t.Action ? t.Action + (t.Category ? ", " + categories[t.Category] : "")
                     : (categories[t.Category] || "");
        var line = { id: t.id, Date: home.showDate(t.Date), Num: t.Number || "", Payee: payees[t.Payee] || "",
                     Category: category, Memo: t.Memo || "",
                     C: t.Cleared === "Reconciled" ? "R" : t.Cleared === "Cleared" ? "C" : "",
                     Payment: t.Amount < 0 ? home.fmt(-t.Amount) : "", Deposit: t.Amount >= 0 ? home.fmt(t.Amount) : "",
                     Balance: home.fmt(balance) };
        var text = (line.Date + " " + line.Num + " " + line.Payee + " " + line.Category + " " + line.Memo
                    + " " + line.Payment + line.Deposit).toLowerCase();
        if (find === "" || text.indexOf(find) >= 0)
            out.push(line);
    });
    grid.rows = out;
    grid.currentIndex = rowOf(form.recordId);
    var holdings = home.balances()[accountId].holdings;
    balanceLabel.text = "Balance: " + home.fmt(balance)
        + (holdings !== 0 ? "   Investments: " + home.fmt(holdings) : "")
        + "   Cleared: " + home.fmt(cleared);
    form.title = account.Name + " - Register";
}

// where a transaction is in the grid (-1: it isn't)
function rowOf(id) {
    for (var i = 0; i < grid.rows.length; ++i) {
        if (grid.rows[i].id === id)
            return i;
    }
    return -1;
}

function showAccount(id) {
    accountId = id;
    accountCombo.currentIndex = accountIds.indexOf(id);
    form.newRecord();
    fill();
    if (grid.rows.length > 0)
        grid.select(grid.rows.length - 1, false);
}

// the amount box and the three options, from the record that's showing
function showKind() {
    var amount = form.record.Amount;
    amountBox.text = amount ? home.fmt(Math.abs(amount)) : "";
    optTransfer.checked = !!form.record.TransferAccount;
    optDeposit.checked = !optTransfer.checked && amount > 0;
    optWithdrawal.checked = !optTransfer.checked && !optDeposit.checked;
    kindChanged();
}
function kindChanged() {
    toLabel.visible = toCombo.visible = optTransfer.checked;
    categoryLabel.visible = categoryCombo.visible = !optTransfer.checked;
}

// Enter: the record as the form has it, into the register
function enter() {
    var amount = home.parseMoney(amountBox.text);
    if (amount === null || amount === 0) {
        form.msgBox("Type the amount: 12.50", "Register", "warning");
        amountBox.forceActiveFocus();
        return false;
    }
    if (optTransfer.checked && !form.record.TransferAccount) {
        form.msgBox("Choose the account the money goes to.", "Register", "warning");
        return false;
    }
    form.setField("Account", accountId);
    form.setField("Amount", (optDeposit.checked ? 1 : -1) * Math.abs(amount));
    if (optTransfer.checked) {
        form.setField("Category", null);
    } else {
        form.setField("TransferAccount", null);
        // a payee's usual category, if none was chosen
        if (form.record.Payee && !form.record.Category && !form.record.IsSplit) {
            var payee = Db.get("Payees", form.record.Payee);
            if (payee.Category)
                form.setField("Category", payee.Category);
        }
    }
    if (!form.save())
        return false;
    lastDate = form.record.Date;
    // (a transfer's other half is written, and this one is read again with its link to it)
    var id = form.recordId;
    home.matchTransfer(id);
    form.loadRecord(id);
    fill();
    return true;
}

function startNew() {
    form.newRecord();
    if (lastDate !== "")
        form.setField("Date", lastDate);
    grid.currentIndex = -1;
    dateBox.forceActiveFocus();
}

// (another form changed the transactions: a download, a bill, a split)
Timer { id: soon; interval: 300; onTriggered: fill() }
Connections {
    target: Db
    function onChanged(table) {
        if (table === "Transactions" || table === "Splits" || table === "Holdings")
            soon.restart();
    }
}

//// load
var accounts = Db.select("Accounts", { orderBy: "Name" });
accountIds = accounts.map(function (a) { return a.id; });
accountCombo.items = accounts.map(function (a) { return a.Name; });
if (accounts.length === 0) {
    form.msgBox("There are no accounts yet: Accounts > Account List.", "Register");
    return;
}
showAccount(form.args.account || accountIds[0]);
if (form.args.transaction && form.loadRecord(form.args.transaction))
    fill();

//// current
showKind();

//// accountCombo.changed
if (accountIds[accountCombo.currentIndex] !== accountId)
    showAccount(accountIds[accountCombo.currentIndex]);

//// findBox.changed
fill();

//// grid.rowSelected
form.loadRecord(grid.currentId);

//// optWithdrawal.clicked
kindChanged();

//// optDeposit.clicked
kindChanged();

//// optTransfer.clicked
kindChanged();

//// amountBox.accepted
if (enter())
    startNew();

//// newButton.clicked
startNew();

//// enterButton.clicked
if (enter())
    startNew();

//// deleteButton.clicked
if (form.isNew) {
    startNew();
    return;
}
form.msgBox("Delete this transaction?", "Register", "question", ["Yes", "No"], function (answer) {
    if (answer !== 0)
        return;
    home.removeTransaction(form.recordId);
    form.newRecord();
    fill();
});

//// splitButton.clicked
if (form.isNew || form.dirty) {
    if (!enter())
        return;
}
App.openForm("SplitForm", { transaction: form.recordId });

//// payeesButton.clicked
App.showForm("PayeesForm");
