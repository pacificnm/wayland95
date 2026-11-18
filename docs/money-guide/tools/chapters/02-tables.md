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

{{fields Accounts}}

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

{{fields Categories}}

A category's name can carry its group, Money's way: `Automobile : Fuel`,
`Housing : Rent`. The program fills in a starting list the first time it
runs ([chapter 3](03-home.md)), so there's nothing to type here.

## Payees

{{fields Payees}}

**Usual Category** saves typing later: a transaction for this payee with
no category gets this one. Phone, E-mail and URL are field types of their
own; each checks what's typed.

## Securities

{{fields Securities}}

The **Pattern** is a regular expression the whole symbol has to match. It
allows what Schwab sends: `BRK/B`, and option symbols with spaces in them.

> **Why "Company" and not "Name"?** A Lookup shows a record by one of its
> text fields. The Studio picks a field called Name, Title, Label or
> Description if the table has one, and only otherwise the first text
> field. We want lists of securities to show the symbol, so the table has
> no field called Name.

## Transactions

The heart of it. One record is one line of a register.

{{fields Transactions}}

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

{{fields Splits}}

## Bills

{{fields Bills}}

Set the table's **Caption** to `Bills and Deposits`. **Ends** uses *On or
after another field* (Next Due); **Amount** *More than zero*: a bill is
typed as a plain amount, and its **Kind** says which way the money goes.

## Budgets

{{fields Budgets}}

**No Duplicates** on a Lookup: one budget for a category.

## Holdings

{{fields Holdings}}

What an account holds of a security. Code writes these: from Schwab's
positions for a Schwab account, from the trades for any other.

## Prices

{{fields Prices}}

One record for a security for a day: what the candlestick chart draws.
**High** uses *At least another field* (Low).

## WorthHistory

{{fields WorthHistory}}

Set its **Caption** to `Net Worth History`. The program writes one line
for each day it's used; the Home page's line graph reads this table with
no code at all.

## Settings

{{fields Settings}}

Named values the program keeps for itself: the Schwab app's key and
tokens, the time of the last download.

## Listing

This one is different. It never holds a record.

{{fields Listing}}

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
