//// (General)
readonly property var home: App.form("HomeForm")

// what the trade does to the account's cash
function cashFor() {
    var q = Number(form.record.Quantity) || 0, p = Number(form.record.Price) || 0, fee = Number(form.record.Commission) || 0;
    var typed = home.parseMoney(amountBox.text) || 0;
    switch (form.record.Action) {
    case "Buy": return -(q * p + fee);
    case "Sell": return q * p - fee;
    case "Dividend":
    case "Interest": return Math.abs(typed);
    case "Fee": return -Math.abs(typed);
    }
    return 0;       // Reinvest, Add Shares, Remove Shares: shares move, cash doesn't
}
function showTotal() {
    var action = form.record.Action || "";
    var shares = ["Buy", "Sell", "Reinvest", "Add Shares", "Remove Shares"].indexOf(action) >= 0;
    var typed = ["Dividend", "Interest", "Fee"].indexOf(action) >= 0;
    quantityLabel.visible = quantityBox.visible = priceLabel.visible = priceBox.visible = shares;
    commissionLabel.visible = commissionBox.visible = action === "Buy" || action === "Sell";
    amountLabel.visible = amountBox.visible = typed;
    var cash = Math.round(cashFor() * 100) / 100;
    totalLabel.text = action === "" ? "Choose what happened."
                    : cash < 0 ? home.fmt(-cash) + " leaves the account's cash."
                    : cash > 0 ? home.fmt(cash) + " comes into the account's cash."
                    : "The account's cash doesn't change.";
}

//// load
if (form.args.account)
    form.setField("Account", form.args.account);
form.setField("Action", "Buy");
showTotal();

//// actionCombo.changed
showTotal();

//// quantityBox.changed
showTotal();

//// priceBox.changed
showTotal();

//// commissionBox.changed
showTotal();

//// amountBox.changed
showTotal();

//// enterButton.clicked
var account = form.record.Account ? Db.get("Accounts", form.record.Account) : ({});
if (!home.isInvestment(account.Type)) {
    form.msgBox("Choose an Investment or Retirement account.", "Trade", "warning");
    return;
}
if (account.SchwabHash) {
    form.msgBox(account.Name + " is downloaded from Schwab: its trades come from there.", "Trade", "info");
    return;
}
var action = form.record.Action;
if (action !== "Interest" && action !== "Fee" && !form.record.Security) {
    form.msgBox("Choose the security.", "Trade", "warning");
    return;
}
if (["Dividend", "Interest", "Fee"].indexOf(action) < 0 && !(Number(form.record.Quantity) > 0)) {
    form.msgBox("Type how many shares.", "Trade", "warning");
    return;
}
form.setField("Amount", Math.round(cashFor() * 100) / 100);
form.setField("Cleared", "Cleared");
if (!form.record.Memo)
    form.setField("Memo", action + (form.record.Security ? " " + Db.lookupLabel("Securities", form.record.Security) : ""));
// (income and fees get their categories; a buy or a sell isn't spending)
var wanted = action === "Dividend" || action === "Reinvest" ? "Investment Income : Dividends"
           : action === "Interest" ? "Investment Income : Interest" : action === "Fee" ? "Investment Expense : Fees" : "";
var found = wanted === "" ? [] : Db.select("Categories", { where: { Name: wanted } });
form.setField("Category", found.length > 0 ? found[0].id : null);
var accountId = form.record.Account;
if (!form.save())
    return;
home.rebuildHoldings(accountId);
form.msgBox("Entered.", "Trade", "info", ["Another", "Close"], function (answer) {
    if (answer !== 0) {
        form.close();
        return;
    }
    form.newRecord();
    form.setField("Account", accountId);
    form.setField("Action", action);
    amountBox.text = "";
    showTotal();
});

//// closeButton.clicked
form.close();
