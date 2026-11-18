# What This Build Showed

Wayland Money was built to find out what Wayland Studio can carry on a
real job. This chapter is the result: what was used, what had to be
worked around, what couldn't be done, and what would be worth adding to
the Studio. The last section is for what you find when you follow the
guide yourself.

## What the Studio carried by itself

| Part of the Studio | Where Wayland Money uses it |
|---|---|
| Field types: Text, Long Text, Whole Number, Decimal, Currency, Yes/No, Date, Date and Time, Choice, Lookup, E-mail, URL, Phone | The thirteen tables ([chapter 2](02-tables.md)). Not used: Floating Point, Time. |
| Required, No Duplicates, Default Value, Max Length, Minimum/Maximum, Decimal Places, Pattern | Throughout. No Duplicates on a Lookup (one budget a category) and on Schwab's ids (no transaction twice) did real work. |
| Validation Rules | Nine of them: a percentage, zero or more, more than zero, not zero, different from another field, only when another field is filled in, on or after another field, at least another field, and a custom expression. |
| Rules belonging to the table | The Schwab download, the bills that enter themselves and the forms all write through the same rules, and none of them repeats one. |
| **Add Form for** | Accounts, Payees, Categories, Bills, Securities. Payees and Categories needed no changes and no code. |
| Lookups shown as lists that keep themselves up to date | Every Payee, Category, Account and Security list. |
| Label, TextBox, Button, CheckBox, OptionButton, ComboBox, ListBox, Frame, Shape, DateTimePicker | All used. Not used: Image, MediaPlayer. |
| DataGrid, listing its form's table | Accounts, Payees, Categories, Bills, Securities. |
| DataGrid, given its rows by code | The registers, Find, the budget, the portfolio, the reports, the planner. |
| Menu and ToolBar, with Actions | The Home page: 18 items and 9 buttons, most of them with no code. |
| Graph from a table, no code | Net worth over time. |
| Graph from code: Pie, Column, Bar, Line, Doughnut | Home, Budget, Portfolio, Reports, Planner. Clicking a slice drills down to its transactions. |
| Stock graph: candlesticks, averages, Bollinger bands, volume, RSI, live ticks | SecuritiesForm. |
| AIChat with tools of the app's own | The Advisor. |
| `form.args`, `App.openForm`, `App.showForm`, `App.form` | Forms opening each other on a record, and calling the library. |
| `Db`, `AppDates`, message boxes | Everywhere. |
| Check for Problems, Make Program | [Chapter 13](13-finish.md). |

Not exercised at all: MySQL and PostgreSQL, **Import Tables**, and
**Project > APIs** with its API tables (see the first gap below).

## What had to be worked around

Each of these is something the job needed that the Studio doesn't have
yet. The workaround is what this guide does; the last column is the
change that would remove it.

