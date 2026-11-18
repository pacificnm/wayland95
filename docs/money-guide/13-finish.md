[Contents](index.md) | Previous: [The Advisor](12-advisor.md) | Next: [What This Build Showed](14-findings.md)

# Finishing: the Menu, the Program, the Test

## AboutForm

**Project > Add Form**:

| Property | Value |
|---|---|
| (Name) | `AboutForm` |
| Caption | About Wayland Money |
| Resizable | False |
| Width, Height | 300, 172 |

| Element | (Name) | Left, Top | Width × Height | Properties |
|---|---|---|---|---|
| Shape | `banner` | 0, 0 | 300 × 48 | FillColor: #000080; BorderColor: #000080 |
| Label | `titleLabel` | 16, 12 | 268 × 24 | Caption: Wayland Money; ForeColor: #ffffff; FontBold: True; FontSize: 18 |
| Label | `versionLabel` | 16, 60 | 268 × 17 | Caption: (blank) |
| Label | `madeLabel` | 16, 82 | 268 × 17 | Caption: Made in Wayland Studio. |
| Label | `noteLabel` | 16, 102 | 268 × 30 | Caption: Not made by or with Charles Schwab; it uses their Trader API with your own account.; WordWrap: True |
| Button | `okButton` | 110, 140 | 80 × 24 | Caption: OK; Default: True |

`banner` is a Shape: a dark blue band behind the title. Put it on first.

The form's **load**:

```
versionLabel.text = "Version " + App.version;
```

`okButton`, **clicked**:

```
form.close();
```

Add to the **&Help** menu, after a separator:

| Menu | Caption | Name | Shortcut | Action |
|---|---|---|---|---|
| **&Help** | `&About Wayland Money` | `helpAbout` |  | Open a form: AboutForm |

## The whole menu

HomeForm's menu, complete. Check yours against it: the **Name** column
is what the code asks for.

| Menu | Caption | Name | Shortcut | Action |
|---|---|---|---|---|
| **&File** | `&Find Transactions...` | `fileFind` | Ctrl+F | Open a form: FindForm |
| | *(separator)* | | | |
|  | `E&xit` | `fileExit` |  | Quit the program |
| **&Accounts** | `&Account List...` | `accountsList` | Ctrl+Shift+A | Open a form: AccountsForm |
|  | `Open &Register` | `accountsRegister` | Ctrl+R | (none: its code only) |
|  | `&Balance an Account...` | `accountsBalance` |  | (none: its code only) |
| | *(separator)* | | | |
|  | `&Payees...` | `accountsPayees` |  | Open a form: PayeesForm |
|  | `&Categories...` | `accountsCategories` |  | Open a form: CategoriesForm |
| **&Bills** | `&Bills && Deposits...` | `billsList` | Ctrl+B | Open a form: BillsForm |
|  | `B&udget...` | `billsBudget` |  | Open a form: BudgetForm |
| **&Investing** | `&Portfolio...` | `investPortfolio` | Ctrl+P | Open a form: PortfolioForm |
|  | `Record a &Trade...` | `investTrade` |  | (none: its code only) |
|  | `&Securities && Prices...` | `investSecurities` |  | Open a form: SecuritiesForm |
| **&Reports** | `&Reports && Charts...` | `reportsList` |  | Open a form: ReportsForm |
|  | `&Loan Planner...` | `reportsPlanner` |  | Open a form: PlannerForm |
| **&Online** | `&Download from Schwab` | `onlineSync` | F9 | (none: its code only) |
|  | `Schwab &Setup...` | `onlineSetup` |  | Open a form: SchwabForm |
| **&Help** | `Ask the &Advisor...` | `helpAdvisor` | F2 | Open a form: AdvisorForm |
| | *(separator)* | | | |
|  | `&About Wayland Money` | `helpAbout` |  | Open a form: AboutForm |

An `&` underlines the next letter (Alt+A opens Accounts). In a caption,
`&&` is a real ampersand: *Bills & Deposits*.

And the tool bar:

| Caption | Name | Symbol | ToolTip | Action |
|---|---|---|---|---|
| Accounts | `accountsTool` | ▤ | The list of accounts | Open a form: AccountsForm |
| Register | `registerTool` | ✎ | The selected account's register | (none: its code only) |
| Find | `findTool` | ⌕ | Find transactions | Open a form: FindForm |
| *(separator)* | | | | |
| Bills | `billsTool` | ✉ | Bills and deposits | Open a form: BillsForm |
| Budget | `budgetTool` | ◔ | The budget | Open a form: BudgetForm |
| *(separator)* | | | | |
| Portfolio | `portfolioTool` | ▲ | What the investment accounts hold | Open a form: PortfolioForm |
| Reports | `reportsTool` | ▥ | Reports and charts | Open a form: ReportsForm |
| *(separator)* | | | | |
| Download | `syncTool` | ⇩ | Download from Schwab (F9) | (none: its code only) |
| Advisor | `advisorTool` | ✦ | Ask the Advisor | Open a form: AdvisorForm |

