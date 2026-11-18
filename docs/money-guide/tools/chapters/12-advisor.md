# The Advisor

Money had an Advisor that noticed things about your accounts. Ours is an
**AIChat** element: Claude, on a form, answering questions from your own
figures. *What did I spend on groceries last month? Which bills are due
this week? What's my net worth?*

![The Advisor](images/advisor.png)

It needs an Anthropic API key and the internet. Skip this chapter and
the rest of the program is unaffected.

## The key

**Project > Properties...**, **AI Chat (Claude)**, **API key**. Type
`${env:ANTHROPIC_API_KEY}` rather than the key itself: the key is then
read from that environment variable where the program runs, and isn't
written into the project or into the program you make. **Test** says
whether it works.

## AdvisorForm

**Project > Add Form**:

{{form AdvisorForm}}

{{elements AdvisorForm *}}

The properties that decide what Claude may do:

| Property | Set to | Why |
|---|---|---|
| DataAccess | Read (as it starts) | It can look up records. It can't add, change or delete them. |
| FormAccess | False | It can't fill in forms or press buttons. An adviser reads the books; it doesn't write in them. |
| ApiAccess | False (as it starts) | It can't make requests of web APIs. |
| ConfirmChanges | True (as it starts) | Would ask before any change, if it could make one. |

**Instructions** is what Claude is told about its job, in your words. As
written above: it's the Advisor in a personal finance program, it should
use its tools for figures, a negative Amount is money going out, and it
gives figures, not investment recommendations.

## Tools of its own

With DataAccess Read, Claude can already find and read records. But
"what did I spend on groceries" would mean reading every transaction and
adding them up itself, which is slow, costs more, and can go wrong. The
library already does that sum correctly. So the form gives Claude three
tools that call it. The form's **load**:

{{code AdvisorForm load}}

`newButton`, **clicked**:

{{code AdvisorForm newButton.clicked}}

**(General)**:

{{code AdvisorForm (General)}}

Each `addTool` gives a name, a description that says **when to use it**
(Claude decides from this), what it takes, and the function. The
function returns plain values: Claude is told them and answers in words.

## Into the menu

A **&Help** menu at the end:

{{menu HomeForm helpAdvisor}}

{{toolbar HomeForm advisorTool}}

## Try it

**F2**. Ask: *How much did I spend this month, and on what?* A grey line
shows the tool it used (*spending_by_category*), then the answer. Ask
what's due this week; ask it to delete a transaction (it will say it
can't).

## Before you rely on it

- **What's sent to Anthropic**: your question, the Instructions, the
  names of your tables and fields, and whatever the tools return or
  Claude reads: category totals, account names and balances, bills,
  transactions it looks up. That is your financial data leaving your
  computer. If that's not acceptable, leave this form out.
- It's billed to the key's account, by use.
- Claude can be wrong, and it isn't a licensed adviser. It's for *what
  do my figures say*, not *what should I buy*.
- This form was checked to compile and open, and its tools return the
  right figures when called. It wasn't run against Claude itself (that
  needs a key); the AIChat element has the Studio's own tests.
