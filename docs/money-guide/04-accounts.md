[Contents](index.md) | Previous: [The Home Page](03-home.md) | Next: [The Account Register](05-register.md)

# Accounts, Payees and Categories

Three forms, and the Studio draws nearly all of them.

## AccountsForm

Open the **Accounts** table and choose **Project > Add Form for
Accounts**. The Studio makes AccountsForm: a label and an element for
each field (a box for text and numbers, a list for the Choice, a check
box for the Yes/No, a date picker for the dates), **New**, **Save** and
**Delete** buttons with their code, and a grid of the accounts on the
right. The elements are named after their fields: `nameBox`, `typeCombo`,
`closedCheck`, `lastSyncPicker`.

![AccountsForm](images/accounts.png)

Then change a few things:

1. The form: **Height** `456` (drag its bottom edge, or type it).
2. `schwabHashBox` and `lastSyncPicker`: **Enabled** False. The download
   sets these; they're shown, not typed.
3. `grid`: **Columns** `Name`, `Type`, `Institution` (one on each line).
   This grid needs no code: it lists the form's own table, and clicking
   a row shows that account in the form.
4. Add two elements:

| Element | (Name) | Left, Top | Width × Height | Properties |
|---|---|---|---|---|
| Button | `registerButton` | 12, 420 | 120 × 24 | Caption: Open Register |
| Label | `balanceLabel` | 144, 424 | 488 × 17 | Caption: (blank); FontBold: True |

Its code. **(General)**:

```
readonly property var home: App.form("HomeForm")
```

The form's **current**:

```
if (form.isNew)
    balanceLabel.text = "A new account: fill it in and press Save.";
else
    balanceLabel.text = "Balance today: " + home.fmt(home.balances()[form.recordId].value);
```

The form's **saved**:

```
balanceLabel.text = "Saved " + form.record.Name + ".";
```

`registerButton`, **clicked**:

```
if (form.isNew)
    form.msgBox("Save the account first.", "Accounts");
else
    App.openForm("RegisterForm", { account: form.recordId });
```

`deleteButton`, **clicked**:

```
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
```

`deleteButton` already has code the Studio wrote; replace it with the
code above. An account with transactions can't be deleted in any case
(the Transactions table's Lookup protects it: the table would refuse);
this says so in plain words first, and offers **Closed** instead.

`home` is the Home page, found once when the form opens. Every form from
here on starts its (General) with that line.

## PayeesForm and CategoriesForm

**Project > Add Form for Payees**, and **for Categories** (open each
table first). Leave them exactly as they're drawn: neither needs a line
of code.

![PayeesForm, as the Studio drew it](images/payees.png)

On PayeesForm, **Usual Category** is a list of the categories, because
the field is a Lookup. Add a category in CategoriesForm while PayeesForm
is open and the list has it at once.

## Into the menu

Open HomeForm, select the menu bar, and open its **Menus**. Add a menu
**&Accounts** after File (select File's last line, **Next**, then **←**
to make it a menu rather than an item), and under it:

| Menu | Caption | Name | Shortcut | Action |
|---|---|---|---|---|
| **&Accounts** | `&Account List...` | `accountsList` | Ctrl+Shift+A | Open a form: AccountsForm |
| **&Accounts** | `&Payees...` | `accountsPayees` |  | Open a form: PayeesForm |
| **&Accounts** | `&Categories...` | `accountsCategories` |  | Open a form: CategoriesForm |

Type each **Name** as given: code in the next chapter asks for the items
by name. Then select the tool bar, open its **Buttons**, and add:

| Caption | Name | Symbol | ToolTip | Action |
|---|---|---|---|---|
| Accounts | `accountsTool` | ▤ | The list of accounts | Open a form: AccountsForm |

## Try it

**F5**, then **Accounts > Account List...**. Make the accounts you have.
For a first test:

| Name | Type | Opening Balance | Also |
|---|---|---|---|
| Checking | Checking | 1000 | |
| Savings | Savings | 5000 | |
| Visa | Credit Card | 0 | Credit Limit 5000, Interest Rate 19.99 |
| Car Loan | Loan | -12000 | Interest Rate 6.5 |
| Brokerage | Investment | 0 | |

What's owed is a negative balance: the car loan opens at `-12000`.

Each one appears on the Home page as you save it, with **Assets**,
**Owed** and **Net worth** under the list, and a first point on the net
worth graph. Nothing told the Home page to redraw: it heard the database
change.

Try saving an account with no name, or an interest rate of 120: the box
is framed in red, and the message box says why.

Then **Accounts > Payees...**: add `Corner Grocer` (Usual Category:
Groceries), `Landlord` (Housing : Rent) and `Employer` (Salary). Type an
E-mail of `nope` and Save: the E-mail field refuses it.

[Contents](index.md) | Previous: [The Home Page](03-home.md) | Next: [The Account Register](05-register.md)