## Check, then make the program

**Run > Check for Problems** compiles every form without running it. It
names anything that would stop the program: a misspelled element name in
code only shows when that line runs, but a menu item that opens a form
that isn't there, two elements with one name, or code that isn't
JavaScript shows here.

**File > Make Program...** writes one file, `money`. Put it on the
desktop or in the Start menu.

### Where the program keeps its data

The program made from the project keeps **its own** database:
`~/.local/share/money/data.db` (or whatever you named the file in
Project > Database). It starts empty: the accounts you made while
building are the project's, in the file beside `money.w95proj`.

- To carry your records over, copy the project's database file over the
  program's (with the program closed).
- **Back up that one file.** It is all your records, and your Schwab
  tokens. Copy it somewhere safe as often as you'd mind losing work.
- The Schwab sign-in is in that file too, so the program needs its own
  first sign-in.

When a later version of the project adds a field or a table, the program
adds it to the database when it starts. It never deletes one.

## The test

This is the run to do at the end: every feature once, with what should
happen. It's the same sequence the guide's own check runs.

**Tables and rules**

1. A new account with Interest Rate `120` is refused: *Interest Rate has
   to be from 0 to 100.*
2. A security with the symbol `ab c!` is refused.
3. A bill with Amount `-5`, or Ends before Next Due, is refused.
4. A second budget for the same category is refused.

**The register**

5. Checking opens at 1,000.00. A 45.10 withdrawal to Corner Grocer takes
   the payee's category and leaves 954.90.
6. A 2,500.00 deposit typed as `$2,500.00` leaves 3,454.90.
7. Enter with no amount is refused. A transfer with no account is
   refused. A transfer to the same account is refused by the table.
8. A 300.00 transfer to Savings appears in both registers; changed to
   350.00 in one, the other follows; there is still exactly one pair.
9. A 120.00 withdrawal split 80.00 / 40.00 says *(split)*, and the
   Split window says *All of it has a category.*
10. Find `grocer` shows only the grocer's rows.
11. Deleting the transfer from either side removes both halves.

**Balancing**

12. With Ending balance 954.90, ticking the 45.10 row brings the
    difference to 0.00 and enables Finish; afterwards the row is `R`.

**Bills and budget**

13. A monthly bill due yesterday with Enter By Itself is in the register
    within a minute, and its Next Due is a month on.
14. A bill due in three days is on the Home page; Skip moves it a month.
15. A transfer bill, entered, moves both accounts.
16. The budget shows Budget, Spent and Left for the month, and follows a
    new transaction without being told.

**Investments**

17. Buying 10 ACME at 20 with 4.95 commission takes 204.95 of cash and
    gives a holding of 10 shares costing 204.95.
18. A Last Price of 26.50 gives the holding a value, the chart a bar,
    and the Home page a higher Brokerage.

**Schwab**

19. With a wrong Secret, Finish Sign-In says *Unauthorized* and the
    form still says *Not signed in.*
20. With the right one: *Signed in.*, then the accounts, transactions
    and prices arrive, and each Schwab account's register ends on
    Schwab's cash figure.
21. A second download right away adds 0 new transactions.
22. Half an hour later a download still works (the access token was
    renewed by itself).
23. **Download Prices** on a security fills its chart; a second press
    adds only what's new.

**Reports**

24. Each of the eight reports shows rows and a graph. Spending by
    Category for this month agrees with the Home page's pie.
25. Clicking a slice opens Find on its transactions.
26. The planner: 12,000.00 at 6.5% over 5 years is 234.79 a month.

**The program**

27. Check for Problems finds none. The made program starts, with an
    empty database and 25 categories.
28. Closing the Home page ends the program.

## How this guide was checked

The finished project, `sample/money.w95proj`, isn't typed by hand. The
guide's folder has a `tools` folder:

- `tools/app.py` describes the tables and every form's elements;
  `tools/code/` has each form's code.
- `tools/build.py` makes the project from them, **and** writes the
  tables of fields and elements and the code into these chapters, so the
  guide and the project can't disagree.
- `tools/selftest.py` runs the project without a screen and does the
  test above by code: about ninety checks, with `tools/mock_schwab.py`
  standing in for Schwab. It also takes the pictures in this guide.
- `tools/build.py --stages` checks that the project compiles at the end
  of each chapter, as you'd have it, not only at the end.

What that doesn't cover: the steps in the Studio's own windows (putting
elements on a form, the Menu Editor, the Graph Editor) were written from
the Studio's manual and not clicked through; a real Schwab account; and
a real conversation with Claude. Those are yours to test, and
[the next chapter](14-findings.md) is where to write down what you find.

[Contents](index.md) | Previous: [The Advisor](12-advisor.md) | Next: [What This Build Showed](14-findings.md)
