//// (General)
readonly property var home: App.form("HomeForm")

// each budget beside what was spent in the month the picker shows
function fill() {
    var day = monthPicker.value || home.today();
    var from = home.monthStart(day), to = home.addDays(home.addMonths(from, 1), -1);
    var spent = {}, names = home.names("Categories");
    home.totalsByCategory(from, to).forEach(function (c) { spent[c.category] = -c.total; });
    var out = [], labels = [], budgeted = [], used = [], totalBudget = 0, totalSpent = 0;
    Db.select("Budgets").forEach(function (b) {
        var s = Math.max(0, spent[b.Category] || 0);
        out.push({ id: b.id, Category: names[b.Category] || "", Budget: home.fmt(b.Amount), Spent: home.fmt(s),
                   Left: home.fmt(b.Amount - s) });
        labels.push(names[b.Category] || "");
        budgeted.push(b.Amount);
        used.push(Math.round(s * 100) / 100);
        totalBudget += b.Amount;
        totalSpent += s;
    });
    grid.rows = out;
    graph1.setData(labels, [{ name: "Budget", values: budgeted }, { name: "Spent", values: used }]);
    graph1.title = AppDates.display(from, "Date", "MMMM yyyy", "");
    totalLabel.text = "Budget " + home.fmt(totalBudget) + "   Spent " + home.fmt(totalSpent)
        + "   Left " + home.fmt(totalBudget - totalSpent);
    totalLabel.foreColor = totalSpent > totalBudget ? "#c00000" : "#000000";
}

Timer { id: soon; interval: 300; onTriggered: fill() }
Connections {
    target: Db
    function onChanged(table) {
        if (table === "Budgets" || table === "Transactions" || table === "Splits")
            soon.restart();
    }
}

//// load
fill();

//// monthPicker.changed
fill();

//// grid.rowSelected
form.loadRecord(grid.currentId);

//// newButton.clicked
form.newRecord();
grid.currentIndex = -1;
categoryCombo.forceActiveFocus();

//// saveButton.clicked
form.save();

//// deleteButton.clicked
if (!form.isNew)
    form.deleteRecord();
