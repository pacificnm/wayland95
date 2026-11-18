# Balancing an Account

When a statement comes, you tick off what's on it until the program's
cleared balance agrees with the statement's ending balance. Then those
transactions are marked reconciled, and stay out of the way next time.

![Balancing an account](images/reconcile.png)

A transaction's **Status** goes *Unreconciled* (entered, not seen on a
statement), *Cleared* (ticked), *Reconciled* (a finished statement had
it). The register shows these as blank, `C` and `R`.

## ReconcileForm

**Project > Add Form**:

{{form ReconcileForm}}

No DataTable: this form shows no single record. It works on many.

{{elements ReconcileForm *}}

`statementPicker` starts on today (**Value** `today`), and can't be left
empty. `finishButton` starts disabled; code enables it when the
difference reaches zero.

**(General)**:

{{code ReconcileForm (General)}}

{{events ReconcileForm *}}

The cleared balance is the opening balance plus every transaction that
isn't *Unreconciled*. **Finish** only works at a difference of exactly
`0.00`, which is why the amounts are rounded to cents before they're
compared: money added up as fractions can be off by a hair.

A message box doesn't wait for its answer, so what happens after **OK**
(closing the form) is inside the function given to it.

## Into the menu

Under **&Accounts**, after Open Register:

{{menu HomeForm accountsBalance}}

No Action again: the code you put in `menu1.itemClicked` last chapter
opens the form for the account that's selected.

## Try it

Select Checking on the Home page, then **Accounts > Balance an
Account...**. Type an **Ending balance** of `954.90`: the difference
shows in red. Double-click the Corner Grocer `45.10` row: it gets a `C`,
the cleared balance becomes `954.90`, the difference `0.00` in green, and
**Finish** comes alive. Finish. In the register that row now has `R`, and
the register's own **Cleared** figure agrees.
