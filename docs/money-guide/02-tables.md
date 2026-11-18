[Contents](index.md) | Previous: [The Plan](01-plan.md) | Next: [The Home Page](03-home.md)

# The Tables

Thirteen tables hold everything. Make them in this order: a **Lookup**
field needs the table it looks up to exist already.

For each one: **Project > Add Table**, set its **Table Name** (in the
properties under the list of fields), then **Add Field** for each field
and set what the table below says. A new table starts with one field,
Name (Text, Required): rename it to the table's first field. The
**Properties** column uses the Data Designer's own words; anything not
mentioned is left as it is.

Every rule put here is the table's, not a form's: whatever writes a
record (a form, code, the Schwab download, the Advisor) goes through it.

## Accounts

| Field Name | Data Type | Properties |
|---|---|---|
| Name | Text | Required; No Duplicates; Max Length: 60 |
| Type | Choice | Required; Choices: Checking, Savings, Credit Card, Cash, Investment, Retirement, Loan, Asset, Liability; Default Value: Checking |
| Institution | Text | Max Length: 60 |
| AccountNumber | Text | Caption: Account Number; Max Length: 40 |
| OpeningBalance | Currency | Caption: Opening Balance; Default Value: 0 |
| OpeningDate | Date | Caption: Opening Date; Default Value: Today |
| CreditLimit | Currency | Caption: Credit Limit; Validation Rule: *Zero or more* |
| InterestRate | Decimal | Caption: Interest Rate; Decimal Places: 3; Validation Rule: *A percentage (0 to 100)* |
| Closed | Yes/No |  |
| SchwabHash | Text | Caption: Schwab Link |
| LastSync | Date and Time | Caption: Last Download |
| Notes | Long Text |  |

- **Type** decides how the program treats the account: *Credit Card*,
  *Loan* and *Liability* are money owed; *Investment* and *Retirement*
  can hold securities.
- **Interest Rate** uses the rule *A percentage (0 to 100)*, and
  **Credit Limit** *Zero or more*: two of the Studio's hundred-odd
  Validation Rules, picked from the list for the field's type.
- **Schwab Link** and **Last Download** are filled in by the download in
  [chapter 10](10-schwab.md). Nobody types them.
- There is no Balance field. A balance kept in a field is wrong the
  moment a transaction changes; this one is always worked out.

## Categories

| Field Name | Data Type | Properties |
|---|---|---|
| Name | Text | Required; No Duplicates; Max Length: 60 |
| Kind | Choice | Required; Choices: Expense, Income; Default Value: Expense |
| TaxRelated | Yes/No | Caption: Tax-related |
| Notes | Text |  |

A category's name can carry its group, Money's way: `Automobile : Fuel`,
`Housing : Rent`. The program fills in a starting list the first time it
runs ([chapter 3](03-home.md)), so there's nothing to type here.

## Payees

| Field Name | Data Type | Properties |
|---|---|---|
| Name | Text | Required; No Duplicates; Max Length: 80 |
| Category | Lookup | Caption: Usual Category; Lookup Table: Categories |
| Phone | Phone |  |
| Email | E-mail | Caption: E-mail |
| Website | URL |  |
| Address | Long Text |  |
| Notes | Long Text |  |

**Usual Category** saves typing later: a transaction for this payee with
no category gets this one. Phone, E-mail and URL are field types of their
own; each checks what's typed.

## Securities

| Field Name | Data Type | Properties |
|---|---|---|
| Symbol | Text | Required; No Duplicates; Max Length: 32; Pattern: `[A-Z0-9 ./$-]+`; Validation Text: A symbol is capitals and digits: AAPL, BRK/B. |
| Company | Text | Max Length: 120 |
| Type | Choice | Required; Choices: Stock, ETF, Mutual Fund, Bond, Option, Money Market, Other; Default Value: Stock |
| LastPrice | Decimal | Caption: Last Price; Decimal Places: 4; Validation Rule: *Zero or more* |
| Change | Decimal | Decimal Places: 4 |
| ChangePercent | Decimal | Caption: Change %; Decimal Places: 2 |
| PriceTime | Date and Time | Caption: Price As Of |
| Watch | Yes/No | Caption: On Watch List |

The **Pattern** is a regular expression the whole symbol has to match. It
allows what Schwab sends: `BRK/B`, and option symbols with spaces in them.

> **Why "Company" and not "Name"?** A Lookup shows a record by one of its
> text fields. The Studio picks a field called Name, Title, Label or
> Description if the table has one, and only otherwise the first text
> field. We want lists of securities to show the symbol, so the table has
> no field called Name.

## Transactions

