//// (General)
readonly property var home: App.form("HomeForm")
property var debtIds: []

function work() {
    var amount = home.parseMoney(amountBox.text), rate = Number(rateBox.text), years = Number(yearsBox.text);
    var extra = home.parseMoney(extraBox.text) || 0;
    if (amount === null || amount <= 0 || rateBox.text.trim() === "" || isNaN(rate) || rate < 0 || !(years > 0)) {
        resultLabel.text = "Type what's owed, the interest rate and how many years.";
        grid.rows = [];
        graph1.setData([], []);
        return;
    }
    var months = Math.round(years * 12), r = rate / 100 / 12;
    var payment = r === 0 ? amount / months : amount * r / (1 - Math.pow(1 + r, -months));
    var owed = amount, interest = 0, rows = [], labels = [], left = [], month = 0;
    while (owed > 0.005 && month < 1200) {
        month += 1;
        var i = owed * r, p = Math.min(owed, payment + extra - i);
        owed -= p;
        interest += i;
        rows.push({ id: month, Num: String(month), Payment: home.fmt(p + i), Principal: home.fmt(p), Interest: home.fmt(i),
                    Balance: home.fmt(owed) });
        if (month % 12 === 0 || owed <= 0.005) {
            labels.push("Year " + Math.ceil(month / 12));
            left.push(Math.round(owed));
        }
    }
    grid.rows = rows;
    graph1.setData(labels, [{ name: "Still owed", values: left }]);
    var plain = payment * months - amount;
    resultLabel.text = home.fmt(payment + extra) + " a month pays it off in " + Math.floor(month / 12) + " years"
        + (month % 12 ? " and " + (month % 12) + " months" : "") + ". Interest: " + home.fmt(interest) + " in all"
        + (extra > 0 ? ", " + home.fmt(plain - interest) + " less than without the extra." : ".");
}

//// load
var b = home.balances(), names = [];
Db.select("Accounts", { orderBy: "Name" }).forEach(function (a) {
    if (home.isDebt(a.Type) && !a.Closed) {
        debtIds.push(a.id);
        names.push(a.Name);
    }
});
accountCombo.items = names;
work();

//// accountCombo.changed
var a = Db.get("Accounts", debtIds[accountCombo.currentIndex]);
amountBox.text = home.fmt(-home.balances()[a.id].value);
rateBox.text = a.InterestRate === null ? "" : String(a.InterestRate);
work();

//// amountBox.changed
work();

//// rateBox.changed
work();

//// yearsBox.changed
work();

//// extraBox.changed
work();
