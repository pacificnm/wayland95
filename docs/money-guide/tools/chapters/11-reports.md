# Reports, Charts and the Loan Planner

## ReportsForm

Eight reports from one form: pick one from the list, pick a period, and
the figures are in a grid on the left and a graph on the right.

![Income and expenses by month](images/reports-months.png)

**Project > Add Form**:

{{form ReportsForm}}

{{elements ReportsForm *}}

`reportList`'s **List** is the eight report names and `periodCombo`'s the
six periods, one on each line, spelled as in the table: the code chooses
by name.

{{graph ReportsForm graph1}}

**(General)**:

{{code ReportsForm (General)}}

{{events ReportsForm *}}

One function, `run()`, makes every report the same way: it builds the
rows and the graph's labels and values, then hands them over. What
changes from report to report is three things code can set while the
program runs:

- **the grid's columns**: `grid.columns = ["Item", "Income", "Expenses",
  "Amount"]` for the monthly report, two columns for the rest;
- **the graph's type**: `graph1.graphType = "Pie"`, `"Bar"`, `"Line"`,
  `"Doughnut"`;
- **one series or two**: the monthly report gives the graph Income and
  Expenses side by side.

| Report | What it adds up | Graph |
|---|---|---|
| Spending by Category | What went out, by category (splits by their parts) | Pie |
| Income by Category | What came in | Pie |
| Income and Expenses by Month | Both, a month at a time | Columns |
| Spending by Payee | The fifteen payees paid the most | Bars |
| Net Worth over Time | The WorthHistory table | Line |
| Account Balances | Every open account today | Bars |
| Tax-related Totals | The categories ticked Tax-related | Bars |
| Portfolio by Kind | Holdings by the security's type | Doughnut |

Transfers and trades are left out of income and spending (`home.isFlow`):
moving money between your own accounts, or into shares, isn't either.

**Clicking a slice or a bar** of the first two reports, or of Spending by
Payee, opens Find with that name and the report's dates: the
transactions behind the figure. That's `graph1.clickedLabel`, and
FindForm reading `form.args`.

![Spending by category](images/reports.png)

## PlannerForm

What a debt costs, and what paying a little more does to it.

![The loan planner](images/planner.png)

**Project > Add Form**:

{{form PlannerForm}}

{{elements PlannerForm *}}

{{graph PlannerForm graph1}}

**(General)**:

{{code PlannerForm (General)}}

{{events PlannerForm *}}

Choosing one of your own debts fills in what's owed (from its balance)
and its rate (from the account's Interest Rate). The schedule is worked
out a month at a time: the month's interest, what's left of the payment
for the principal, the balance after. Every box's **changed** event runs
it again, so the answer follows as you type.

## Into the Home page

`spendingGraph`, **clicked** (the pie on the Home page opens the report
it's a small copy of):

{{code HomeForm spendingGraph.clicked}}

A new menu **&Reports** after Investing:

{{menu HomeForm reportsList,reportsPlanner}}

{{toolbar HomeForm reportsTool}}

## Try it

**Reports > Reports & Charts...**. Go down the list. Change the period to
*Last 12 months*, then *All dates* (both dates go blank). Click the
biggest slice of Spending by Category: Find opens on it.

**Reports > Loan Planner...**: choose Car Loan, **Years** `5`: *234.79 a
month pays it off in 5 years. Interest: 2,087.63 in all.* Type `100` in
**Extra a month**: 3 years and 4 months, and 709.79 less interest.
