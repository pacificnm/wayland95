[Contents](index.md) | Next: [The Tables](02-tables.md)

# The Plan

## What Microsoft Money did, and what we'll build

| In Money | In Wayland Money | Chapter |
|---|---|---|
| Money Home: balances, bills due, a spending chart | HomeForm | [3](03-home.md) |
| Account list: bank, credit card, cash, investment, loan, asset | AccountsForm | [4](04-accounts.md) |
| Payees and categories | PayeesForm, CategoriesForm | [4](04-accounts.md) |
| The account register, with its Withdrawal / Deposit / Transfer forms | RegisterForm | [5](05-register.md) |
| Split transactions | SplitForm | [5](05-register.md) |
| Find and Replace (finding) | FindForm | [5](05-register.md) |
| Balance an account against a statement | ReconcileForm | [6](06-reconcile.md) |
| Bills & Deposits, reminders, automatic entry | BillsForm | [7](07-bills.md) |
| Budget planner | BudgetForm | [8](08-budget.md) |
| Portfolio, investment transactions, price history | PortfolioForm, TradeForm, SecuritiesForm | [9](09-investing.md) |
| Online services: download statements and quotes | SchwabForm | [10](10-schwab.md) |
| Reports & Charts | ReportsForm | [11](11-reports.md) |
| Debt reduction and loan planning | PlannerForm | [11](11-reports.md) |
| The Advisor | AdvisorForm (Claude) | [12](12-advisor.md) |

Some of Money isn't here, because the Studio has no way to do it yet:
printing, importing and exporting files (QIF, OFX), and paying bills
online. [The last chapter](14-findings.md#what-was-left-out) lists these
and the rest.

## How the program is put together

- **The data** is thirteen tables in a SQLite file. A transaction's
  amount is one signed number: negative is money leaving the account. An
  account's balance is never stored; it is the opening balance plus its
  transactions, added up when it's shown.
- **The Home page** is the startup form, and it is also the program's
  library: the functions every form needs (money as text, balances, bills,
  the Schwab requests) are written once in its code, and the other forms
  call them.
- **Each list of money** (a register, a report, the portfolio) is a
  DataGrid filled by code, so that it can show one account's rows, a
  running balance, and amounts written as `1,234.50`.
- **Schwab** is reached by the program's own code. The Studio's
  **Project > APIs** can't sign in to Schwab yet; [chapter 10](10-schwab.md)
  says why and what we do instead.

## What you need

- Wayland 95 with Wayland Studio (**Start > Programs > Programming >
  Wayland Studio**).
- For chapter 10: a Schwab brokerage account, and a free developer
  account at [developer.schwab.com](https://developer.schwab.com).
  Register the app now: [Your app at Schwab](10-schwab.md#your-app-at-schwab).
- For chapter 12: an Anthropic API key. Optional; everything else works
  without it.

## How the chapters give you a form

Each form comes as a table of its elements:

| Element | (Name) | Left, Top | Width × Height | Properties |
|---|---|---|---|---|
| Button | `enterButton` | 664, 392 | 96 × 24 | Caption: Enter; Default: True |

Put the element on the form (click it in the Toolbox, drag out a box),
then in the **Properties** window set its **(Name)**, and type its
**Left**, **Top**, **Width** and **Height** rather than dragging to the
pixel. Set the properties listed; leave the others alone. The names
matter: the code uses them, and they are spelled exactly, small letter
first.

Code is given by where it goes. *`enterButton`, **clicked*** means: select
the element, press **F7**, and in the code window choose **Object**
`enterButton` and **Procedure** `clicked` (or just double-click the
element). *The form's **load*** is Object *Form*, Procedure `load`.
**(General)** is Object *Form*, Procedure *(General)*.

Press **F5** often. Each chapter ends with something to try.

## A new project

1. Start Wayland Studio. It opens on *Project1* with one form, Form1.
2. **File > Save Project As...**: make a folder `Wayland Money` in your
   home folder and save the project there as `money`. The project is the
   one file `money.w95proj`.
3. **Project > Properties...**: **Program name** `money`, **Title**
   `Wayland Money`, **Version** `1.0`.
4. **Project > Database...**: leave it on **SQLite**. The file is made
   beside the project the first time it's needed.

Form1 stays for now; [chapter 3](03-home.md) turns it into the Home page.

[Contents](index.md) | Next: [The Tables](02-tables.md)
