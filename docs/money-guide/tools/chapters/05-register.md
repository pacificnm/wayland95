# The Account Register

The register is where the work is done: an account's transactions with a
running balance, and under them the form to enter one, as a withdrawal, a
deposit or a transfer.

![The register](images/register.png)

## More for the library

Open HomeForm's **(General)** and add this at the end. It's the part of
entering transactions that isn't any one form's business: a transfer's
two halves, and deleting cleanly.

{{code HomeForm (General) register}}

And a stand-in, until [chapter 9](09-investing.md):

{{code HomeForm (General) register-noholdings}}

A transfer from Checking to Savings is two records: `-300` in Checking
naming Savings, and `+300` in Savings naming Checking, each holding the
other's id. `matchTransfer` is called after either one is saved, and
makes the other agree with it, whichever side was edited.

## RegisterForm

**Project > Add Form**, and set the form:

{{form RegisterForm}}

Its **DataTable** is Transactions: the boxes at the bottom show and edit
one transaction, the form's record.

{{elements RegisterForm *}}

Put `entryFrame` on first. The three OptionButtons share a **Group**, so
choosing one clears the others. `amountBox` has no DataField: people type
`45.10` for a payment, and code turns that into `-45.10` for the table.
`toLabel` and `toCombo` are only shown for a transfer; code hides them.

### Its code

**(General)**:

{{code RegisterForm (General)}}

{{events RegisterForm *}}

How it works:

- **`fill()`** is the Listing technique in full. It reads the account's
  transactions in date order, keeps a running balance, and builds one row
  for each with everything already as text. The names of payees and
  categories are read once (`home.names`) rather than once for each row.
- **The grid and the form** are tied by two lines: choosing a row loads
  that transaction (`grid.rowSelected`), and after `fill()` the grid
  marks the row of the form's record.
- **`enter()`** puts the sign on the amount, gives a payee its usual
  category, saves, and then lets the library write the transfer's other
  half. `form.save()` runs the table's rules: a transfer to the same
  account comes back refused, with `toCombo` framed in red.
- **After a save the record is read again** (`form.loadRecord(id)`). The
  library has just written the other half's id into this transaction;
  without reading it back, the form would still hold the old record and
  the next Enter would write the blank over it.
- **The `Connections`** at the end of (General) refills the register when
  something else changes its tables: a split, a bill entering itself, a
  download.
- **Enter** in `amountBox` enters the transaction (its `accepted` event),
  so a row of transactions can be typed without the mouse.

## SplitForm

One transaction, several categories: 120.00 at the grocer that was 80.00
of groceries and 40.00 of lunch.

![Splitting a transaction](images/split.png)

{{form SplitForm}}

{{elements SplitForm *}}

**(General)**:

{{code SplitForm (General)}}

{{events SplitForm *}}

The form is opened with the transaction: `App.openForm("SplitForm",
{ transaction: id })`, which the form reads as `form.args.transaction`.
Each part is saved with the transaction's own sign. **Done** won't leave
the parts adding up to something else without asking.

Splits has a Lookup to Transactions, so the table would refuse to delete
a split transaction; that's why the library's `removeTransaction` deletes
the parts first.

## FindForm

Every account's transactions, searched at once.

![Find](images/find.png)

{{form FindForm}}

{{elements FindForm *}}

**(General)**:

{{code FindForm (General)}}

{{events FindForm *}}

A DateTimePicker with nothing in it has the value `""`, so an empty
**From** or **To** leaves that end open. Double-clicking a result opens
that account's register on the transaction.

## Into the Home page

On HomeForm:

`accountGrid`, **rowActivated** (a double-click on an account):

{{code HomeForm accountGrid.rowActivated}}

The menu's **itemClicked** and the tool bar's **itemClicked** are given
here once, whole. They name items that later chapters add; until an item
exists its line simply never matches.

`menu1`, **itemClicked**:

{{code HomeForm menu1.itemClicked}}

`toolBar1`, **itemClicked**:

{{code HomeForm toolBar1.itemClicked}}

Add to the menu:

{{menu HomeForm fileFind,accountsRegister}}

(Put **&Find Transactions...** above E&xit with a separator between, and
**Open &Register** under Account List.) *Open Register* has no Action: it
needs to know which account is selected, so its work is in `itemClicked`.

And to the tool bar:

{{toolbar HomeForm registerTool,findTool}}

## Try it

**F5**. Double-click **Checking** on the Home page.

1. Payee `Corner Grocer`, Number `101`, Amount `45.10`, **Enter**. The
   row appears with Category *Groceries* (the payee's usual one) and a
   balance of `954.90`.
2. Click **Deposit**, Payee `Employer`, Amount `$2,500.00`, Enter:
   `3,454.90`.
3. **Enter** with no amount: it asks for one.
4. Click **Transfer**: Category gives way to **To account**. Choose
   `Checking` itself, Amount `300`, Enter: refused, *A transfer goes to
   another account.* Choose `Savings`: entered. On the Home page Savings
   is now `5,300.00`. Open its register: the other half is there.
5. Back in Checking, click the transfer, change the amount to `350`,
   Enter. Savings follows.
6. A new withdrawal, `Corner Grocer`, `120`, then **Split...**: Category
   `Groceries`, Amount `80`, **Save Part**; the Amount box now offers the
   `40.00` that's left; Category `Dining Out`, Save Part, **Done**. The
   register says *(split)*.
7. Type `grocer` in **Find**: only those rows.
8. Select the transfer and **Delete**: both halves go, and Savings is
   `5,000.00` again.

The Home page's pie now shows the month's spending.