The heart of it. One record is one line of a register.

| Field Name | Data Type | Properties |
|---|---|---|
| Account | Lookup | Required; Lookup Table: Accounts |
| Date | Date | Required; Default Value: Today |
| Number | Text | Caption: Num; Max Length: 12 |
| Payee | Lookup | Lookup Table: Payees |
| Category | Lookup | Lookup Table: Categories |
| Memo | Text | Max Length: 120 |
| Amount | Currency | Required; Validation Rule: *Custom expression* (the Expression is under this table); Validation Text: Amount can't be 0. |
| Cleared | Choice | Caption: Status; Choices: Unreconciled, Cleared, Reconciled; Default Value: Unreconciled |
| TransferAccount | Lookup | Caption: Transfer Account; Lookup Table: Accounts; Validation Rule: *Different from another field*, Other Field: Account; Validation Text: A transfer goes to another account. |
| TransferId | Whole Number | Caption: Transfer's Other Half |
| IsSplit | Yes/No | Caption: Split |
| Action | Choice | Choices: Buy, Sell, Dividend, Interest, Reinvest, Add Shares, Remove Shares, Fee |
| Security | Lookup | Lookup Table: Securities; Validation Rule: *Only when another field is filled in*, Only With: Action |
| Quantity | Decimal | Caption: Shares; Decimal Places: 4; Validation Rule: *Zero or more* |
| Price | Decimal | Decimal Places: 4; Validation Rule: *Zero or more* |
| Commission | Currency | Validation Rule: *Zero or more* |
| Source | Choice | Choices: Manual, Schwab, Bill; Default Value: Manual |
| SchwabId | Text | Caption: Schwab Id; No Duplicates |

**Amount**'s Expression is `value !== 0 || record.Action`: an amount
can't be 0, unless the transaction is a share movement (shares received
have no cash in them). `value` is the field, `record` the whole record.

- **Amount** is signed: `-45.10` is a payment, `2500` a deposit.
- A **transfer** is two transactions, one in each account, each naming
  the other account in **Transfer Account** and the other record in
  **Transfer's Other Half**. *Different from another field* stops a
  transfer to the same account.
- **Split** says the transaction's categories are in the Splits table.
- **Action**, **Security**, **Shares**, **Price** and **Commission** are
  for investment accounts. *Only when another field is filled in* refuses
  a security on a transaction that has no action.
- **Schwab Id** is Schwab's own number for a downloaded transaction.
  **No Duplicates** is what stops a transaction being downloaded twice;
  blank values don't count as duplicates of each other.
- **Transfer's Other Half** is a Whole Number, not a Lookup, on purpose:
  a record can't be deleted while a Lookup points at it, and the two
  halves of a transfer would each protect the other for ever.

## Splits

| Field Name | Data Type | Properties |
|---|---|---|
| Transaction | Lookup | Required; Lookup Table: Transactions |
| Category | Lookup | Required; Lookup Table: Categories |
| Memo | Text | Max Length: 120 |
| Amount | Currency | Required; Validation Rule: *Not zero* |

## Bills

| Field Name | Data Type | Properties |
|---|---|---|
| Name | Text | Required; Max Length: 80 |
| Kind | Choice | Required; Choices: Bill, Deposit, Transfer; Default Value: Bill |
| Account | Lookup | Required; Lookup Table: Accounts |
| ToAccount | Lookup | Caption: To Account; Lookup Table: Accounts; Validation Rule: *Different from another field*, Other Field: Account; Validation Text: A transfer goes to another account. |
| Payee | Lookup | Lookup Table: Payees |
| Category | Lookup | Lookup Table: Categories |
| Amount | Currency | Required; Validation Rule: *More than zero* |
| Frequency | Choice | Required; Choices: Once, Weekly, Every two weeks, Twice a month, Monthly, Every three months, Every six months, Yearly; Default Value: Monthly |
| NextDue | Date | Caption: Next Due; Required; Default Value: Today |
| EndDate | Date | Caption: Ends; Validation Rule: *On or after another field*, Other Field: NextDue |
| RemindDays | Whole Number | Caption: Remind Days; Default Value: 7; Minimum: 0; Maximum: 60 |
| AutoEnter | Yes/No | Caption: Enter By Itself |
| Active | Yes/No | Default Value: Yes |
| Memo | Text | Max Length: 120 |

Set the table's **Caption** to `Bills and Deposits`. **Ends** uses *On or
after another field* (Next Due); **Amount** *More than zero*: a bill is
typed as a plain amount, and its **Kind** says which way the money goes.

## Budgets

