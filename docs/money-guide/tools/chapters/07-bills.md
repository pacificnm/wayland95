# Bills and Deposits

Rent every month, a paycheck every two weeks, a transfer to savings.
Each is entered once here with how often it comes round; the Home page
reminds you when one is near, one click enters it in the register, and
the ones you trust can enter themselves.

![Bills and Deposits](images/bills.png)

## The library's part

In HomeForm's **(General)**, **delete** the stand-in `showBills` from
chapter 3 and add this at the end:

{{code HomeForm (General) bills}}

- **`nextDue`** moves a date on by the bill's frequency. `addMonths`
  keeps to the end of a short month: the 31st of January goes to the
  28th of February.
- **`enterBill`** writes the transaction (a transfer gets its other half
  from `matchTransfer`), then **`skipBill`** moves the bill to its next
  date, or switches it off when it was a one-off or has passed its end.
- **Two Timers**. `minute` runs for as long as the program does and
  calls `everyMinute()` each minute, so a bill that falls due while the
  program is open is entered. `startup` runs it once, just after the
  program starts.

Change the form's **load** to start that timer:

{{code HomeForm load bills}}

`billList`, **doubleClicked**:

{{code HomeForm billList.doubleClicked}}

## BillsForm

Open the **Bills** table and **Project > Add Form for Bills**. Then:

1. The form: **Height** `473`.
2. `grid`: **Columns** `Name`, `NextDue`, `Amount`, `Frequency`;
   **OrderBy** `NextDue`.
3. Add:

{{elements BillsForm enterButton,skipButton,dueLabel}}

**(General)**:

{{code BillsForm (General)}}

{{events BillsForm *}}

`saveButton` already has `form.save();`: replace it with the code above.
The table can't say "To Account is required, but only for a transfer"
(a rule isn't run on an empty field), so `check()` says it.

This form's grid is the plain kind: it lists the Bills table itself, so
the amounts show as the table keeps them (`1200`, not `1,200.00`). That's
the trade: no code, no formatting.

## Into the menu

A new menu **&Bills** after Accounts:

{{menu HomeForm billsList}}

{{toolbar HomeForm billsTool}}

## Try it

**F5**, **Bills > Bills & Deposits...**:

1. **Rent**: Account Checking, Payee Landlord, Category Housing : Rent,
   Amount `1200`, Monthly, **Next Due** yesterday, tick **Enter By
   Itself**, Save. Within a minute the Home page's status line says *1
   bill was entered by itself.*, Checking is 1,200.00 lower, and the
   bill's Next Due has moved on a month.
2. **Electric**: Account Checking, Amount `80`, Monthly, Next Due three
   days from now. It appears on the Home page under **Bills coming up**.
   Select it here and **Skip This One**: next month.
3. **To savings**: Kind Transfer (the **To Account** row appears),
   Account Checking, To Account Savings, `100`, Every two weeks, due
   today. **Enter in Register**: both accounts move.
4. Try an Amount of `-5`, or an **Ends** date before Next Due: refused.
