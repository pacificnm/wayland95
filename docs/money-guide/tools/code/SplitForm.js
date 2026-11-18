//// (General)
readonly property var home: App.form("HomeForm")
property var transactionId: null
property real left: 0

function fill() {
    var t = Db.get("Transactions", transactionId), categories = home.names("Categories");
    var sign = t.Amount < 0 ? -1 : 1, total = 0, out = [];
    Db.select("Splits", { where: { Transaction: transactionId } }).forEach(function (s) {
        total += s.Amount;
        out.push({ id: s.id, Category: categories[s.Category] || "", Memo: s.Memo || "", Amount: home.fmt(sign * s.Amount) });
    });
    grid.rows = out;
    left = Math.round((t.Amount - total) * 100) / 100;
    totalLabel.text = home.showDate(t.Date) + "   " + (t.Memo || "") + "   " + home.fmt(Math.abs(t.Amount))
        + (t.Amount < 0 ? " spent" : " received");
    leftLabel.text = left === 0 ? "All of it has a category." : "Still to give a category: " + home.fmt(sign * left);
    leftLabel.foreColor = left === 0 ? "#008000" : "#c00000";
    // (the transaction is a split while it has any)
    if (t.IsSplit !== (out.length > 0))
        Db.update("Transactions", transactionId, out.length > 0 ? { IsSplit: true, Category: null } : { IsSplit: false });
}

function startNew() {
    form.newRecord();
    amountBox.text = left !== 0 ? home.fmt(Math.abs(left)) : "";
    grid.currentIndex = -1;
    categoryCombo.forceActiveFocus();
}

function saveLine() {
    var amount = home.parseMoney(amountBox.text);
    if (amount === null || amount === 0) {
        form.msgBox("Type this part's amount.", "Split", "warning");
        return false;
    }
    var t = Db.get("Transactions", transactionId);
    form.setField("Transaction", transactionId);
    form.setField("Amount", (t.Amount < 0 ? -1 : 1) * Math.abs(amount));
    if (!form.save())
        return false;
    fill();
    return true;
}

//// load
transactionId = form.args.transaction;
fill();
startNew();

//// current
amountBox.text = form.record.Amount ? home.fmt(Math.abs(form.record.Amount)) : "";

//// grid.rowSelected
form.loadRecord(grid.currentId);

//// amountBox.accepted
if (saveLine())
    startNew();

//// addButton.clicked
if (saveLine())
    startNew();

//// deleteButton.clicked
if (!form.isNew) {
    form.deleteRecord();
    fill();
}
startNew();

//// doneButton.clicked
if (left === 0 || grid.rows.length === 0) {
    form.close();
    return;
}
form.msgBox("The parts don't add up to the transaction (" + home.fmt(Math.abs(left)) + " is left).\n"
            + "Change the transaction's amount to what the parts come to?", "Split", "question",
            ["Change It", "Keep Editing"], function (answer) {
    if (answer !== 0)
        return;
    var t = Db.get("Transactions", transactionId);
    Db.update("Transactions", transactionId, { Amount: t.Amount - left });
    home.matchTransfer(transactionId);
    form.close();
});
