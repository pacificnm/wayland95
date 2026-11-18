//// (General)
readonly property var home: App.form("HomeForm")

function check() {
    if (form.record.Kind === "Transfer" && !form.record.ToAccount) {
        form.msgBox("A transfer needs the account the money goes to.", "Bills", "warning");
        return false;
    }
    return true;
}

//// current
if (form.isNew) {
    dueLabel.text = "A new bill: fill it in and press Save.";
} else {
    var days = home.daysBetween(home.today(), form.record.NextDue);
    dueLabel.text = !form.record.Active ? "This one is finished."
                  : days < 0 ? "Overdue by " + (-days) + (days === -1 ? " day." : " days.")
                  : days === 0 ? "Due today." : "Due in " + days + (days === 1 ? " day." : " days.");
}
toAccountLabel.visible = toAccountCombo.visible = form.record.Kind === "Transfer";

//// kindCombo.changed
toAccountLabel.visible = toAccountCombo.visible = form.record.Kind === "Transfer";

//// saveButton.clicked
if (check())
    form.save();

//// enterButton.clicked
if (form.isNew || form.dirty) {
    if (!check() || !form.save())
        return;
}
var id = form.recordId;
if (home.enterBill(id)) {
    form.loadRecord(id);
    dueLabel.text = "Entered in the register. " + dueLabel.text;
}

//// skipButton.clicked
if (form.isNew)
    return;
var id = form.recordId;
home.skipBill(id);
form.loadRecord(id);
