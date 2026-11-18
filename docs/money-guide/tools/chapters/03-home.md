# The Home Page

The Home page is what the program opens with: the accounts and what
they're worth, the bills coming up, this month's spending, and net worth
over time. It also holds the code the whole program shares.

![The Home page](images/home.png)

## The form

Click Form1 in the Project window and select the form itself (click its
empty space). Set:

{{form HomeForm}}

**Project > Properties...**: **Startup form** is `HomeForm`.

## A menu bar and a tool bar

Put these on first: they sit across the top and move everything else
down, and the positions below allow for them.

1. Double-click **Menu** in the Toolbox. Open its **Menus** (F4): the
   Menu Editor starts with **&File > E&xit** and **&Help > &About**.
   Delete the Help menu for now (select each of its two lines and
   **Delete**). The menus grow chapter by chapter; the whole menu is in
   [chapter 13](13-finish.md#the-whole-menu).
2. Double-click **ToolBar**. Open its **Buttons** and delete the two it
   starts with (New and Save: this form has no record to save). Buttons
   are added as the forms they open get made.

## The elements

{{elements HomeForm accountsFrame,billsFrame,accountGrid,assetsCaption,assetsLabel,owedCaption,owedLabel,worthCaption,worthLabel,billList,spendingGraph,syncButton,syncLabel,worthGraph,statusLabel}}

Put the two Frames on first (a Frame goes behind what's put on it
afterwards).

`accountGrid` is the first of the grids that code fills: its
**DataTable** is Listing and its **Columns** the three headings it wants.

### The two graphs

When you put a **Graph** on the form the Graph Editor opens.

`spendingGraph` is a pie that code fills:

{{graph HomeForm spendingGraph}}

`worthGraph` needs no code. It reads the WorthHistory table by itself,
and again whenever a line is added:

{{graph HomeForm worthGraph}}

## The library

Select the form, press **F7**, and choose **(General)**. Everything here
can be called by this form's events, and by every other form as
`App.form("HomeForm").fmt(12.5)`.

{{code HomeForm (General) core}}

And, until [chapter 7](07-bills.md) writes the real one:

{{code HomeForm (General) core-nobills}}

Things worth noticing:

- **`fmt` and `parseMoney`** are how every amount in the program is shown
  and read. A table keeps `1234.5`; people read `1,234.50` and type
  `$1,234.50`.
- **`balances()`** reads every transaction and adds them up by account,
  each time it's asked. That's the whole of "the balance is never
  stored".
- **`Timer` and `Connections`** aren't functions: (General) can hold
  these too. `Connections` listens to `Db`: its `changed` says a table
  was written to, by anything. The page doesn't redraw at once (a
  download writes hundreds of records): it starts a 300 millisecond
  timer, and redraws once when the writing stops.
- **`snapshot()`** writes to WorthHistory, which would itself set the
  timer off again, for ever. That's why the `Connections` leaves that
  table out, and why `snapshot()` only writes when the figure changed.
- **`accountGrid.rows = rows`** is the Listing technique from the last
  chapter. Each row's `id` is the account's id, so `accountGrid.currentId`
  is the account that's clicked.
- **`seed()`** gives a new file its categories.

A name in (General) can't be one the form has already: don't call a
function `load`, `current`, `save`, `close`, `show` or `update`.

## Its events

The form's **load**:

{{code HomeForm load}}

The form's **unload** (closing the Home page ends the program, as closing
Money's window did; and it keeps the library there for as long as any
other form is open):

{{code HomeForm unload}}

## Try it

**F5**. The page opens with an empty account list, an empty pie, and
*Welcome to Wayland Money.* at the bottom. Close it, open **Categories**
in the Studio and click **Data**: 25 categories are there, seven of them
Tax-related.

If the Output window says `HomeForm, (General), line …`, a line of the
library was mistyped; the line number is the line within (General).

> **One thing to know about `App.form("HomeForm")`**: a form isn't in the
> program's list of open forms until its own `load` has finished. So the
> Home page can't open another form from its `load` and have that form
> find the library. Where the program needs to do something at startup
> that opens a form, it waits a moment with a Timer first
> ([chapter 7](07-bills.md), [chapter 10](10-schwab.md)).
