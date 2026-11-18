//// (General)
readonly property var home: App.form("HomeForm")

//// current
if (form.isNew)
    balanceLabel.text = "A new account: fill it in and press Save.";
else
    balanceLabel.text = "Balance today: " + home.fmt(home.balances()[form.recordId].value);

//// saved
balanceLabel.text = "Saved " + form.record.Name + ".";

//// registerButton.clicked
if (form.isNew)
    form.msgBox("Save the account first.", "Accounts");
else
    App.openForm("RegisterForm", { account: form.recordId });

//// deleteButton.clicked
if (form.isNew)
    return;
if (Db.count("Transactions", { Account: form.recordId }) > 0) {
    form.msgBox("This account has transactions, so it can't be deleted.\nTick Closed instead: it leaves the lists and keeps its history.",
                "Accounts", "info");
    return;
}
form.msgBox("Delete this account?", "Accounts", "question", ["Yes", "No"], function (answer) {
    if (answer === 0)
        form.deleteRecord();
});