| Field Name | Data Type | Properties |
|---|---|---|
| Category | Lookup | Required; No Duplicates; Lookup Table: Categories |
| Amount | Currency | Caption: Monthly Amount; Required; Validation Rule: *More than zero* |
| Notes | Text |  |

**No Duplicates** on a Lookup: one budget for a category.

## Holdings

| Field Name | Data Type | Properties |
|---|---|---|
| Account | Lookup | Required; Lookup Table: Accounts |
| Security | Lookup | Required; Lookup Table: Securities |
| Quantity | Decimal | Caption: Shares; Decimal Places: 4 |
| CostBasis | Currency | Caption: Cost Basis |
| MarketValue | Currency | Caption: Market Value |
| AsOf | Date and Time | Caption: As Of |

What an account holds of a security. Code writes these: from Schwab's
positions for a Schwab account, from the trades for any other.

## Prices

| Field Name | Data Type | Properties |
|---|---|---|
| Security | Lookup | Required; Lookup Table: Securities |
| Day | Date | Required |
| Open | Decimal | Decimal Places: 4 |
| High | Decimal | Decimal Places: 4; Validation Rule: *At least another field*, Other Field: Low |
| Low | Decimal | Decimal Places: 4 |
| Close | Decimal | Required; Decimal Places: 4 |
| Volume | Whole Number | Minimum: 0 |

One record for a security for a day: what the candlestick chart draws.
**High** uses *At least another field* (Low).

## WorthHistory

| Field Name | Data Type | Properties |
|---|---|---|
| Day | Date | Required; No Duplicates |
| Assets | Currency |  |
| Liabilities | Currency |  |
| Worth | Currency | Caption: Net Worth |

Set its **Caption** to `Net Worth History`. The program writes one line
for each day it's used; the Home page's line graph reads this table with
no code at all.

## Settings

| Field Name | Data Type | Properties |
|---|---|---|
| Name | Text | Required; No Duplicates; Max Length: 40 |
| Value | Long Text |  |

Named values the program keeps for itself: the Schwab app's key and
tokens, the time of the last download.

## Listing

This one is different. It never holds a record.

| Field Name | Data Type | Properties |
|---|---|---|
| Date | Text |  |
| Num | Text |  |
| Payee | Text |  |
| Category | Text |  |
| Memo | Text |  |
| C | Text |  |
| Account | Text |  |
| Type | Text |  |
| Item | Text |  |
| Symbol | Text |  |
| Name | Text |  |
| Payment | Currency |  |
| Deposit | Currency |  |
| Balance | Currency |  |
| Amount | Currency |  |
| Income | Currency |  |
| Expenses | Currency |  |
| Budget | Currency |  |
| Spent | Currency |  |
| Left | Currency |  |
| Shares | Currency |  |
| Price | Currency |  |
| Value | Currency |  |
| Cost | Currency |  |
| Gain | Currency |  |
| Percent | Currency | Caption: Gain % |
| Principal | Currency |  |
| Interest | Currency |  |

A DataGrid shows the fields of a table, in columns, and by itself it
shows every record of that table as the database has it. A register needs
more than that: one account's transactions only, a payee's name instead
of its number, `1,234.50` instead of `1234.5`, and a running balance,
which isn't a field anywhere. So the grids that list money are given
**Listing** as their table, purely for its column headings, and code
hands them their rows:

```
grid.rows = [{ id: 7, Date: "11/18/2026", Payee: "Corner Grocer", Payment: "45.10", Balance: "954.90" }];
```

Each grid picks the columns it wants with its **Columns** property. The
Currency fields are there because a grid lines numbers up on the right.
Because nothing is ever written to Listing, the grid never reads it
again, and the rows code gave it stay. The fields to add:

- Text: `Date`, `Num`, `Payee`, `Category`, `Memo`, `C`, `Account`,
  `Type`, `Item`, `Symbol`, `Name`.
- Currency: `Payment`, `Deposit`, `Balance`, `Amount`, `Income`,
  `Expenses`, `Budget`, `Spent`, `Left`, `Shares`, `Price`, `Value`,
  `Cost`, `Gain`, `Percent` (Caption: `Gain %`), `Principal`, `Interest`.

## Make them, and try a rule

Open **Accounts** and click **Data**. The Studio says the database doesn't
have the project's tables yet and asks: **Yes** makes all thirteen.

**New Record**: type a Name, set Interest Rate to `120`, and **Save**. The
table refuses it, in red: *Interest Rate has to be from 0 to 100.* That
message will appear the same way in every form you build on this table.
Cancel; the real accounts go in through the program.

[Contents](index.md) | Previous: [The Plan](01-plan.md) | Next: [The Home Page](03-home.md)
