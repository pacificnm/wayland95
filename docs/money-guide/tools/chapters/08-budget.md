# The Budget

A monthly amount for a category, set beside what was really spent.

![The budget](images/budget.png)

## BudgetForm

**Project > Add Form**:

{{form BudgetForm}}

{{elements BudgetForm *}}

`monthPicker`'s **DateFormat** is *Custom* with **CustomDateFormat**
`MMMM yyyy`, so it reads *November 2026*; any day picked in a month means
that month. `graph1`:

{{graph BudgetForm graph1}}

**(General)**:

{{code BudgetForm (General)}}

{{events BudgetForm *}}

`fill()` asks the library what was spent in the month
(`home.totalsByCategory`, which already knows about splits and leaves
transfers out) and sets each budget beside it. The graph gets two series,
so each category has a Budget column and a Spent column side by side.

The form has a DataTable (Budgets) and its grid shows Listing, which is
the same arrangement as the register: clicking a row loads that budget
into the boxes at the bottom.

## Into the menu

{{menu HomeForm billsBudget}}

{{toolbar HomeForm budgetTool}}

## Try it

**Bills > Budget...**. **New**, Category Groceries, `400`, Save. Then
Dining Out `30` and Housing : Rent `1200`. The grid shows each with what
was spent this month and what's left; a category that's over shows a
negative **Left**, and the total line turns red when the whole budget is
overspent. Try a second budget for Groceries: *There is already a record
with this Category.* Leave the form open and enter a grocery transaction
in a register: the budget follows.
