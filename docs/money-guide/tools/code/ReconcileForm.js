//// (General)
readonly property var home: App.form("HomeForm")
property var accountId: null
property real difference: 0

// the transactions not reconciled yet, up to the statement's day
function fill() {
    var account = Db.get("Accounts", accountId), payees = home.names("Payees");
    var cleared = account.OpeningBalance || 0, out = [], day = statementPicker.value || home.today();
    Db.select("Transactions", { where: { Account: accountId }, orderBy: "Date" }).forEach(function (t) {
        if (t.Cleared !== "Unreconciled")
            cleared += t.Amount;
        if (t.Cleared === "Reconciled" || t.Date > day)
            return;
        out.push({ id: t.id, Date: home.showDate(t.Date), Num: t.Number || "", Payee: payees[t.Payee] || t.Memo || "",
                   C: t.Cleared === "Cleared" ? "C" : "", Payment: t.Amount < 0 ? home.fmt(-t.Amount) : "",
                   Deposit: t.Amount >= 0 ? home.fmt(t.Amount) : "" });
    });
    var keep = grid.currentIndex;
    grid.rows = out;
    grid.currentIndex = Math.min(keep, out.length - 1);
    var ending = home.parseMoney(endingBox.text);
    clearedLabel.text = home.fmt(cleared);
    difference = ending === null ? NaN : Math.round((ending - cleared) * 100) / 100;
    differenceLabel.text = ending === null ? "(type the statement's ending balance)" : home.fmt(difference);
    differenceLabel.foreColor = difference === 0 ? "#008000" : "#c00000";
    finishButton.enabled = difference === 0;
    form.title = "Balance " + account.Name;
}

// tick or untick the row that's selected
function toggle() {
    if (grid.currentId === null)
        return;
    var t = Db.get("Transactions", grid.currentId);
    Db.update("Transactions", t.id, { Cleared: t.Cleared === "Cleared" ? "Unreconciled" : "Cleared" });
    fill();
}

//// load
accountId = form.args.account;
fill();

//// endingBox.changed
fill();

//// statementPicker.changed
fill();

//// grid.rowActivated
toggle();

//// toggleButton.clicked
toggle();

//// finishButton.clicked
var done = 0;
Db.select("Transactions", { where: { Account: accountId, Cleared: "Cleared" } }).forEach(function (t) {
    Db.update("Transactions", t.id, { Cleared: "Reconciled" });
    done += 1;
});
form.msgBox("Balanced: " + done + " transactions are reconciled.", "Balance an Account", "info", ["OK"], function () {
    form.close();
});

//// cancelButton.clicked
form.close();