| # | What was needed | What happened | What the Studio could gain |
|---|---|---|---|
| 1 | Sign in to Schwab with **Project > APIs** | Its OAuth comes back to `http://127.0.0.1` only, and sends the client id and secret in the body. Schwab requires an `https://` callback and a Basic header. The app signs in with its own `XMLHttpRequest` code, and the user pastes the callback address. | In the API's sign-in: a **Callback URL** of your own (with pasting the address back, for an `https` loopback), and **Send the client secret as**: body or Basic header. Then Schwab's endpoints could be API tables. |
| 2 | A grid of one account's transactions | A DataGrid lists its whole table; **Search** only matches text. | A **Where** on the grid (and `grid.where = { Account: 3 }` in code). |
| 3 | Amounts as `1,234.50`, dates as `11/18/2026` | A grid shows a value as the table keeps it: `1234.5`. | A format for each column; Currency fields shown as money by default. |
| 4 | A running balance column | A grid only shows fields. | Calculated columns, or an event that lets code add to each row. |
| | *2, 3 and 4 together* | *The **Listing** table: fields that exist only as column headings, with `grid.rows` set by code. It works well, but it's a trick.* | |
| 5 | Columns of different widths | Every column of a grid is the same width: the one-letter `C` column is as wide as Memo. | Column widths. |
| 6 | Code shared by every form | There's no place for it. The Home page's (General) is the library, reached as `App.form("HomeForm")`, and closing the Home page has to end the program. A form isn't findable until its own `load` has finished. | **Project > Add Module**: functions every form can call. |
| 7 | Do something every minute; wait a moment | No Timer in the Toolbox. A QML `Timer { }` typed into (General) works (the Studio's own tests do it), but the manual doesn't mention it. | A Timer element. |
| 8 | Know when another form changed the data | `Connections { target: Db; function onChanged(table) { } }` in (General): works, undocumented. | A form event: **dataChanged**. |
| 9 | Web requests with headers of their own | `Db.callApi` only uses a project API's sign-in. `XMLHttpRequest` works in code (it doesn't wait for its answer), undocumented. | Document it; or headers on `Db.callApi`. |
| 10 | Type a new payee straight into the register | A ComboBox is a list only: no typing, no "add this". A `...` button opens Payees instead. | A ComboBox you can type in, that offers to add what isn't there. |
| 11 | Take a category off a transaction | A Lookup's list has no blank line: once chosen, it can be changed but not cleared by hand. | A *(none)* line for a Lookup that isn't Required. |
| 12 | List only investment accounts in Record a Trade | A Lookup lists every record of its table. | A filter on a Lookup: *only where Type is...* |
| 13 | Show a security by its symbol | A Lookup shows a field called Name, Title, Label or Description if there is one, else the first text field. The table couldn't have a field called Name. | A table property: **Display Field**. |
| 14 | "To Account is required when Kind is Transfer" | A rule isn't run on an empty field, and *Required when another field is filled in* can't test for a value. Checked in the form's code. | *Required when* with an expression. |
| 15 | Totals by category, by month | `Db.select` has no sums or groups, and one test for each field (no "between two dates"). The code reads the tables whole and adds up in JavaScript. Fine for some thousands of transactions; it will slow with many more. | `Db.sum`, group by, and two tests on a field. |
| 16 | Two writes that succeed or fail together | A transfer's two halves, and a download's hundreds of records, are separate writes. If the program stops between them, one half is left. | `Db.transaction(function () { ... })`. |
| 17 | Keep a secret | App code has only tables: the Schwab secret and tokens are plain text in Settings. (The Studio's own API tokens are kept in a file only the user can read.) | `App.secret("name")`: kept as the Studio keeps its own. |

Smaller things met on the way:

- **A function in (General) can't start with a capital letter**, or have
  a name the form already uses (`load`, `current`, `save`, `close`,
  `show`, `update`). Check for Problems does say so.
- **Setting a box's text from code doesn't run its `changed` event.**
  Only typing does. Sensible, but code that fills a box has to call the
  work itself.
- **A message box doesn't wait.** Everything after an answer goes inside
  its function. That shapes Delete, Finish and Done.
- **The Phone type accepts `12345`.** It's lenient about what a phone
  number is.
- **Two warnings that are the Studio's own**, seen in the Output window
  and when the Studio opens a project, and harmless: `ScrollBar95:
  Binding loop detected for property "needed"` (from a DataGrid, when
  its rows change) and `StudioDialogs.qml:827: Unable to assign
  [undefined] to bool` (the Graph Editor). Both are worth fixing in the
  Studio so that a real mistake in an app's code isn't lost among them.

## What was left out

Parts of Microsoft Money that Wayland Money doesn't have, and why:

| Left out | Why |
|---|---|
| Importing and exporting files (QIF, OFX, CSV); exporting a report; a Backup command | App code can't read or write a file, or show a file dialog. |
| Printing checks and reports | There's no printing. |
| Other banks; online bill payment | Only Schwab's API is wired in. Schwab's Trader API is for brokerage accounts: an account that isn't offered when you sign in isn't available to it. |
| Placing trades | On purpose. The program only reads. |
| Capital gains, by lot | Needs every purchase since the beginning; Schwab's API gives a year. The portfolio shows gain against average cost instead. |
| A tax estimator; the Lifetime Planner | Out of scope. Tax-related categories and their totals are there. |
| More than one currency | Everything is US dollars. |
| A password on the file | No way to protect the database file from app code. |
| Tabs (Money's register had them) | No tab element: option buttons and separate forms instead. |
| Typing ahead in the payee and category boxes | Gap 10. |

## What to do to the Studio first

In the order this build would have gained most:

1. **OAuth that Schwab accepts** (gap 1). It's the one place the Studio's
   own feature couldn't be used at all, and the sign-in code in this
   app's library would become a dialog.
2. **The grid**: Where, column formats, column widths (gaps 2, 3, 5).
   Most of the Listing technique would go.
3. **A module for shared code, and a Timer** (gaps 6, 7, 8).
4. **Lookups**: a blank line, a display field, a filter (gaps 11, 12, 13).
5. **Files and printing**: the biggest missing part of Money itself.
6. **Sums, and writes that go together** (gaps 15, 16).

## Your own findings

The guide's steps in the Studio's windows were written from its manual,
and the Schwab chapter was run against a stand-in. Following it for real
is the rest of the test. Note here what you meet:

| Chapter | Step | What happened | What was expected |
|---|---|---|---|
| | | | |
| | | | |
| | | | |
